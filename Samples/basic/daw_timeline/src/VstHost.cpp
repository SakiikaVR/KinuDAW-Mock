// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "VstHost.h"
#include "PluginEditor.h"
#include "PluginIPC.h"
#include "public.sdk/source/common/memorystream.h"
#include "pluginterfaces/base/funknownimpl.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Kinu {
std::vector<std::string> modulePaths() { return VST3::Hosting::Module::getModulePaths(); }
std::vector<PluginInfo> scanModule(const std::string& path, std::string& error) {
    auto module = VST3::Hosting::Module::create(path, error);
    std::vector<PluginInfo> infos;
    if (module) for (const auto& c : module->getFactory().classInfos())
        if (c.category() == kVstAudioEffectClass)
            infos.push_back({path,c.ID().toString(),c.name(),c.subCategoriesString(),c.vendor()});
    return infos;
}
Plugin::Plugin(const PluginInfo& metadata) : info(metadata) {
    std::string error;
    module_ = VST3::Hosting::Module::create(info.path,error);
    if (!module_) throw std::runtime_error(error);
    module_->getFactory().setHostContext(&host_);
    PluginContextFactory::instance().setPluginContext(&host_);
    for (const auto& c : module_->getFactory().classInfos()) if (c.ID().toString() == info.uid) {
        provider_ = std::make_unique<PlugProvider>(module_->getFactory(),c,true); break;
    }
    if (!provider_ || !provider_->initialize()) throw std::runtime_error("VST3 initialization failed");
    component_ = provider_->getComponentPtr(); controller_ = provider_->getControllerPtr();
    processor_ = U::cast<IAudioProcessor>(component_);
    if (!processor_ || processor_->canProcessSampleSize(kSample32) != kResultOk)
        throw std::runtime_error("32-bit audio processing unsupported");
    if (controller_) {
        controller_->setComponentHandler(this);
        MemoryStream stream; if (component_->getState(&stream) == kResultOk) {
            stream.seek(0,IBStream::kIBSeekSet,nullptr); controller_->setComponentState(&stream);
        }
        changes_.setMaxParameters(controller_->getParameterCount());
        outputChanges_.setMaxParameters(controller_->getParameterCount());
    }
    std::vector<SpeakerArrangement> ins(component_->getBusCount(kAudio,kInput));
    std::vector<SpeakerArrangement> outs(component_->getBusCount(kAudio,kOutput));
    for (size_t i=0;i<ins.size();++i) processor_->getBusArrangement(kInput,int32(i),ins[i]);
    for (size_t i=0;i<outs.size();++i) processor_->getBusArrangement(kOutput,int32(i),outs[i]);
    if (!ins.empty()) ins[0] = SpeakerArr::kStereo;
    if (!outs.empty()) outs[0] = SpeakerArr::kStereo;
    processor_->setBusArrangements(ins.data(),int32(ins.size()),outs.data(),int32(outs.size()));
    for (auto media : {kAudio,kEvent}) for (auto dir : {kInput,kOutput})
        for (int32 i=0;i<component_->getBusCount(media,dir);++i) {
            BusInfo bus{}; component_->getBusInfo(media,dir,i,bus);
            component_->activateBus(media,dir,i,bus.busType == kMain);
        }
    ProcessSetup setup{kRealtime,kSample32,Block,48000.};
    if (processor_->setupProcessing(setup) != kResultOk || !data_.prepare(*component_,Block,kSample32))
        throw std::runtime_error("Audio bus setup failed");
    data_.processMode = kRealtime; data_.inputEvents = &events_; data_.outputEvents = &outputEvents_;
    data_.inputParameterChanges = &changes_; data_.outputParameterChanges = &outputChanges_;
    auto activation=component_->setActive(true); active_=activation==kResultOk;
    auto processing=active_?processor_->setProcessing(true):kResultFalse;
    processing_=active_ && (processing==kResultOk || processing==kNotImplemented);
    if (!processing_) throw std::runtime_error("VST3 activation failed: setActive="+std::to_string(activation)+", setProcessing="+std::to_string(processing));
    latency_.store(processor_->getLatencySamples()); refreshMidiMapping();
}
Plugin::~Plugin() {
    editor_.reset();
    if (view_) { view_->removed(); view_->setFrame(nullptr); view_.reset(); }
    if (window_) DestroyWindow(window_);
    if (processing_) processor_->setProcessing(false);
    if (active_) component_->setActive(false);
    if (controller_) controller_->setComponentHandler(nullptr);
    data_.unprepare(); processor_.reset(); controller_.reset(); component_.reset(); provider_.reset();
    PluginContextFactory::instance().setPluginContext(nullptr);
}
tresult PLUGIN_API Plugin::queryInterface(const TUID iid, void** obj) {
    if (!obj) return kInvalidArgument; *obj = nullptr;
    if (FUnknownPrivate::iidEqual(iid,IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid,FUnknown::iid))
        *obj = static_cast<IComponentHandler*>(this);
    else if (FUnknownPrivate::iidEqual(iid,IPlugFrame::iid)) *obj = static_cast<IPlugFrame*>(this);
    else return kNoInterface;
    addRef(); return kResultOk;
}
tresult PLUGIN_API Plugin::performEdit(ParamID id, ParamValue value) {
    auto w = editWrite_.load(std::memory_order_relaxed);
    if (w-editRead_.load(std::memory_order_acquire) >= edits_.size()) return kOutOfMemory;
    edits_[w%edits_.size()] = {id,std::clamp(value,0.,1.)};
    editWrite_.store(w+1,std::memory_order_release); return kResultOk;
}
tresult PLUGIN_API Plugin::restartComponent(int32 flags) {
    if(flags & kMidiCCAssignmentChanged) mappingDirty_.store(true);
    restart_.fetch_or(flags); return kResultOk;
}
void Plugin::refreshMidiMapping() {
    auto r=feedbackRead_.load(); auto w=feedbackWrite_.load(std::memory_order_acquire);
    while(r!=w) { const auto edit=midiFeedback_[r++%midiFeedback_.size()]; if(controller_) controller_->setParamNormalized(edit.id,edit.value); } feedbackRead_.store(r,std::memory_order_release);

    if(!mappingDirty_.exchange(false)) return;
    auto mapping=U::cast<IMidiMapping>(controller_);
    for(int channel=0;channel<16;++channel) for(int cc=0;cc<kCountCtrlNumber;++cc) {
        ParamID id=kNoParamId;
        if(mapping && mapping->getMidiControllerAssignment(0,int16(channel),CtrlNumber(cc),id)!=kResultOk) id=kNoParamId;
        midiMapping_[channel][cc].store(id,std::memory_order_release);
    }
}
void Plugin::midi(int status,int a,int b,int offset) {
    int channel=status&15,kind=status&0xf0;
    if(kind==0x90 || kind==0x80) { note(a,b,kind==0x90 && b!=0,offset,channel); return; }
    if(kind==0xa0) { Event e{}; e.busIndex=0; e.sampleOffset=offset; e.flags=Event::kIsLive; e.type=Event::kPolyPressureEvent; e.polyPressure={int16(channel),int16(a),b/127.f,channel*128+a}; events_.addEvent(e); return; }
    if(kind==0xb0 && (a==120 || a==123)) for(int p=0;p<128;++p) if(activeNotes_[channel][p]) note(p,0,false,offset,channel);
    int number=kind==0xb0?a:kind==0xe0?kPitchBend:kind==0xd0?kAfterTouch:-1;
    int value=kind==0xe0?(a|(b<<7)):kind==0xd0?a:b;
    if(number>=0 && controlCount_<int(controls_.size())) controls_[controlCount_++]={number,channel,value,offset};
}
void Plugin::queueMidi(int status,int a,int b) {
    auto w=uiMidiWrite_.load(); if(w-uiMidiRead_.load(std::memory_order_acquire)>=uiMidi_.size()) return;
    uiMidi_[w%uiMidi_.size()]={status,a,b}; uiMidiWrite_.store(w+1,std::memory_order_release);
}
void Plugin::parameter(unsigned id,double value,int offset) { if(parameterCount_<int(parameterPoints_.size()) && std::isfinite(value)) parameterPoints_[parameterCount_++]={id,std::clamp(value,0.,1.),offset}; }
void Plugin::note(int pitch, int velocity, bool on, int offset,int channel) {
    if(pitch<0 || pitch>127 || channel<0 || channel>15) return;
    activeNotes_[channel][pitch]=on;
    Event e{}; e.busIndex=0; e.sampleOffset=offset; e.flags=Event::kIsLive;
    if (on) { e.type=Event::kNoteOnEvent; e.noteOn={int16(channel),int16(pitch),0,float(velocity)/127.f,0,channel*128+pitch}; }
    else { e.type=Event::kNoteOffEvent; e.noteOff={int16(channel),int16(pitch),0,channel*128+pitch,0}; }
    events_.addEvent(e);
}
void Plugin::allOff() { for(int c=0;c<16;++c) { midi(0xb0|c,64,0); midi(0xb0|c,123,0); } }
void Plugin::process(float* left,float* right,int frames,double beat,double bpm,bool playing) {
    auto mr=uiMidiRead_.load(); auto mw=uiMidiWrite_.load(std::memory_order_acquire);
    while(mr!=mw) { auto m=uiMidi_[mr++%uiMidi_.size()]; midi(m.status,m.a,m.b); } uiMidiRead_.store(mr,std::memory_order_release);
    const int flags = restart_.exchange(0);
    if(flags & kReloadComponent) throw std::runtime_error("Plugin requested component reload; reopen instance");
    if(flags & (kIoChanged|kLatencyChanged)) {
        processor_->setProcessing(false); component_->setActive(false); processing_=active_=false;
        data_.unprepare();
        for(auto dir:{kInput,kOutput}) for(int32 i=0;i<component_->getBusCount(kAudio,dir);++i) { BusInfo bus{}; component_->getBusInfo(kAudio,dir,i,bus); component_->activateBus(kAudio,dir,i,bus.busType==kMain); }
        ProcessSetup setup{kRealtime,kSample32,Block,48000.};
        if(processor_->setupProcessing(setup)!=kResultOk || !data_.prepare(*component_,Block,kSample32)) throw std::runtime_error("Plugin bus reconfiguration failed");
        data_.processMode=kRealtime; data_.inputEvents=&events_; data_.outputEvents=&outputEvents_; data_.inputParameterChanges=&changes_; data_.outputParameterChanges=&outputChanges_;
        active_=component_->setActive(true)==kResultOk; auto result=processor_->setProcessing(true); processing_=active_ && (result==kResultOk||result==kNotImplemented);
        if(!processing_) throw std::runtime_error("Plugin reactivation failed");
        latency_.store(processor_->getLatencySamples());
    }
    changes_.clearQueue(); outputChanges_.clearQueue(); outputEvents_.clear();
    auto r = editRead_.load(); const auto w = editWrite_.load(std::memory_order_acquire);
    while (r!=w) { const auto& edit=edits_[r++%edits_.size()]; int32 index=0,point=0;
        if (auto* q=changes_.addParameterData(edit.id,index)) q->addPoint(0,edit.value,point);
    } editRead_.store(r,std::memory_order_release);
    for(int i=0;i<controlCount_;++i) {
        const auto& cc=controls_[i]; auto id=midiMapping_[cc.channel][cc.number].load(std::memory_order_acquire);
        if(id!=kNoParamId) { int32 index=0,point=0; double value=cc.value/double(cc.number==kPitchBend?16383:127); if(auto* q=changes_.addParameterData(id,index)) q->addPoint(cc.offset,value,point);
            auto w=feedbackWrite_.load(); if(w-feedbackRead_.load(std::memory_order_acquire)<midiFeedback_.size()) { midiFeedback_[w%midiFeedback_.size()]={id,value}; feedbackWrite_.store(w+1,std::memory_order_release); }
        }
    } controlCount_=0;
    for(int i=0;i<parameterCount_;++i) { auto p=parameterPoints_[i]; int32 index=0,point=0; if(auto* q=changes_.addParameterData(p.id,index)) q->addPoint(p.offset,p.value,point);
        if(i+1==parameterCount_ || parameterPoints_[i+1].id!=p.id) { auto w=feedbackWrite_.load(); if(w-feedbackRead_.load(std::memory_order_acquire)<midiFeedback_.size()) { midiFeedback_[w%midiFeedback_.size()]={p.id,p.value}; feedbackWrite_.store(w+1,std::memory_order_release); } }
    } parameterCount_=0;
    for (int b=0;b<data_.numInputs;++b) {
        auto& bus=data_.inputs[b]; bus.silenceFlags=0;
        for (int c=0;c<bus.numChannels;++c) {
            auto* dest=bus.channelBuffers32[c];
            if (b==0) std::copy_n(c==0?left:right,frames,dest); else std::fill_n(dest,frames,0.f);
        }
    }
    for (int b=0;b<data_.numOutputs;++b) {
        data_.outputs[b].silenceFlags=0;
        for (int c=0;c<data_.outputs[b].numChannels;++c) std::fill_n(data_.outputs[b].channelBuffers32[c],frames,0.f);
    }
    ProcessContext ctx{}; ctx.sampleRate=48000; ctx.tempo=bpm; ctx.timeSigNumerator=4; ctx.timeSigDenominator=4;
    ctx.projectTimeMusic=beat; ctx.projectTimeSamples=int64(beat*60/bpm*48000);
    ctx.barPositionMusic=std::floor(beat/4)*4;
    ctx.state=ProcessContext::kTempoValid|ProcessContext::kTimeSigValid|ProcessContext::kProjectTimeMusicValid|ProcessContext::kBarPositionValid;
    if (playing) ctx.state |= ProcessContext::kPlaying;
    data_.numSamples=frames; data_.processContext=&ctx;
    const auto result=processor_->process(data_); events_.clear();
    if(data_.outputParameterChanges) for(int i=0;i<data_.outputParameterChanges->getParameterCount();++i) {
        auto* q=data_.outputParameterChanges->getParameterData(i); if(!q || !q->getPointCount()) continue; int32 offset=0; double value=0;
        if(q->getPoint(q->getPointCount()-1,offset,value)==kResultOk && std::isfinite(value)) { auto w=feedbackWrite_.load(); if(w-feedbackRead_.load(std::memory_order_acquire)<midiFeedback_.size()) { midiFeedback_[w%midiFeedback_.size()]={q->getParameterId(),value}; feedbackWrite_.store(w+1,std::memory_order_release); } }
    }
    if (result != kResultOk) throw std::runtime_error("VST3 process failed");
    std::fill_n(left,frames,0.f); std::fill_n(right,frames,0.f);
    if (data_.numOutputs && data_.outputs[0].numChannels) {
        auto& bus=data_.outputs[0];
        if (!(bus.silenceFlags & 1)) std::copy_n(bus.channelBuffers32[0],frames,left);
        int c=bus.numChannels>1?1:0;
        if (!(bus.silenceFlags & (uint64(1)<<c))) std::copy_n(bus.channelBuffers32[c],frames,right);
    }
}
void Plugin::copyMidiOutput(PluginIPC& ipc) {
    ipc.midiOutputCount=0;
    for(int i=0;i<outputEvents_.getEventCount() && ipc.midiOutputCount<2048;++i) {
        Event event{}; if(outputEvents_.getEvent(i,event)!=kResultOk || event.busIndex!=0) continue; PluginIPC::Event m{}; m.offset=event.sampleOffset;
        if(event.type==Event::kNoteOnEvent) { m.status=0x90|(event.noteOn.channel&15); m.data1=event.noteOn.pitch; m.data2=int(std::round(event.noteOn.velocity*127)); }
        else if(event.type==Event::kNoteOffEvent) { m.status=0x80|(event.noteOff.channel&15); m.data1=event.noteOff.pitch; }
        else if(event.type==Event::kPolyPressureEvent) { m.status=0xa0|(event.polyPressure.channel&15); m.data1=event.polyPressure.pitch; m.data2=int(std::round(event.polyPressure.pressure*127)); }
        else if(event.type==Event::kLegacyMIDICCOutEvent) { auto cc=event.midiCCOut; int number=cc.controlNumber; if(number<128) { m.status=0xb0|(cc.channel&15); m.data1=number; m.data2=cc.value&127; } else if(number==kPitchBend) { m.status=0xe0|(cc.channel&15); m.data1=cc.value&127; m.data2=cc.value2&127; } else if(number==kAfterTouch) { m.status=0xd0|(cc.channel&15); m.data1=cc.value&127; } else continue; }
        else continue;
        if(m.data1>=0 && m.data1<128 && m.data2>=0 && m.data2<128) ipc.midiOutput[ipc.midiOutputCount++]=m;
    }
}
bool Plugin::openEditor(std::string& error,bool generic) {
    try { if(!controller_) { error="No edit controller"; return false; } if(!editor_) editor_=std::make_unique<PluginEditor>(*this,generic); else editor_->show(); return true; }
    catch(const std::exception& e) { error=e.what(); return false; }
}
void Plugin::pumpEditor() { if(editor_) editor_->pump(); }
tresult PLUGIN_API Plugin::resizeView(IPlugView*,ViewRect* rect) { if(!rect) return kInvalidArgument; if(editor_) editor_->resize(rect->getWidth(),rect->getHeight()); else if(window_) { editorWidth_=rect->getWidth(); editorHeight_=rect->getHeight(); RECT outer{0,0,std::max(620,editorWidth_),editorHeight_+100}; AdjustWindowRect(&outer,GetWindowLongW(window_,GWL_STYLE),FALSE); SetWindowPos(window_,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER); } else return kInvalidArgument; return kResultOk; }
std::vector<unsigned char> Plugin::state() {
    MemoryStream stream; if (component_->getState(&stream)!=kResultOk) throw std::runtime_error("Plugin state save failed");
    const auto* p=reinterpret_cast<unsigned char*>(stream.getData()); return {p,p+stream.getSize()};
}
bool Plugin::state(const std::vector<unsigned char>& bytes) {
    MemoryStream stream(const_cast<unsigned char*>(bytes.data()),bytes.size());
    bool ok=component_->setState(&stream)==kResultOk;
    if (controller_) { stream.seek(0,IBStream::kIBSeekSet,nullptr); controller_->setComponentState(&stream); }
    mappingDirty_.store(true); refreshMidiMapping();
    return ok;
}
}
