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
    int frames = Block, noteCount = 0; double beat = 0, bpm = 124; bool playing = false;
    struct Event { int pitch, velocity, offset; bool on; } notes[2048];
    float left[Block], right[Block];
    wchar_t statePath[1024];
    char error[2048]{};
};
class RemotePlugin {
public:
    explicit RemotePlugin(const PluginInfo& info); ~RemotePlugin();
    void process(float*, float*, int, double, double, bool);
    void note(int, int, bool, int = 0); void allOff();
    bool openEditor(std::string& error);
    std::vector<unsigned char> state(); bool state(const std::vector<unsigned char>&);
    const PluginInfo info;
    bool alive() const;
    DWORD workerId() const { return GetProcessId(process_); }
    void synchronous(bool enabled, unsigned timeout = 5000) { synchronous_=enabled; timeout_=timeout; }
private:
    HANDLE mapping_ = nullptr, process_ = nullptr, wake_ = nullptr, done_ = nullptr, job_ = nullptr; PluginIPC* ipc_ = nullptr;
    std::array<PluginIPC::Event,2048> pending_{}; int count_ = 0;
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
