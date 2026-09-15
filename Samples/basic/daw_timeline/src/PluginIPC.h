// Copyright (c) 2026 KinuDAW contributors. MIT License.
#pragma once
#include "AudioEngine.h"
#include <windows.h>
#include <mutex>
namespace Kinu {
// One outstanding block: ownership passes to worker at 1, host at 2.
// The audio callback never waits on a worker or touches plug-in code.
struct PluginIPC {
    LONG phase = 0, quit = 0, editor = 0, editorResult=0, stateCommand = 0, stateResult = 0;
    LONG latency=0;
    int hostTrack=-1,hostSlot=0; DWORD owner=0;
    LONG route[6]{},routingStatus=0,bindRequest=0,bindTrack=0,bindParam=0;
    double bindValue=0;
    char bindName[256]{};
    struct Parameter { unsigned id; double value; int offset; } parameters[2048]; int parameterCount=0;
    int frames = Block, noteCount = 0; double beat = 0, bpm = 124; bool playing = false;
    struct Event { int status,data1,data2,offset; } notes[2048];
    Event midiOutput[2048]; int midiOutputCount=0;
    float left[Block], right[Block];
    wchar_t statePath[1024];
    char error[2048]{};
};
class RemotePlugin {
public:
    explicit RemotePlugin(const PluginInfo& info,int track=-1,int slot=0); ~RemotePlugin();
    void process(float*, float*, int, double, double, bool);
    void note(int, int, bool, int = 0,int channel=0); void allOff();
    void midi(int status,int data1,int data2,int offset=0);
    void parameter(unsigned id,double value,int offset=0);
    const PluginIPC::Event* midiOutput() const { return output_.data(); }
    int midiOutputCount() const { return outputCount_; }
    std::array<int,6> routing() const;
    void routing(const std::array<int,6>&);
    void routingStatus(int status) { InterlockedExchange(&ipc_->routingStatus,status); }
    bool bindingRequest(int& track,unsigned& param,double& value,std::string& name);
    unsigned latency() const { return ipc_?unsigned(InterlockedCompareExchange(&ipc_->latency,0,0)):0; }
    bool openEditor(std::string& error);
    std::vector<unsigned char> state(); bool state(const std::vector<unsigned char>&);
    const PluginInfo info;
    bool alive() const;
    DWORD workerId() const { return GetProcessId(process_); }
    void synchronous(bool enabled, unsigned timeout = 5000) { synchronous_=enabled; timeout_=timeout; }
private:
    HANDLE mapping_ = nullptr, process_ = nullptr, wake_ = nullptr, done_ = nullptr, job_ = nullptr; PluginIPC* ipc_ = nullptr;
    std::array<PluginIPC::Event,2048> pending_{}; int count_ = 0;
    std::array<PluginIPC::Event,2048> output_{}; int outputCount_=0;
    std::array<PluginIPC::Parameter,2048> parameters_{}; int parameterCount_=0;
    ULONGLONG requested_ = 0;
    std::filesystem::path statePath_,configPath_;
    void shutdown() noexcept;
    bool stateRequest(int command);
    bool synchronous_=false;
    unsigned timeout_=5000;
    std::atomic<bool> controlBusy_{false};
    std::mutex controlMutex_;
};
}
