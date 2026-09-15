// Copyright (c) 2026 KinuDAW contributors. MIT License.
#pragma once
#include "AudioEngine.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/gui/iplugview.h"
#include <windows.h>

namespace Kinu {
class PluginEditor;
struct PluginIPC;
class Plugin final : public Steinberg::Vst::IComponentHandler, public Steinberg::IPlugFrame {
public:
    explicit Plugin(const PluginInfo& info); ~Plugin();
    void process(float* left, float* right, int frames, double beat, double bpm, bool playing);
    void note(int pitch, int velocity, bool on, int offset = 0,int channel=0);
    void midi(int status,int data1,int data2,int offset=0);
    void refreshMidiMapping();
    void queueMidi(int status,int data1,int data2);
    void parameter(unsigned id,double value,int offset=0);
    bool hasEditor() const { return bool(editor_); } void pumpEditor(); void setIPC(PluginIPC* ipc) { ipc_=ipc; }
    void copyMidiOutput(PluginIPC&);
    unsigned latency() const { return latency_.load(); }
    void allOff(); bool openEditor(std::string& error,bool forceGeneric=false);
    std::vector<unsigned char> state(); bool state(const std::vector<unsigned char>& bytes);
    const PluginInfo info;
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override;
    Steinberg::uint32 PLUGIN_API addRef() override { return ++refs_; }
    Steinberg::uint32 PLUGIN_API release() override { return --refs_; }
    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue) override;
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID) override { return Steinberg::kResultOk; }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;
    Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView*, Steinberg::ViewRect*) override;
private:
    friend class PluginEditor;
    std::unique_ptr<PluginEditor> editor_;
    PluginIPC* ipc_=nullptr;
    Steinberg::Vst::HostApplication host_;
    VST3::Hosting::Module::Ptr module_;
    std::unique_ptr<Steinberg::Vst::PlugProvider> provider_;
    Steinberg::IPtr<Steinberg::Vst::IComponent> component_;
    Steinberg::IPtr<Steinberg::Vst::IEditController> controller_;
    Steinberg::IPtr<Steinberg::Vst::IAudioProcessor> processor_;
    Steinberg::IPtr<Steinberg::IPlugView> view_;
    Steinberg::Vst::HostProcessData data_;
    Steinberg::Vst::EventList events_{2048}, outputEvents_{2048};
    std::array<std::array<bool,128>,16> activeNotes_{};
    Steinberg::Vst::ParameterChanges changes_, outputChanges_;
    struct Edit { Steinberg::Vst::ParamID id; double value; };
    std::array<Edit, 4096> edits_{};
    std::array<Edit,4096> midiFeedback_{};
    std::atomic<unsigned> feedbackWrite_{0},feedbackRead_{0};
    std::atomic<unsigned> editWrite_{0}, editRead_{0}, refs_{1};
    std::atomic<int> restart_{0};
    std::atomic<bool> mappingDirty_{true};
    std::array<std::array<std::atomic<Steinberg::Vst::ParamID>,Steinberg::Vst::kCountCtrlNumber>,16> midiMapping_{};
    struct Control { int number,channel,value,offset; };
    std::array<Control,2048> controls_{}; int controlCount_=0;
    std::atomic<unsigned> latency_{0};
    HWND window_ = nullptr; bool active_ = false, processing_ = false;
    HWND viewWindow_=nullptr;
    int editorWidth_=800,editorHeight_=600;
    struct ParameterPoint { unsigned id; double value; int offset; };
    std::array<ParameterPoint,2048> parameterPoints_{}; int parameterCount_=0;
    struct RawMidi { int status,a,b; };
    std::array<RawMidi,1024> uiMidi_{}; std::atomic<unsigned> uiMidiWrite_{0},uiMidiRead_{0};
};
}
