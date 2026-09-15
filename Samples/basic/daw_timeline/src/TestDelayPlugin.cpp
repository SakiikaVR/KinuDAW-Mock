// Copyright (c) 2026 KinuDAW contributors. MIT License.
// Deterministic test fixture. Built only for tests and excluded from distribution.
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/base/ibstream.h"
#include <array>
#include <algorithm>
#include <atomic>
using namespace Steinberg;
using namespace Steinberg::Vst;
static const FUID ProcessorID(0x12405080,0x11223344,0x55667788,0x99AABBCC);
static const FUID ControllerID(0x12405080,0x11223344,0x55667788,0x99AABBCD);
static std::atomic<unsigned> requestedLatency{257};
class DelayProcessor : public AudioEffect {
public:
    DelayProcessor() { setControllerClass(ControllerID); }
    static FUnknown* create(void*) { return static_cast<IAudioProcessor*>(new DelayProcessor); }
    tresult PLUGIN_API initialize(FUnknown* context) override { auto r=AudioEffect::initialize(context); if(r!=kResultOk) return r; addAudioInput(STR16("Stereo Input"),SpeakerArr::kStereo); addAudioOutput(STR16("Stereo Output"),SpeakerArr::kStereo); addEventInput(STR16("MIDI"),16); addEventOutput(STR16("MIDI Out"),16); return kResultOk; }
    uint32 PLUGIN_API getLatencySamples() override { return latency_; }
    tresult PLUGIN_API setActive(TBool state) override { for(auto& side:delay_) side.fill(0); position_=0; if(state) latency_=requestedLatency.load(); return AudioEffect::setActive(state); }
    tresult PLUGIN_API process(ProcessData& data) override {
        if(data.inputEvents && data.outputEvents) for(int i=0;i<data.inputEvents->getEventCount();++i) { Event event{}; if(data.inputEvents->getEvent(i,event)==kResultOk) data.outputEvents->addEvent(event); }
        if(!data.numInputs || !data.numOutputs) return kResultOk;
        for(int i=0;i<data.numSamples;++i) {
            if(data.inputParameterChanges) for(int q=0;q<data.inputParameterChanges->getParameterCount();++q) { auto* queue=data.inputParameterChanges->getParameterData(q); if(queue->getParameterId()!=1) continue; for(int p=0;p<queue->getPointCount();++p) { int32 offset=0; ParamValue value=0; if(queue->getPoint(p,offset,value)==kResultOk && offset==i) gain_=value; } }
            for(int side=0;side<2;++side) { float input=data.inputs[0].channelBuffers32[side][i]; float old=delay_[side][position_]; delay_[side][position_]=input; data.outputs[0].channelBuffers32[side][i]=old*float(gain_); }
            position_=(position_+1)%latency_;
        } data.outputs[0].silenceFlags=0; return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream* state) override { return state->write(&gain_,sizeof(gain_),nullptr); }
    tresult PLUGIN_API setState(IBStream* state) override { return state->read(&gain_,sizeof(gain_),nullptr); }
private:
    std::array<std::array<float,513>,2> delay_{}; unsigned position_=0,latency_=257; double gain_=1;
};
class DelayController : public EditControllerEx1, public IMidiMapping {
public:
    static FUnknown* create(void*) { return static_cast<IEditController*>(new DelayController); }
    tresult PLUGIN_API initialize(FUnknown* context) override { auto r=EditControllerEx1::initialize(context); if(r==kResultOk) { parameters.addParameter(STR16("Gain (CC7, bend, pressure)"),nullptr,0,1,ParameterInfo::kCanAutomate,1); parameters.addParameter(STR16("Latency 257/513 (CC20)"),nullptr,1,0,ParameterInfo::kCanAutomate,2); } return r; }
    tresult PLUGIN_API setParamNormalized(ParamID id,ParamValue value) override { auto r=EditControllerEx1::setParamNormalized(id,value); if(id==2) { unsigned wanted=value>=.5?513:257; if(requestedLatency.exchange(wanted)!=wanted && componentHandler) componentHandler->restartComponent(kLatencyChanged); } return r; }
    tresult PLUGIN_API setComponentState(IBStream* state) override { double gain=1; if(state->read(&gain,sizeof(gain),nullptr)!=kResultOk) return kResultFalse; setParamNormalized(1,gain); return kResultOk; }
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus,int16 channel,CtrlNumber number,ParamID& id) override { if(bus==0 && channel>=0 && channel<16) { if(number==20) { id=2; return kResultOk; } if(number==7 || number==kPitchBend || number==kAfterTouch) { id=1; return kResultOk; } } return kResultFalse; }
    DELEGATE_REFCOUNT(EditControllerEx1)
    tresult PLUGIN_API queryInterface(const TUID iid,void** obj) override { QUERY_INTERFACE(iid,obj,IMidiMapping::iid,IMidiMapping) return EditControllerEx1::queryInterface(iid,obj); }
};
BEGIN_FACTORY_DEF("KinuDAW tests","https://github.com/SakiikaVR/KinuDAW-Mock","")
DEF_CLASS2(INLINE_UID_FROM_FUID(ProcessorID),PClassInfo::kManyInstances,kVstAudioEffectClass,"Kinu Delay Test",kDistributable,"Fx", "1.0.0",kVstVersionString,DelayProcessor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(ControllerID),PClassInfo::kManyInstances,kVstComponentControllerClass,"Kinu Delay Test Controller",0,"", "1.0.0",kVstVersionString,DelayController::create)
END_FACTORY
