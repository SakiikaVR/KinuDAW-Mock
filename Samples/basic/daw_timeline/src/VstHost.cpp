// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "VstHost.h"
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
}
Plugin::~Plugin() {
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
    restart_.fetch_or(flags); return kResultOk;
}
void Plugin::note(int pitch, int velocity, bool on, int offset) {
    Event e{}; e.busIndex=0; e.sampleOffset=offset; e.flags=Event::kIsLive;
    if (on) { e.type=Event::kNoteOnEvent; e.noteOn={0,int16(pitch),0,float(velocity)/127.f,0,pitch}; }
    else { e.type=Event::kNoteOffEvent; e.noteOff={0,int16(pitch),0,pitch,0}; }
    events_.addEvent(e);
}
void Plugin::allOff() { for (int p=0;p<128;++p) note(p,0,false); }
void Plugin::process(float* left,float* right,int frames,double beat,double bpm,bool playing) {
    const int flags = restart_.exchange(0);
    if(flags & kReloadComponent) throw std::runtime_error("Plugin requested component reload; reopen instance");
    if(flags & kIoChanged) {
        processor_->setProcessing(false); component_->setActive(false); processing_=active_=false;
        data_.unprepare();
        for(auto dir:{kInput,kOutput}) for(int32 i=0;i<component_->getBusCount(kAudio,dir);++i) { BusInfo bus{}; component_->getBusInfo(kAudio,dir,i,bus); component_->activateBus(kAudio,dir,i,bus.busType==kMain); }
        ProcessSetup setup{kRealtime,kSample32,Block,48000.};
        if(processor_->setupProcessing(setup)!=kResultOk || !data_.prepare(*component_,Block,kSample32)) throw std::runtime_error("Plugin bus reconfiguration failed");
        data_.processMode=kRealtime; data_.inputEvents=&events_; data_.outputEvents=&outputEvents_; data_.inputParameterChanges=&changes_; data_.outputParameterChanges=&outputChanges_;
        active_=component_->setActive(true)==kResultOk; auto result=processor_->setProcessing(true); processing_=active_ && (result==kResultOk||result==kNotImplemented);
        if(!processing_) throw std::runtime_error("Plugin reactivation failed");
    }
    changes_.clearQueue(); outputChanges_.clearQueue(); outputEvents_.clear();
    auto r = editRead_.load(); const auto w = editWrite_.load(std::memory_order_acquire);
    while (r!=w) { const auto& edit=edits_[r++%edits_.size()]; int32 index=0,point=0;
        if (auto* q=changes_.addParameterData(edit.id,index)) q->addPoint(0,edit.value,point);
    } editRead_.store(r,std::memory_order_release);
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
    if (result != kResultOk) throw std::runtime_error("VST3 process failed");
    std::fill_n(left,frames,0.f); std::fill_n(right,frames,0.f);
    if (data_.numOutputs && data_.outputs[0].numChannels) {
        auto& bus=data_.outputs[0];
        if (!(bus.silenceFlags & 1)) std::copy_n(bus.channelBuffers32[0],frames,left);
        int c=bus.numChannels>1?1:0;
        if (!(bus.silenceFlags & (uint64(1)<<c))) std::copy_n(bus.channelBuffers32[c],frames,right);
    }
}
LRESULT CALLBACK Plugin::windowProc(HWND h,UINT msg,WPARAM w,LPARAM l) {
    auto* p=reinterpret_cast<Plugin*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { p=static_cast<Plugin*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(h,GWLP_USERDATA,LONG_PTR(p)); }
    if (msg==WM_CLOSE) { ShowWindow(h,SW_HIDE); return 0; }
    if(p && p->parameterList_ && msg==WM_COMMAND) {
        if(LOWORD(w)==101 && HIWORD(w)==LBN_SELCHANGE) p->selectParameter();
        if(LOWORD(w)==103 && HIWORD(w)==BN_CLICKED) p->applyParameter();
        return 0;
    }
    if(p && p->parameterList_ && msg==WM_SIZE) { int width=LOWORD(l),height=HIWORD(l); MoveWindow(p->parameterList_,8,8,std::max(80,width-16),std::max(40,height-52),TRUE); MoveWindow(p->parameterValue_,8,std::max(48,height-36),std::max(40,width-120),26,TRUE); if(auto button=GetDlgItem(h,103)) MoveWindow(button,std::max(60,width-104),std::max(48,height-36),96,26,TRUE); }
    if (p && p->view_ && msg==WM_SIZE) { ViewRect rect{0,0,int(LOWORD(l)),int(HIWORD(l))}; p->view_->onSize(&rect); }
    return DefWindowProcW(h,msg,w,l);
}
bool Plugin::openEditor(std::string& error,bool forceGeneric) {
    if (window_) { ShowWindow(window_,SW_SHOW); SetForegroundWindow(window_); return true; }
    if (!controller_) { error="No edit controller"; return false; }
    if(forceGeneric) return genericEditor(error);
    view_=owned(controller_->createView(ViewType::kEditor));
    if (!view_ || view_->isPlatformTypeSupported(kPlatformTypeHWND)!=kResultOk) { view_.reset(); return genericEditor(error); }
    WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"KinuVstEditor"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); RegisterClassW(&wc);
    ViewRect rect{0,0,800,600}; view_->getSize(&rect);
    RECT outer{0,0,rect.getWidth(),rect.getHeight()}; AdjustWindowRect(&outer,WS_OVERLAPPEDWINDOW,FALSE);
    auto title=std::filesystem::u8path(info.name).wstring();
    window_=CreateWindowW(wc.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,outer.right-outer.left,outer.bottom-outer.top,nullptr,nullptr,wc.hInstance,this);
    view_->setFrame(this);
    if (view_->attached(window_,kPlatformTypeHWND)!=kResultOk) {
        view_->setFrame(nullptr); view_.reset(); DestroyWindow(window_); window_=nullptr; error="Editor attachment failed"; return false;
    }
    view_->onSize(&rect); ShowWindow(window_,SW_SHOW); return true;
}
bool Plugin::genericEditor(std::string& error) {
    WNDCLASSW wc{}; wc.lpfnWndProc=windowProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"KinuVstEditor"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); RegisterClassW(&wc);
    auto title=std::filesystem::u8path(info.name+" / Parameters (0..1)").wstring();
    window_=CreateWindowW(wc.lpszClassName,title.c_str(),WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,620,500,nullptr,nullptr,wc.hInstance,this);
    if(!window_) { error="Parameter editor creation failed"; return false; }
    parameterList_=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,8,8,588,400,window_,HMENU(101),wc.hInstance,nullptr);
    parameterValue_=CreateWindowW(L"EDIT",L"0",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,8,420,480,26,window_,HMENU(102),wc.hInstance,nullptr);
    CreateWindowW(L"BUTTON",L"Apply",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,496,420,96,26,window_,HMENU(103),wc.hInstance,nullptr);
    for(int32 i=0;i<controller_->getParameterCount();++i) { ParameterInfo parameter{}; if(controller_->getParameterInfo(i,parameter)!=kResultOk) continue; parameterIds_.push_back(parameter.id); SendMessageW(parameterList_,LB_ADDSTRING,0,LPARAM(reinterpret_cast<const wchar_t*>(parameter.title))); }
    if(!parameterIds_.empty()) { SendMessageW(parameterList_,LB_SETCURSEL,0,0); selectParameter(); }
    ShowWindow(window_,SW_SHOW); return true;
}
void Plugin::selectParameter() {
    int index=int(SendMessageW(parameterList_,LB_GETCURSEL,0,0)); if(index<0 || index>=int(parameterIds_.size())) return;
    auto value=controller_->getParamNormalized(parameterIds_[index]); wchar_t text[64]{}; swprintf_s(text,L"%.8f",value); SetWindowTextW(parameterValue_,text);
}
void Plugin::applyParameter() {
    int index=int(SendMessageW(parameterList_,LB_GETCURSEL,0,0)); if(index<0 || index>=int(parameterIds_.size())) return;
    wchar_t text[64]{},*end=nullptr; GetWindowTextW(parameterValue_,text,64); double value=wcstod(text,&end); if(end==text || *end || !std::isfinite(value)) return;
    value=std::clamp(value,0.,1.); auto id=parameterIds_[index]; controller_->setParamNormalized(id,value); beginEdit(id); performEdit(id,value); endEdit(id); selectParameter();
}
tresult PLUGIN_API Plugin::resizeView(IPlugView*,ViewRect* rect) {
    if (!window_ || !rect) return kInvalidArgument;
    RECT outer{0,0,rect->getWidth(),rect->getHeight()}; AdjustWindowRect(&outer,WS_OVERLAPPEDWINDOW,FALSE);
    SetWindowPos(window_,nullptr,0,0,outer.right-outer.left,outer.bottom-outer.top,SWP_NOMOVE|SWP_NOZORDER); return kResultOk;
}
std::vector<unsigned char> Plugin::state() {
    MemoryStream stream; if (component_->getState(&stream)!=kResultOk) throw std::runtime_error("Plugin state save failed");
    const auto* p=reinterpret_cast<unsigned char*>(stream.getData()); return {p,p+stream.getSize()};
}
bool Plugin::state(const std::vector<unsigned char>& bytes) {
    MemoryStream stream(const_cast<unsigned char*>(bytes.data()),bytes.size());
    bool ok=component_->setState(&stream)==kResultOk;
    if (controller_) { stream.seek(0,IBStream::kIBSeekSet,nullptr); controller_->setComponentState(&stream); }
    return ok;
}
}
