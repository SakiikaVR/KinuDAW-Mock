// Copyright (c) 2026 KinuDAW contributors. MIT License.
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#include "miniaudio.h"
#include "AudioEngine.h"
#include "PluginIPC.h"
#include "DelayCompensation.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <cstring>
#include <thread>
#include <objbase.h>
#include <avrt.h>
namespace Kinu {
struct AudioEngine::Device {
    ma_context context{}; bool contextInitialized=false;
    ma_device value{},capture{}; bool initialized=false,started=false,capturing=false;
    std::array<float,65536> ring{}; std::atomic<unsigned> write{0},read{0};
    std::atomic<bool> recording{false},overflow{false}; std::thread writer;
    std::filesystem::path recordingPath; uint64_t recorded=0; bool diskFailed=false;
    std::array<float,32768> outputRing{}; std::array<double,16384> outputBeats{};
    std::atomic<unsigned> outputWrite{0},outputRead{0},underruns{0};
    std::atomic<bool> rendering{false}; std::thread renderer; HANDLE renderWake=nullptr;
    unsigned queueFrames=512;
};
struct AudioEngine::Effects {
    struct State { float prev[2]{},highpass[2]{},envelope=0; std::array<float,8192> delay[2]{}; unsigned position=0; };
    std::array<State,Tracks> tracks{};
    std::array<double,128> frequencies{};
};
struct AudioEngine::Compensation {
    std::array<std::array<SampleDelay<float>,2>,Tracks> delays;
    SampleDelay<double> beats;
    std::array<unsigned,Tracks> previous{};
    unsigned previousTotal=0;
    void reset() { for(auto& track:delays) for(auto& side:track) side.reset(); beats.reset(); }
};
AudioEngine::AudioEngine() : device_(std::make_unique<Device>()),effects_(std::make_unique<Effects>()),compensation_(std::make_unique<Compensation>()) { for(int p=0;p<128;++p) effects_->frequencies[p]=440*std::pow(2.,(p-69)/12.); }
AudioEngine::~AudioEngine() { std::string ignored; endRecording(ignored); stop(); if(device_->renderWake) CloseHandle(device_->renderWake); if(device_->initialized) ma_device_uninit(&device_->value); if(device_->contextInitialized) ma_context_uninit(&device_->context); }
bool AudioEngine::start(std::string& error) {
    if(!device_->initialized) {
        if(!device_->contextInitialized) {
            ma_backend backends[]={ma_backend_wasapi}; auto contextConfig=ma_context_config_init(); contextConfig.threadPriority=ma_thread_priority_realtime;
            if(ma_context_init(backends,1,&contextConfig,&device_->context)!=MA_SUCCESS) { error="WASAPI context initialization failed"; return false; }
            device_->contextInitialized=true;
        }
        auto config=ma_device_config_init(ma_device_type_playback);
        config.playback.format=ma_format_f32; config.playback.channels=2; config.sampleRate=48000;
        config.periodSizeInFrames=requestedFrames_; config.periods=2; config.performanceProfile=ma_performance_profile_low_latency;
        config.playback.shareMode=exclusive_?ma_share_mode_exclusive:ma_share_mode_shared; config.wasapi.usage=ma_wasapi_usage_pro_audio; config.pUserData=this;
        config.wasapi.noAutoConvertSRC=MA_TRUE; config.wasapi.noHardwareOffloading=MA_TRUE;
        config.dataCallback=[](ma_device* d,void* out,const void*,ma_uint32 n) {
            auto* engine=static_cast<AudioEngine*>(d->pUserData); auto* device=engine->device_.get(); auto* output=static_cast<float*>(out);
            unsigned r=device->outputRead.load(std::memory_order_relaxed),w=device->outputWrite.load(std::memory_order_acquire),count=std::min(n,w-r);
            for(unsigned i=0;i<count;++i) { unsigned index=(r+i)%device->outputBeats.size(); output[i*2]=device->outputRing[index*2]; output[i*2+1]=device->outputRing[index*2+1]; }
            std::fill(output+count*2,output+n*2,0.f); if(count<n && engine->deviceActive_.load()) device->underruns.fetch_add(1);
            if(count) engine->audiblePosition_.store(device->outputBeats[(r+count-1)%device->outputBeats.size()]);
            device->outputRead.store(r+count,std::memory_order_release); SetEvent(device->renderWake);
        };
        if(ma_device_init(&device_->context,&config,&device_->value)!=MA_SUCCESS) {
            config.periodSizeInFrames=512; config.playback.shareMode=ma_share_mode_shared; exclusive_=false;
            if(ma_device_init(&device_->context,&config,&device_->value)!=MA_SUCCESS) { error="WASAPI audio device initialization failed"; return false; }
        }
        device_->initialized=true;
        device_->queueFrames=std::clamp(unsigned(std::ceil(device_->value.playback.internalPeriodSizeInFrames*48000./device_->value.playback.internalSampleRate))+Block,256u,8192u);
        device_->renderWake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    }
    if(device_->started) return true;
    device_->outputRead.store(0); device_->outputWrite.store(0); device_->rendering.store(true);
    device_->renderer=std::thread([this] {
        CoInitializeEx(nullptr,COINIT_MULTITHREADED); SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
        DWORD task=0; HANDLE mmcss=AvSetMmThreadCharacteristicsW(L"Pro Audio",&task);
        if(mmcss) AvSetMmThreadPriority(mmcss,AVRT_PRIORITY_CRITICAL);
        std::array<float,Block*2> buffer{};
        while(device_->rendering.load()) {
            unsigned w=device_->outputWrite.load(),r=device_->outputRead.load(std::memory_order_acquire);
            if(w-r+Block<=device_->queueFrames) {
                try { render(buffer.data(),Block); }
                catch(...) { buffer.fill(0); reading_.store(-1); renderedBeats_.fill(beat_); playing_.store(false); }
                for(unsigned i=0;i<Block;++i) { auto index=(w+i)%device_->outputBeats.size(); device_->outputRing[index*2]=buffer[i*2]; device_->outputRing[index*2+1]=buffer[i*2+1]; device_->outputBeats[index]=renderedBeats_[i]; }
                device_->outputWrite.store(w+Block,std::memory_order_release);
            } else WaitForSingleObject(device_->renderWake,20);
        } if(mmcss) AvRevertMmThreadCharacteristics(mmcss); CoUninitialize();
    });
    auto deadline=GetTickCount64()+1000; while(device_->outputWrite.load()<device_->queueFrames-Block && GetTickCount64()<deadline) Sleep(1);
    if(ma_device_start(&device_->value)!=MA_SUCCESS) { device_->rendering.store(false); SetEvent(device_->renderWake); device_->renderer.join(); error="Audio device start failed"; return false; }
    deviceActive_.store(true);
    device_->started=true; return true;
}
void AudioEngine::stop() {
    if(device_->started) { ma_device_stop(&device_->value); device_->started=false; deviceActive_.store(false); }
    if(device_->renderer.joinable()) { device_->rendering.store(false); SetEvent(device_->renderWake); device_->renderer.join(); }
}
unsigned AudioEngine::bufferFrames() const { return device_->initialized?device_->value.playback.internalPeriodSizeInFrames:0; }
unsigned AudioEngine::nativeSampleRate() const { return device_->initialized?device_->value.playback.internalSampleRate:0; }
std::string AudioEngine::deviceName() const { return device_->initialized?device_->value.playback.name:""; }
bool AudioEngine::configure(unsigned frames,bool exclusive,std::string& error) {
    bool started=device_->started; stop();
    if(device_->initialized) { ma_device_uninit(&device_->value); device_->initialized=false; }
    if(device_->renderWake) { CloseHandle(device_->renderWake); device_->renderWake=nullptr; }
    requestedFrames_=std::clamp(frames,64u,2048u); exclusive_=exclusive;
    return !started || start(error);
}
bool AudioEngine::beginRecording(const std::filesystem::path& path,std::string& error) {
    if(device_->capturing || !device_->contextInitialized) { error="Recorder unavailable"; return false; }
    device_->recordingPath=path; device_->write.store(0); device_->read.store(0); device_->overflow.store(false); device_->recorded=0; device_->diskFailed=false;
    auto config=ma_device_config_init(ma_device_type_capture); config.capture.format=ma_format_f32; config.capture.channels=2; config.sampleRate=48000; config.periodSizeInFrames=Block;
    config.wasapi.usage=ma_wasapi_usage_pro_audio; config.pUserData=device_.get();
    config.wasapi.noAutoConvertSRC=MA_TRUE; config.wasapi.noHardwareOffloading=MA_TRUE;
    config.dataCallback=[](ma_device* capture,void*,const void* input,ma_uint32 frames) {
        auto* d=static_cast<Device*>(capture->pUserData); if(!input || !d->recording.load()) return;
        unsigned w=d->write.load(std::memory_order_relaxed),r=d->read.load(std::memory_order_acquire),samples=frames*2;
        if(samples>d->ring.size() || w-r+samples>d->ring.size()) { d->overflow.store(true); return; }
        auto* data=static_cast<const float*>(input); for(unsigned i=0;i<samples;++i) d->ring[(w+i)%d->ring.size()]=data[i];
        d->write.store(w+samples,std::memory_order_release);
    };
    if(ma_device_init(&device_->context,&config,&device_->capture)!=MA_SUCCESS) { error="WASAPI microphone initialization failed"; return false; }
    device_->capturing=true; device_->recording.store(true);
    device_->writer=std::thread([d=device_.get()] {
        auto temporary=d->recordingPath; temporary+=L".tmp"; std::ofstream out(temporary,std::ios::binary);
        std::array<char,44> header{}; out.write(header.data(),header.size());
        std::array<float,4096> chunk{};
        while(d->recording.load() || d->read.load()!=d->write.load()) {
            unsigned r=d->read.load(),w=d->write.load(std::memory_order_acquire),n=std::min<unsigned>(w-r,chunk.size());
            if(n) { for(unsigned i=0;i<n;++i) chunk[i]=d->ring[(r+i)%d->ring.size()]; out.write(reinterpret_cast<char*>(chunk.data()),n*4); d->recorded+=n; d->read.store(r+n,std::memory_order_release); }
            else Sleep(2);
            if(!out || d->recorded>48000ull*2*60*60) { d->diskFailed=true; d->recording.store(false); break; }
        }
        uint32_t bytes=uint32_t(d->recorded*4),size=bytes+36,rate=48000,byteRate=384000,fmt=16; uint16_t code=3,channels=2,align=8,bits=32;
        out.seekp(0); out.write("RIFF",4); out.write(reinterpret_cast<char*>(&size),4); out.write("WAVEfmt ",8); out.write(reinterpret_cast<char*>(&fmt),4); out.write(reinterpret_cast<char*>(&code),2); out.write(reinterpret_cast<char*>(&channels),2); out.write(reinterpret_cast<char*>(&rate),4); out.write(reinterpret_cast<char*>(&byteRate),4); out.write(reinterpret_cast<char*>(&align),2); out.write(reinterpret_cast<char*>(&bits),2); out.write("data",4); out.write(reinterpret_cast<char*>(&bytes),4); out.flush();
        d->diskFailed|=!out; out.close();
        if(!d->diskFailed && !MoveFileExW(temporary.c_str(),d->recordingPath.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) d->diskFailed=true;
    });
    if(ma_device_start(&device_->capture)!=MA_SUCCESS) { endRecording(error); error="Microphone start failed"; return false; } return true;
}
std::filesystem::path AudioEngine::endRecording(std::string& error) {
    if(!device_->capturing) return {};
    ma_device_stop(&device_->capture); device_->recording.store(false); if(device_->writer.joinable()) device_->writer.join();
    ma_device_uninit(&device_->capture); device_->capturing=false;
    if(device_->diskFailed) { error="Recording disk write failed; temporary data retained"; return {}; }
    if(device_->overflow.load()) error="Recording completed with dropped input samples";
    return device_->recordingPath;
}
void AudioEngine::publish(const Scene& scene) {
    // Single UI writer; the callback pins an immutable slot before accessing it.
    int p=published_.load(),r=reading_.load();
    for(int i=0;i<3;++i) if(i!=p && i!=r) { scenes_[i]=scene; published_.store(i); return; }
}
void AudioEngine::transport(bool play,double beat) {
    seek_.store(std::max(0.,beat)); seekVersion_.fetch_add(1,std::memory_order_release); playing_.store(play);
}
const AudioFile* AudioEngine::import(const std::filesystem::path& path,std::string& error) {
    for(const auto& f:media_) if(f->path==path.u8string()) return f.get();
    auto config=ma_decoder_config_init(ma_format_f32,2,48000);
    ma_decoder decoder{};
    if(ma_decoder_init_file_w(path.c_str(),&config,&decoder)!=MA_SUCCESS) { error="Unsupported or unreadable audio file (WAV / FLAC / MP3)"; return nullptr; }
    ma_uint64 length=0; ma_decoder_get_length_in_pcm_frames(&decoder,&length);
    if(!length || length>48000ull*60*60) { ma_decoder_uninit(&decoder); error="Audio length invalid or exceeds one hour"; return nullptr; }
    auto media=std::make_unique<AudioFile>(); media->path=path.u8string();
    try { media->samples.resize(size_t(length)*2); }
    catch(...) { ma_decoder_uninit(&decoder); error="Insufficient memory for audio file"; return nullptr; }
    ma_uint64 read=0; auto result=ma_decoder_read_pcm_frames(&decoder,media->samples.data(),length,&read); ma_decoder_uninit(&decoder);
    if(result!=MA_SUCCESS && result!=MA_AT_END) { error="Audio decoding failed"; return nullptr; }
    media->samples.resize(size_t(read)*2);
    auto* ptr=media.get(); media_.push_back(std::move(media)); return ptr;
}
bool AudioEngine::loadPlugin(int track,const PluginInfo& info,std::string& error,int slot) {
    if(track<0 || track>=Tracks || slot<0 || slot>3) return false;
    // Construct replacement first. A failed load leaves the previous worker usable.
    try {
        auto next=std::make_unique<RemotePlugin>(info,track,slot); next->synchronous(true,20); bool started=device_->started; stop();
        if(slot==0) { plugins_[track]=std::move(next); pluginFault_[track].store(false); }
        else effectsPlugins_[track][slot-1]=std::move(next);
        bypass_[track][slot].store(false); compensation_->reset();
        if(started) return start(error); return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}
bool AudioEngine::editor(int track,std::string& error,int slot) { if(!hasPlugin(track,slot)) return false; auto& p=slot?effectsPlugins_[track][slot-1]:plugins_[track]; return p->openEditor(error); }
bool AudioEngine::unloadPlugin(int track,std::string& error,int slot) {
    if(!hasPlugin(track,slot)) return true;
    bool started=device_->started; stop(); (slot?effectsPlugins_[track][slot-1]:plugins_[track]).reset();
    bypass_[track][slot].store(false); compensation_->reset();
    if(!slot) { held_[track].fill(false); pluginFault_[track].store(false); }
    if(!slot) for(int c=0;c<16;++c) { keys_[track][c].fill(false); sustained_[track][c].fill(false); sustain_[track][c]=false; }
    return !started || start(error);
}
bool AudioEngine::hasPlugin(int track,int slot) const { return track>=0 && track<Tracks && slot>=0 && slot<=3 && bool(slot?effectsPlugins_[track][slot-1]:plugins_[track]); }
bool AudioEngine::pluginHealthy(int track,int slot) const { return hasPlugin(track,slot) && (slot?effectsPlugins_[track][slot-1]->alive():!pluginFault_[track].load() && plugins_[track]->alive()); }
unsigned AudioEngine::underruns() const { return device_->underruns.load(); }
const PluginInfo* AudioEngine::pluginInfo(int track,int slot) const { return hasPlugin(track,slot)?&(slot?effectsPlugins_[track][slot-1]:plugins_[track])->info:nullptr; }
void AudioEngine::midi(int track,int pitch,int velocity,bool on,int channel) { midiMessage(track,(on?0x90:0x80)|(channel&15),pitch,velocity); }
void AudioEngine::midiMessage(int track,int status,int a,int b) {
    if(track<0 || track>=Tracks || status<0x80 || status>=0xf0 || a<0 || a>127 || b<0 || b>127) return;
    auto w=midiWrite_.load(std::memory_order_relaxed);
    if(w-midiRead_.load(std::memory_order_acquire)>=midiQueue_.size()) { midiPanic_.store(true); return; }
    midiQueue_[w%midiQueue_.size()]={track,status,a,b}; midiWrite_.store(w+1,std::memory_order_release);
}
void AudioEngine::bypassPlugin(int t,int s,bool value) { if(t>=0 && t<Tracks && s>=0 && s<4) { bypass_[t][s].store(value); midiPanic_.store(true); } }
bool AudioEngine::pluginBypassed(int t,int s) const { return t>=0 && t<Tracks && s>=0 && s<4 && bypass_[t][s].load(); }
unsigned AudioEngine::pluginLatency(int t,int s) const { if(!pluginHealthy(t,s) || pluginBypassed(t,s)) return 0; return (s?effectsPlugins_[t][s-1]:plugins_[t])->latency(); }
unsigned AudioEngine::compensationFrames() const {
    unsigned longest=0,master=0;
    for(int t=0;t<Tracks;++t) { uint64_t total=0; for(int s=0;s<4;++s) total+=pluginLatency(t,s); if(total>MaxPathLatency) throw std::runtime_error("Plugin chain latency exceeds two seconds"); if(t==5) master=unsigned(total); else longest=std::max(longest,unsigned(total)); }
    if(longest+master>MaxPathLatency) throw std::runtime_error("Total plugin latency exceeds two seconds"); return longest+master;
}
void AudioEngine::dispatchMidi(int t,int status,int a,int b,int offset) {
    int c=status&15,kind=status&0xf0;
    for(int s=0;s<4;++s) if(hasPlugin(t,s) && !pluginBypassed(t,s)) (s?effectsPlugins_[t][s-1]:plugins_[t])->midi(status,a,b,offset);
    if(kind==0x90 && b) { keys_[t][c][a]=true; sustained_[t][c][a]=false; }
    else if(kind==0x80 || kind==0x90) { keys_[t][c][a]=false; sustained_[t][c][a]=sustain_[t][c]; }
    else if(kind==0xe0) bend_[t][c]=((a|(b<<7))-8192)/8192.*2.;
    else if(kind==0xb0) {
        if(a==64) { sustain_[t][c]=b>=64; if(b<64) sustained_[t][c].fill(false); }
        if(a==120 || a==123) { keys_[t][c].fill(false); sustained_[t][c].fill(false); }
        if(a==121) { sustain_[t][c]=false; sustained_[t][c].fill(false); bend_[t][c]=0; }
    }
    int first=(kind==0x90 || kind==0x80)?a:0,last=(kind==0x90 || kind==0x80)?a+1:128;
    if(kind!=0x90 && kind!=0x80 && !(kind==0xb0 && (a==64 || a==120 || a==121 || a==123))) return;
    for(int p=first;p<last;++p) { held_[t][p]=false; for(int ch=0;ch<16;++ch) held_[t][p]|=keys_[t][ch][p]||sustained_[t][ch][p]; }
}
void AudioEngine::panicAll() {
    routedCount_=0;
    for(int t=0;t<Tracks;++t) { for(int s=0;s<4;++s) if(hasPlugin(t,s)) (s?effectsPlugins_[t][s-1]:plugins_[t])->allOff(); held_[t].fill(false); for(int c=0;c<16;++c) { keys_[t][c].fill(false); sustained_[t][c].fill(false); sustain_[t][c]=false; } }
}
std::array<int,6> AudioEngine::pluginRouting(int t,int s) const { return hasPlugin(t,s)?(s?effectsPlugins_[t][s-1]:plugins_[t])->routing():std::array<int,6>{}; }
void AudioEngine::pluginRouting(int t,int s,const std::array<int,6>& value) { if(hasPlugin(t,s)) (s?effectsPlugins_[t][s-1]:plugins_[t])->routing(value); }
bool AudioEngine::pluginBindingRequest(int t,int s,int& automation,unsigned& id,double& value,std::string& name) { return hasPlugin(t,s) && (s?effectsPlugins_[t][s-1]:plugins_[t])->bindingRequest(automation,id,value,name); }
bool AudioEngine::updateMidiRouting() {
    constexpr int Nodes=Tracks*4; std::array<std::array<int,6>,Nodes> routes{}; std::array<bool,Nodes> active{};
    for(int node=0;node<Nodes;++node) { int t=node/4,s=node%4; active[node]=pluginHealthy(t,s) && !pluginBypassed(t,s); if(active[node]) routes[node]=pluginRouting(t,s); }
    if(routingSeen_ && routes!=routingValues_) panicAll(); routingValues_=routes; routingSeen_=true;
    auto connected=[&](int a,int b) { return a!=b && active[a] && active[b] && routes[a][3] && routes[b][0] && routes[a][4]==routes[b][1] && routes[a][5]==routes[b][2]; };
    std::array<int,Nodes> state{};
    auto visit=[&](auto&& self,int node)->bool { if(state[node]==1) return false; if(state[node]==2) return true; state[node]=1; for(int dest=0;dest<Nodes;++dest) if(connected(node,dest) && !self(self,dest)) return false; state[node]=2; return true; };
    bool valid=true; for(int node=0;node<Nodes && valid;++node) if(active[node]) valid=visit(visit,node);
    for(int node=0;node<Nodes;++node) if(active[node]) (node%4?effectsPlugins_[node/4][node%4-1]:plugins_[node/4])->routingStatus(valid?1:-1);
    if(!valid) { if(!routingBlocked_.exchange(true)) panicAll(); routedCount_=0; return false; } routingBlocked_.store(false);
    for(int i=0;i<routedCount_;++i) { auto m=routedMidi_[i]; if(pluginHealthy(m.track,m.slot) && !pluginBypassed(m.track,m.slot) && pluginRouting(m.track,m.slot)[0]) (m.slot?effectsPlugins_[m.track][m.slot-1]:plugins_[m.track])->midi(m.status,m.a,m.b,m.offset); }
    routedCount_=0; return true;
}
void AudioEngine::collectMidiOutput(int t,int s) {
    auto& plugin=s?effectsPlugins_[t][s-1]:plugins_[t]; if(!plugin) return; auto output=plugin->routing(); if(!output[3]) return;
    for(int destTrack=0;destTrack<Tracks;++destTrack) for(int destSlot=0;destSlot<4;++destSlot) {
        if((destTrack==t && destSlot==s) || !pluginHealthy(destTrack,destSlot) || pluginBypassed(destTrack,destSlot)) continue; auto input=pluginRouting(destTrack,destSlot); if(!input[0] || input[1]!=output[4] || input[2]!=output[5]) continue;
        for(int i=0;i<plugin->midiOutputCount();++i) { auto m=plugin->midiOutput()[i]; if(routedCount_>=int(routedMidi_.size())) { midiPanic_.store(true); return; } routedMidi_[routedCount_++]={destTrack,destSlot,(m.status&0xf0)|output[5],m.data1,m.data2,m.offset}; }
    }
}
std::vector<unsigned char> AudioEngine::pluginState(int t,int slot) { return hasPlugin(t,slot)?(slot?effectsPlugins_[t][slot-1]:plugins_[t])->state():std::vector<unsigned char>{}; }
bool AudioEngine::restorePluginState(int t,const std::vector<unsigned char>& bytes,int slot) { return hasPlugin(t,slot) && (slot?effectsPlugins_[t][slot-1]:plugins_[t])->state(bytes); }
void AudioEngine::render(float* output,unsigned frames) {
    int slot;
    do { slot=published_.load(); reading_.store(slot); } while(slot!=published_.load());
    const Scene& scene=scenes_[slot];
    const bool play=playing_.load(); const auto seek=seekVersion_.load(std::memory_order_acquire);
    bool chaseControllers=seek!=appliedSeek_;
    if(seek!=appliedSeek_ || (wasPlaying_ && !play)) {
        beat_=seek_.load(); appliedSeek_=seek;
        compensation_->reset();
        panicAll();
    }
    wasPlaying_=play;
    auto r=midiRead_.load(); auto w=midiWrite_.load(std::memory_order_acquire);
    while(r!=w) { auto m=midiQueue_[r++%midiQueue_.size()]; dispatchMidi(m.track,m.status,m.data1,m.data2);
    } midiRead_.store(r,std::memory_order_release);
    if(midiPanic_.exchange(false)) panicAll();
    bool solo=false; for(int t=0;t<Tracks;++t) if(t!=5) solo|=scene.channels[t].solo;
    const double step=std::clamp(scene.bpm,20.,300.)/(60.*48000.);
    unsigned done=0;
    while(done<frames) {
        int n=std::min<unsigned>(Block,frames-done); bool blockPlay=play && beat_<scene.end;
        double end=scene.loop?scene.loopEnd:scene.end;
        if(blockPlay) n=std::min(n,std::max(1,int(std::ceil((end-beat_)/step))));
        std::array<float,Block> sumL{},sumR{};
        bool midiRouting=updateMidiRouting();
        std::array<unsigned,Tracks> pathLatency{}; unsigned longest=0;
        for(int t=0;t<Tracks;++t) { uint64_t total=0; for(int s=0;s<4;++s) { unsigned latency=pluginLatency(t,s); if(total+latency>MaxPathLatency) { if(exporting_) throw std::runtime_error("Plugin chain latency exceeds two seconds"); bypass_[t][s].store(true); } else total+=latency; } pathLatency[t]=unsigned(total); if(t!=5) longest=std::max(longest,pathLatency[t]); }
        unsigned totalLatency=longest+pathLatency[5]; if(totalLatency>MaxPathLatency) { if(exporting_) throw std::runtime_error("Total plugin latency exceeds two seconds"); for(int s=0;s<4;++s) bypass_[5][s].store(true); pathLatency[5]=0; totalLatency=longest; }
        if(compensation_->previous!=pathLatency || compensation_->previousTotal!=totalLatency) { compensation_->reset(); compensation_->previous=pathLatency; compensation_->previousTotal=totalLatency; }
        for(int ai=0;ai<scene.parameterCount;++ai) { const auto& curve=scene.parameters[ai]; if(!curve.count || !pluginHealthy(curve.track,curve.slot) || pluginBypassed(curve.track,curve.slot)) continue;
            auto& p=curve.slot?effectsPlugins_[curve.track][curve.slot-1]:plugins_[curve.track];
            unsigned preceding=curve.track==5?longest:0; for(int s=0;s<curve.slot;++s) preceding+=pluginLatency(curve.track,s);
            for(int i=0;i<n;i+=8) { double when=beat_+(i-double(preceding))*step; auto a=curve.points[0],b=a; for(int point=1;point<curve.count;++point) { b=curve.points[point]; if(b.beat>=when) break; a=b; }
                double db=b.beat>a.beat?a.db+(b.db-a.db)*std::clamp((when-a.beat)/(b.beat-a.beat),0.,1.):a.db; p->parameter(curve.id,std::clamp((db+48)/54,0.,1.),i);
            }
        }
        for(int t=0;t<Tracks;++t) {
            if(t==5) continue;
            std::array<float,Block> left{},right{};
            struct Scheduled { int status,a,b,offset; };
            std::array<Scheduled,2048> schedule{}; int scheduleCount=0;
            if(chaseControllers && blockPlay) {
                struct Chase { double when=-1; int status=0,a=0,b=0; };
                std::array<std::array<Chase,130>,16> latest{};
                for(int ci=0;ci<scene.count;++ci) { const auto& clip=scene.clips[ci]; if(clip.track!=t || clip.audio || beat_<clip.beat || beat_>=clip.beat+clip.length) continue;
                    double pattern=std::max(.25,clip.pattern);
                    for(int mi=0;mi<clip.controlCount;++mi) { auto m=clip.controls[mi]; int kind=m.status&0xf0; int number=kind==0xb0?m.data1:kind==0xe0?129:kind==0xd0?128:-1; if(number<0 || (kind==0xb0 && m.data1>=120)) continue;
                        int repeat=int(std::floor((beat_-clip.beat-m.beat)/pattern)); if(repeat<0) continue; double when=clip.beat+repeat*pattern+m.beat; auto& entry=latest[m.status&15][number]; if(when>entry.when && when<beat_-1e-10) entry={when,m.status,m.data1,m.data2};
                    }
                }
                for(auto& channel:latest) for(auto& cc:channel) if(cc.when>=0) dispatchMidi(t,cc.status,cc.a,cc.b);
            }
            if(blockPlay) for(int ci=0;ci<scene.count;++ci) {
                const auto& clip=scene.clips[ci]; if(clip.track!=t) continue;
                for(int i=0;i<n;++i) {
                    double local=beat_+i*step-clip.beat;
                    if(local<0 || local>=clip.length) continue;
                    if(clip.audio) {
                        double frame=(local*60./scene.bpm+clip.offset)*48000;
                        if(std::isfinite(frame) && frame>=0 && frame<clip.audio->samples.size()/2) { auto index=size_t(frame)*2; left[i]+=clip.audio->samples[index]; right[i]+=clip.audio->samples[index+1]; }
                    }
                }
                if(!clip.audio) for(int ni=0;ni<clip.noteCount;++ni) {
                    auto note=clip.notes[ni];
                    double pattern=std::max(.25,clip.pattern);
                    int first=std::max(0,int(std::floor((beat_-clip.beat-note.beat-note.duration)/pattern)));
                    int last=std::max(first,int(std::floor((beat_+n*step-clip.beat)/pattern)));
                    for(int repeat=first;repeat<=last;++repeat) {
                        double on=clip.beat+repeat*pattern+note.beat;
                        double off=std::min(on+note.duration,clip.beat+clip.length);
                        if(on>=clip.beat+clip.length) continue;
                        for(int kind=0;kind<2;++kind) {
                            double when=kind?off:on;
                            if(when>=beat_-1e-10 && when<beat_+n*step-1e-10) {
                                int offset=std::clamp(int(std::round((when-beat_)/step)),0,n-1);
                                if(scheduleCount<int(schedule.size())) schedule[scheduleCount++]={(kind?0x80:0x90)|(note.channel&15),note.pitch,note.velocity,offset};
                            }
                        }
                    }
                }
                if(!clip.audio) for(int ci=0;ci<clip.controlCount;++ci) {
                    auto control=clip.controls[ci]; double pattern=std::max(.25,clip.pattern);
                    int first=std::max(0,int(std::floor((beat_-clip.beat-control.beat)/pattern)));
                    for(int repeat=first;repeat<=first+1;++repeat) { double when=clip.beat+repeat*pattern+control.beat;
                        if(when<clip.beat+clip.length && when>=beat_-1e-10 && when<beat_+n*step-1e-10 && scheduleCount<int(schedule.size())) schedule[scheduleCount++]={control.status,control.data1,control.data2,std::clamp(int(std::round((when-beat_)/step)),0,n-1)};
                    }
                }
            }
            std::stable_sort(schedule.begin(),schedule.begin()+scheduleCount,[](const Scheduled& a,const Scheduled& b) { return a.offset<b.offset; });
            if(plugins_[t]) for(int e=0;e<scheduleCount;++e) dispatchMidi(t,schedule[e].status,schedule[e].a,schedule[e].b,schedule[e].offset);
            if(plugins_[t] && !pluginFault_[t].load() && !pluginBypassed(t,0)) {
                try { plugins_[t]->process(left.data(),right.data(),n,beat_,scene.bpm,blockPlay); }
                catch(...) { pluginFault_[t].store(true); if(exporting_) throw; }
                if(midiRouting) collectMidiOutput(t,0);
            }
            else if(!plugins_[t]) {
                int event=0;
                bool sound=false; for(bool held:held_[t]) sound|=held;
                for(int i=0;i<n;++i) {
                    bool changed=false; while(event<scheduleCount && schedule[event].offset<=i) { auto e=schedule[event++]; dispatchMidi(t,e.status,e.a,e.b,e.offset); changed=true; }
                    if(changed) { sound=false; for(bool held:held_[t]) sound|=held; } if(!sound) continue;
                    for(int pitch=0;pitch<128;++pitch) if(held_[t][pitch]) {
                        int c=0; while(c<15 && !keys_[t][c][pitch] && !sustained_[t][c][pitch]) ++c;
                        double freq=effects_->frequencies[pitch]*std::pow(2.,bend_[t][c]/12); float v=float(std::sin(phases_[t][pitch])*.025);
                        phases_[t][pitch]=std::fmod(phases_[t][pitch]+freq*6.283185307/48000,6.283185307); left[i]+=v; right[i]+=v;
                    }
                }
            }
            auto channel=scene.channels[t]; bool audible=!channel.mute && (!solo || channel.solo);
            for(int s=0;s<3;++s) if(auto& p=effectsPlugins_[t][s]; p && p->alive() && !pluginBypassed(t,s+1)) { try { p->process(left.data(),right.data(),n,beat_,scene.bpm,blockPlay); if(midiRouting) collectMidiOutput(t,s+1); } catch(...) { if(exporting_) throw; } }
            auto& fx=effects_->tracks[t];
            for(int i=0;i<n;++i) {
                float values[2]={left[i],right[i]};
                if(channel.effects[0]) for(int side=0;side<2;++side) { float hp=.984414f*(fx.highpass[side]+values[side]-fx.prev[side]); fx.prev[side]=values[side]; fx.highpass[side]=hp; values[side]=hp; }
                if(channel.effects[1]) { float level=std::max(std::abs(values[0]),std::abs(values[1])); float coeff=level>fx.envelope?.02f:.0002f; fx.envelope+=(level-fx.envelope)*coeff; if(fx.envelope>.25f) { float gain=std::pow(.25f/fx.envelope,.75f); values[0]*=gain; values[1]*=gain; } }
                if(channel.effects[2]) { unsigned pos=fx.position++%8192; float a=fx.delay[0][pos],b=fx.delay[1][pos]; fx.delay[0][pos]=values[0]+b*.55f; fx.delay[1][pos]=values[1]+a*.55f; values[0]=values[0]*.82f+a*.18f; values[1]=values[1]*.82f+b*.18f; }
                left[i]=values[0]; right[i]=values[1];
            }
            float lg=audible?channel.gain*std::sqrt(1-std::max(0.f,channel.pan)):0;
            float rg=audible?channel.gain*std::sqrt(1+std::min(0.f,channel.pan)):0;
            float peak=0;
            for(int i=0;i<n;++i) {
                float automation=1;
                if(channel.automationCount) {
                    double when=beat_+(i-double(pathLatency[t]))*step; auto a=channel.automation[0],b=a;
                    for(int p=1;p<channel.automationCount;++p) { b=channel.automation[p]; if(b.beat>=when) break; a=b; }
                    float db=b.beat>a.beat?a.db+(b.db-a.db)*float(std::clamp((when-a.beat)/(b.beat-a.beat),0.,1.)):a.db;
                    automation=std::pow(10.f,db/20.f);
                }
                float l=std::isfinite(left[i])?left[i]*lg*automation:0,r=std::isfinite(right[i])?right[i]*rg*automation:0;
                l=compensation_->delays[t][0].push(l,longest-pathLatency[t]); r=compensation_->delays[t][1].push(r,longest-pathLatency[t]);
                sumL[i]+=l; sumR[i]+=r; peak=std::max({peak,std::abs(l),std::abs(r)});
            }
            peaks_[t].store(peak);
        }
        for(int s=0;s<4;++s) if(hasPlugin(5,s) && pluginHealthy(5,s) && !pluginBypassed(5,s)) { auto& p=s?effectsPlugins_[5][s-1]:plugins_[5]; try { p->process(sumL.data(),sumR.data(),n,beat_,scene.bpm,blockPlay); if(midiRouting) collectMidiOutput(5,s); } catch(...) { if(!s) pluginFault_[5].store(true); if(exporting_) throw; } }
        float master=scene.channels[5].mute?0:scene.channels[5].gain,peak=0;
        for(int i=0;i<n;++i) {
            float automation=1; const auto& channel=scene.channels[5];
            if(channel.automationCount) { double when=beat_+(i-double(totalLatency))*step; auto a=channel.automation[0],b=a; for(int p=1;p<channel.automationCount;++p) { b=channel.automation[p]; if(b.beat>=when) break; a=b; } double db=b.beat>a.beat?a.db+(b.db-a.db)*std::clamp((when-a.beat)/(b.beat-a.beat),0.,1.):a.db; automation=float(std::pow(10.,db/20)); }
            float l=std::clamp(sumL[i]*master*automation,-1.f,1.f),r=std::clamp(sumR[i]*master*automation,-1.f,1.f);
            output[(done+i)*2]=l; output[(done+i)*2+1]=r; peak=std::max({peak,std::abs(l),std::abs(r)});
            if(done+i<Block) { waveform_[0][done+i].store(l,std::memory_order_relaxed); waveform_[1][done+i].store(r,std::memory_order_relaxed); }
            auto audibleBeat=compensation_->beats.push(blockPlay?beat_+i*step:beat_,totalLatency);
            if(done+i<Block) renderedBeats_[done+i]=audibleBeat;
        } peaks_[5].store(peak); done+=n;
        chaseControllers=false;
        if(blockPlay) {
            beat_+=n*step;
            if(beat_>=end-1e-9) {
                panicAll();
                if(scene.loop) { beat_=scene.loopStart; chaseControllers=true; } else beat_=scene.end;
            }
        }
    }
    position_.store(beat_); reading_.store(-1);
}
bool AudioEngine::exportWav(const std::filesystem::path& path,double beats,std::string& error) {
    const auto& current=scenes_[published_.load()];
    double frames=std::ceil(beats*60/current.bpm*48000);
    if(!std::isfinite(frames) || frames<=0 || frames>48000ull*60*60 || frames*8>0xffffffffull-36) { error="Invalid export duration (maximum one hour)"; return false; }
    auto count=uint64_t(frames);
    auto oldBeat=position(); bool oldPlay=playing_.load();
    bool started=device_->started; stop(); auto old=std::make_unique<Scene>(scenes_[published_.load()]);
    auto offline=std::make_unique<Scene>(*old); offline->loop=false; publish(*offline);
    transport(true,0);
    std::array<std::vector<unsigned char>,Tracks> states;
    std::array<std::array<std::vector<unsigned char>,3>,Tracks> effectStates;
    try {
        for(int t=0;t<Tracks;++t) { if(plugins_[t]) { states[t]=plugins_[t]->state(); plugins_[t]->synchronous(true); plugins_[t]->allOff(); }
            for(int s=0;s<3;++s) if(auto& p=effectsPlugins_[t][s]) { effectStates[t][s]=p->state(); p->synchronous(true); }
        }
    } catch(const std::exception& e) { error=e.what(); for(auto& p:plugins_) if(p) p->synchronous(true,20); for(auto& chain:effectsPlugins_) for(auto& p:chain) if(p) p->synchronous(true,20); publish(*old); transport(oldPlay,oldBeat); if(started) { std::string ignored; start(ignored); } return false; }
    auto temporary=path; temporary+=L".tmp";
    std::ofstream out(temporary,std::ios::binary); uint32_t data=uint32_t(count*8),size=data+36,rate=48000,byteRate=384000,fmt=16; uint16_t code=3,channels=2,align=8,bits=32;
    out.write("RIFF",4); out.write(reinterpret_cast<char*>(&size),4); out.write("WAVEfmt ",8); out.write(reinterpret_cast<char*>(&fmt),4); out.write(reinterpret_cast<char*>(&code),2); out.write(reinterpret_cast<char*>(&channels),2); out.write(reinterpret_cast<char*>(&rate),4); out.write(reinterpret_cast<char*>(&byteRate),4); out.write(reinterpret_cast<char*>(&align),2); out.write(reinterpret_cast<char*>(&bits),2); out.write("data",4); out.write(reinterpret_cast<char*>(&data),4);
    bool ok=false;
    exporting_=true;
    try { unsigned latency=compensationFrames(); std::array<float,Block*2> buffer{};
        for(uint64_t n=0;n<count+latency;n+=Block) { auto frames=unsigned(std::min<uint64_t>(Block,count+latency-n)); render(buffer.data(),frames);
            if(compensationFrames()!=latency) throw std::runtime_error("Plugin latency changed during export; retry with stable settings");
            unsigned skip=n<latency?unsigned(std::min<uint64_t>(frames,latency-n)):0;
            if(frames>skip) out.write(reinterpret_cast<char*>(buffer.data()+skip*2),(frames-skip)*8);
        }
        out.flush(); ok=bool(out); out.close();
        if(ok) ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
        if(!ok) error="Export write failed; previous file preserved";
    } catch(const std::exception& e) { error=e.what(); out.close(); }
    exporting_=false;
    for(int t=0;t<Tracks;++t) if(plugins_[t]) { plugins_[t]->synchronous(true,20); plugins_[t]->allOff(); try { if(!states[t].empty() && !plugins_[t]->state(states[t])) { ok=false; error="Plugin state restoration failed after export"; } } catch(const std::exception& e) { ok=false; error=e.what(); } }
    for(int t=0;t<Tracks;++t) for(int s=0;s<3;++s) if(auto& p=effectsPlugins_[t][s]) { p->synchronous(true,20); try { if(!effectStates[t][s].empty() && !p->state(effectStates[t][s])) { ok=false; error="Effect state restoration failed after export"; } } catch(const std::exception& e) { ok=false; error=e.what(); } }
    publish(*old); transport(oldPlay,oldBeat); if(started) { std::string ignored; start(ignored); }
    if(!ok) { std::error_code ec; std::filesystem::remove(temporary,ec); } return ok;
}
}
