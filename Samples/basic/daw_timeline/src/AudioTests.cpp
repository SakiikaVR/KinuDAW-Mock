// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "AudioEngine.h"
#include "PluginIPC.h"
#include "DelayCompensation.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
#include <thread>
#include <stdexcept>
static void require(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
int main(int argc,char** argv) {
    try {
        auto engine=std::make_unique<Kinu::AudioEngine>(); auto scene=std::make_unique<Kinu::Scene>();
        {
            auto delay=std::make_unique<Kinu::SampleDelay<float>>();
            for(int i=0;i<1000;++i) require(delay->push(i==0?1.f:0.f,257)==(i==257?1.f:0.f),"Delay line impulse mismatch");
            delay->reset(); require(delay->push(1.f,0)==1.f,"Zero delay failed");
            Kinu::PluginInfo fixture{KINU_TEST_PLUGIN_PATH,"12405080112233445566778899AABBCC","Kinu Delay Test","Fx","KinuDAW tests"};
            Kinu::RemotePlugin plugin(fixture); plugin.synchronous(true);
            require(plugin.latency()==257,"VST declared latency missing from IPC");
            float left[Kinu::Block],right[Kinu::Block];
            for(int b=0;b<4;++b) { std::fill_n(left,Kinu::Block,1.f); std::fill_n(right,Kinu::Block,1.f); plugin.process(left,right,Kinu::Block,0,120,false); }
            plugin.midi(0xbf,7,64,32); std::fill_n(left,Kinu::Block,1.f); std::fill_n(right,Kinu::Block,1.f); plugin.process(left,right,Kinu::Block,0,120,false);
            require(std::abs(left[31]-1.f)<1e-6 && std::abs(left[32]-64/127.f)<1e-6,"Channel 16 sample-offset MIDI CC mapping failed");
            plugin.midi(0xef,0,64,64); std::fill_n(left,Kinu::Block,1.f); std::fill_n(right,Kinu::Block,1.f); plugin.process(left,right,Kinu::Block,0,120,false);
            require(std::abs(left[63]-64/127.f)<1e-6 && std::abs(left[64]-8192/16383.f)<1e-6,"Pitch bend normalization failed");
            plugin.midi(0xdf,100,0,16); std::fill_n(left,Kinu::Block,1.f); std::fill_n(right,Kinu::Block,1.f); plugin.process(left,right,Kinu::Block,0,120,false); require(std::abs(left[16]-100/127.f)<1e-6,"Channel pressure failed");
            plugin.midi(0xb0,20,127); for(int i=0;i<100 && plugin.latency()!=513;++i) { std::fill_n(left,Kinu::Block,0.f); std::fill_n(right,Kinu::Block,0.f); plugin.process(left,right,Kinu::Block,0,120,false); Sleep(2); } require(plugin.latency()==513,"kLatencyChanged reactivation and IPC update failed");
            plugin.note(65,90,true,42,6); plugin.parameter(1,.25,32); std::fill_n(left,Kinu::Block,1.f); std::fill_n(right,Kinu::Block,1.f); plugin.process(left,right,Kinu::Block,0,120,false);
            require(plugin.midiOutputCount()==1 && plugin.midiOutput()[0].status==0x96 && plugin.midiOutput()[0].data1==65 && plugin.midiOutput()[0].offset==42,"Plugin MIDI output bridge failed");
            plugin.allOff(); plugin.note(67,100,true); plugin.process(left,right,Kinu::Block,0,120,false);
            bool firstNote=false; for(int i=0;i<plugin.midiOutputCount();++i) firstNote|=plugin.midiOutput()[i].status==0x90 && plugin.midiOutput()[i].data1==67;
            require(firstNote,"All Notes Off exhausted the event queue and dropped the first note");
            auto pdc=std::make_unique<Kinu::AudioEngine>(); auto graph=std::make_unique<Kinu::Scene>(); Kinu::AudioFile impulse; impulse.samples.assign(48000*2,0); impulse.samples[0]=impulse.samples[1]=.1f;
            graph->bpm=120; graph->count=2;
            for(int t=0;t<2;++t) { graph->clips[t].track=t; graph->clips[t].audio=&impulse; graph->clips[t].length=2; }
            std::string error; require(pdc->loadPlugin(0,fixture,error,1),error.c_str()); require(pdc->loadPlugin(0,fixture,error,2),error.c_str()); require(pdc->loadPlugin(5,fixture,error,1),error.c_str());
            require(pdc->compensationFrames()==771,"Serial and master latency sum failed"); pdc->publish(*graph); pdc->transport(true,0);
            std::array<float,2048> mixed{}; pdc->render(mixed.data(),1024);
            for(int i=0;i<1024;++i) require(std::abs(mixed[i*2]-(i==771?.2f:0.f))<1e-6,"PDC failed to align dry, serial FX and master impulse");
            graph->channels[5].automationCount=2; graph->channels[5].automation[0]={0,0}; graph->channels[5].automation[1]={.01,-48};
            pdc->publish(*graph); pdc->transport(true,0); pdc->render(mixed.data(),1024);
            require(std::abs(mixed[771*2]-.2f)<1e-6,"Master volume automation did not follow compensated audio clock");
            graph->channels[5].automationCount=0; pdc->publish(*graph);
            auto wav=std::filesystem::temp_directory_path()/"kinu-pdc-test.wav"; require(pdc->exportWav(wav,2,error),error.c_str()); auto* rendered=pdc->import(wav,error); require(rendered && std::abs(rendered->samples[0]-.2f)<1e-6 && rendered->samples.size()==96000,"PDC export trim or duration failed"); std::filesystem::remove(wav);
            pdc->bypassPlugin(0,2,true); require(pdc->compensationFrames()==514,"Bypass did not remove latency"); require(pdc->unloadPlugin(5,error,1),error.c_str()); require(pdc->compensationFrames()==257,"Removal did not rebuild latency");
            graph->count=1; impulse.samples.assign(48000*2,.1f); graph->parameterCount=1; graph->parameters[0].track=0; graph->parameters[0].slot=1; graph->parameters[0].id=1; graph->parameters[0].count=2; graph->parameters[0].points[0]={0,-34.5f}; graph->parameters[0].points[1]={128,-34.5f};
            pdc->publish(*graph); pdc->transport(true,0); pdc->render(mixed.data(),1024); require(std::abs(mixed[800*2]-.025f)<1e-6,"Plugin parameter automation did not affect DSP");
            require(pdc->loadPlugin(1,fixture,error,1),error.c_str()); pdc->pluginRouting(0,1,{1,0,0,1,0,0}); pdc->pluginRouting(1,1,{1,0,0,1,0,0}); pdc->render(mixed.data(),128); require(pdc->midiRoutingBlocked(),"Cyclic MIDI routing was not blocked");
            pdc->pluginRouting(1,1,{1,0,0,0,0,0}); pdc->render(mixed.data(),128); require(!pdc->midiRoutingBlocked(),"Valid MIDI connection remained blocked");
            auto synth=std::make_unique<Kinu::AudioEngine>(); std::array<float,256> sound{};
            synth->midiMessage(0,0x93,60,100); synth->render(sound.data(),128);
            synth->midiMessage(0,0xb3,64,127); synth->midiMessage(0,0x83,60,0); synth->render(sound.data(),128); double energy=0; for(float s:sound) energy+=s*s; require(energy>0,"Sustain pedal did not hold note");
            synth->midiMessage(0,0xb3,64,0); synth->render(sound.data(),128); for(float s:sound) require(s==0,"Sustain pedal release left stuck notes");
            std::cout<<"PDC serial/master impulse and dynamic latency, export trim, bypass/removal, MIDI CC/channel 16, bend, pressure, sustain, output events, parameter automation and routing cycle rejection passed\n";
        }
        if(argc>=2 && std::string(argv[1])=="--capture") {
            std::string error; require(engine->start(error),error.c_str());
            auto path=std::filesystem::temp_directory_path()/"kinu-capture-test.wav";
            require(engine->beginRecording(path,error),error.c_str()); Sleep(1000);
            require(engine->endRecording(error)==path && error.empty(),error.c_str());
            auto* file=engine->import(path,error); require(file && file->samples.size()>48000,"WASAPI capture file empty");
            for(auto sample:file->samples) require(std::isfinite(sample),"Non-finite captured sample");
            std::cout<<"WASAPI capture finalized and decoded: "<<file->samples.size()/2<<" frames\n"; engine->stop(); std::filesystem::remove(path); return 0;
        }
        if(argc>=2 && std::string(argv[1]).rfind("--device",0)==0) {
            std::string error; if(std::string(argv[1])=="--device-exclusive") engine->configure(128,true,error); require(engine->start(error),error.c_str()); Sleep(500);
            std::cout<<"WASAPI / 48000 Hz processing / "<<engine->deviceName()<<" / native "<<engine->nativeSampleRate()<<" Hz / actual period "<<engine->bufferFrames()<<" frames / "<<engine->bufferFrames()*1000./engine->nativeSampleRate()<<" ms\n"; engine->stop(); return 0;
        }
        Kinu::AudioFile file; file.samples.assign(48000*2,.25f);
        scene->bpm=120; scene->count=1; auto& clip=scene->clips[0]; clip.audio=&file; clip.track=0; clip.beat=0; clip.length=2;
        engine->publish(*scene); engine->transport(true,0); float output[512]{}; engine->render(output,256);
        require(std::abs(output[0]-.25f)<1e-6,"Audio playback mismatch");
        require(engine->position()>0,"Audio clock did not advance");
        scene->channels[0].mute=true; engine->publish(*scene); engine->render(output,256); require(output[0]==0,"Mute failed");
        scene->channels[0].mute=false; scene->channels[0].gain=.5f; scene->channels[0].pan=1;
        engine->publish(*scene); engine->render(output,256); require(output[0]==0 && std::abs(output[1]-.125f)<1e-6,"Gain/pan failed");
        scene->channels[0].gain=1; scene->channels[0].pan=0; scene->loop=true; scene->loopEnd=.02;
        engine->publish(*scene); engine->transport(true,0); for(int i=0;i<16;++i) engine->render(output,256);
        require(engine->position()<.02,"Loop clock failed");
        engine->transport(false,0); engine->render(output,256); require(output[0]==0,"Stop failed");
        scene->loop=false; engine->publish(*scene); std::string error;
        auto path=std::filesystem::temp_directory_path()/"kinu-audio-test.wav";
        require(engine->exportWav(path,2,error),error.c_str());
        require(std::filesystem::file_size(path)==44+48000*8,"WAV export length mismatch");
        auto* imported=engine->import(path,error); require(imported && imported->samples.size()==96000,"WAV round trip failed");
        std::filesystem::remove(path);
        // Concurrent publication exercises the slot pinning protocol under load.
        std::atomic<bool> running{true}; std::thread reader([&] { float out[512]; while(running.load()) engine->render(out,256); });
        for(int i=0;i<10000;++i) { scene->channels[0].gain=(i%100)/100.f; engine->publish(*scene); }
        running.store(false); reader.join();
        if(argc>=2) {
            std::ifstream in(argv[1]); nlohmann::json j; in>>j;
            Kinu::PluginInfo info{j.at("path"),j.at("uid"),j.at("name"),j.value("category",""),j.value("vendor","")};
            Kinu::RemotePlugin remote(info); float left[256]{},right[256]{}; remote.note(60,100,true);
            double energy=0;
            for(int b=0;b<200;++b) { std::fill_n(left,256,0.f); std::fill_n(right,256,0.f); remote.process(left,right,Kinu::Block,b*.01,120,true); for(auto v:left) energy+=v*v; Sleep(3); }
            require(energy>0,"Remote instrument produced no audio");
            auto bytes=remote.state(); require(!bytes.empty() && remote.state(bytes),"Remote state round trip failed");
            HANDLE worker=OpenProcess(PROCESS_TERMINATE,FALSE,remote.workerId());
            require(worker && TerminateProcess(worker,99),"Could not inject worker crash"); CloseHandle(worker); Sleep(20);
            auto began=GetTickCount64(); for(int i=0;i<200;++i) remote.process(left,right,Kinu::Block,0,120,true);
            require(GetTickCount64()-began<100,"Worker crash blocked parent audio");
            engine->render(output,256);
            require(engine->loadPlugin(0,info,error),error.c_str());
            clip.audio=nullptr; clip.noteCount=1; clip.notes[0]={60,100,0,1}; scene->channels[0].gain=1; scene->loop=false;
            engine->publish(*scene);
            require(engine->exportWav(path,2,error),error.c_str());
            auto exported=std::make_unique<Kinu::AudioEngine>(); auto* wave=exported->import(path,error); require(wave,"VST WAV import failed");
            double exportEnergy=0; for(auto v:wave->samples) exportEnergy+=v*v;
            require(exportEnergy>1,"VST offline export silent"); std::filesystem::remove(path);
            // Exercise the actual callback/renderer queue with a live instrument.
            scene->loop=true; scene->loopEnd=2; engine->publish(*scene); engine->transport(true,0);
            require(engine->start(error),error.c_str()); Sleep(1000); auto initialXruns=engine->underruns(); Sleep(30000);
            require(engine->pluginHealthy(0),"Live VST worker failed");
            auto additionalXruns=engine->underruns()-initialXruns; engine->stop();
            std::cout<<"Live WASAPI + VST: "<<additionalXruns<<" underruns in 30 seconds (startup "<<initialXruns<<")\n";
            require(additionalXruns==0,"Live audio queue underrun");
            std::cout<<"Remote audio/state passed, energy="<<energy<<"\n";
        }
        std::cout<<"Audio playback, clock, gain, pan, mute, loop, stop, WAV and concurrent publication passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
