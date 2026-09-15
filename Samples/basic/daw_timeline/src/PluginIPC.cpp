// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "PluginIPC.h"
#include "json.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>
namespace Kinu {
static std::atomic<unsigned> serial{0};
static std::wstring quote(const std::wstring& s) {
    std::wstring result=L"\""; unsigned slashes=0;
    for (auto c:s) { if (c==L'\\') { ++slashes; continue; }
        result.append(c==L'"'?slashes*2+1:slashes,L'\\'); result+=c; slashes=0;
    } result.append(slashes*2,L'\\'); return result+L"\"";
}
RemotePlugin::RemotePlugin(const PluginInfo& metadata,int track,int slot) : info(metadata) {
    try {
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,32768);
    auto worker=std::filesystem::path(executable).parent_path()/L"kinu_vst_worker.exe";
    auto id=std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
    auto mappingName=L"Local\\KinuVst-"+id;
    wake_=CreateEventW(nullptr,FALSE,FALSE,(mappingName+L"-request").c_str());
    done_=CreateEventW(nullptr,FALSE,FALSE,(mappingName+L"-done").c_str());
    statePath_=std::filesystem::temp_directory_path()/(L"kinu-vst-"+id+L".state");
    configPath_=std::filesystem::temp_directory_path()/(L"kinu-vst-"+id+L".json"); const auto& config=configPath_;
    if(!wake_ || !done_) throw std::runtime_error("Cannot create worker events");
    nlohmann::json j={{"path",info.path},{"uid",info.uid},{"name",info.name},{"category",info.category},{"vendor",info.vendor}};
    { std::ofstream out(config); out<<j.dump(); if (!out) throw std::runtime_error("Cannot write worker configuration"); }
    mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(PluginIPC),mappingName.c_str());
    if (mapping_) ipc_=static_cast<PluginIPC*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(PluginIPC)));
    if (!ipc_) throw std::runtime_error("Cannot create plugin IPC");
    new(ipc_) PluginIPC{};
    ipc_->hostTrack=track; ipc_->hostSlot=slot; ipc_->owner=GetCurrentProcessId();
    wcsncpy_s(ipc_->statePath,statePath_.c_str(),_TRUNCATE);
    auto cmd=quote(worker.wstring())+L" --worker "+quote(config.wstring())+L" "+quote(mappingName)+L" "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    job_=CreateJobObjectW(nullptr,nullptr); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{}; limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(!job_ || !SetInformationJobObject(job_,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) throw std::runtime_error("Cannot configure worker job isolation");
    if (!CreateProcessW(worker.c_str(),cmd.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,worker.parent_path().c_str(),&startup,&process)) {
        throw std::runtime_error("Cannot launch kinu_vst_worker.exe");
    }
    process_=process.hProcess;
    if(!job_ || !AssignProcessToJobObject(job_,process_)) {
        TerminateProcess(process_,1); CloseHandle(process.hThread);
        throw std::runtime_error("Cannot isolate plugin worker in Windows Job Object");
    }
    ResumeThread(process.hThread); CloseHandle(process.hThread);
    const auto deadline=GetTickCount64()+20000;
    while (InterlockedCompareExchange(&ipc_->phase,0,0)==0 && alive() && GetTickCount64()<deadline) Sleep(10);
    std::error_code ec; std::filesystem::remove(config,ec);
    if (InterlockedCompareExchange(&ipc_->phase,0,0)!=2) {
        std::string failure=ipc_->error[0]?ipc_->error:"Plugin crashed, hung, or failed initialization (worker isolated)";
        throw std::runtime_error(failure);
    }
    InterlockedExchange(&ipc_->phase,0);
    } catch(...) { shutdown(); throw; }
}
RemotePlugin::~RemotePlugin() { shutdown(); }
void RemotePlugin::shutdown() noexcept {
    if (ipc_) InterlockedExchange(&ipc_->quit,1);
    if (process_) { if (WaitForSingleObject(process_,500)==WAIT_TIMEOUT) TerminateProcess(process_,1); CloseHandle(process_); }
    if (ipc_) UnmapViewOfFile(ipc_); if(mapping_) CloseHandle(mapping_);
    if(wake_) CloseHandle(wake_);
    if(done_) CloseHandle(done_);
    if(job_) CloseHandle(job_);
    std::error_code ec; std::filesystem::remove(statePath_,ec);
    std::filesystem::remove(configPath_,ec);
}
bool RemotePlugin::alive() const { return process_ && WaitForSingleObject(process_,0)==WAIT_TIMEOUT; }
void RemotePlugin::note(int pitch,int velocity,bool on,int offset,int channel) {
    midi((on?0x90:0x80)|(channel&15),pitch,velocity,offset);
}
void RemotePlugin::midi(int status,int a,int b,int offset) {
    if(count_<int(pending_.size())) pending_[count_++]={status,a,b,offset};
}
void RemotePlugin::parameter(unsigned id,double value,int offset) { if(parameterCount_<int(parameters_.size())) parameters_[parameterCount_++]={id,value,offset}; }
std::array<int,6> RemotePlugin::routing() const { std::array<int,6> result{}; for(int i=0;i<6;++i) result[i]=InterlockedCompareExchange(ipc_->route+i,0,0); return result; }
void RemotePlugin::routing(const std::array<int,6>& values) { for(int i=0;i<6;++i) InterlockedExchange(ipc_->route+i,values[i]); }
bool RemotePlugin::bindingRequest(int& track,unsigned& param,double& value,std::string& name) { if(!InterlockedCompareExchange(&ipc_->bindRequest,0,0)) return false; track=ipc_->bindTrack; param=unsigned(ipc_->bindParam); value=ipc_->bindValue; name=ipc_->bindName; InterlockedExchange(&ipc_->bindRequest,0); return true; }
void RemotePlugin::allOff() { count_=0; for(int c=0;c<16;++c) { midi(0xb0|c,64,0); midi(0xb0|c,123,0); } }
void RemotePlugin::process(float* left,float* right,int frames,double beat,double bpm,bool playing) {
    outputCount_=0;
    // Preserve unprocessed input for effects when the worker misses its deadline.
    // Instruments receive zero input. No process wait, IPC lock, allocation, or DLL call.
    if (!alive()) { count_=0; if(synchronous_) throw std::runtime_error("Plugin worker exited during export"); return; }
    if(controlBusy_.load()) return;
    // This runs on the renderer, never the WASAPI callback. A state operation
    // bypasses processing rather than making the callback wait for a UI save.
    std::unique_lock<std::mutex> control(controlMutex_,std::try_to_lock);
    if(!control.owns_lock() || controlBusy_.load()) return;
    if(synchronous_) {
        if(InterlockedCompareExchange(&ipc_->phase,0,0)==1) WaitForSingleObject(done_,timeout_);
        if(!alive() || InterlockedCompareExchange(&ipc_->phase,0,0)==1) { TerminateProcess(process_,2); throw std::runtime_error("Plugin processing timed out"); }
        std::copy_n(left,frames,ipc_->left); std::copy_n(right,frames,ipc_->right);
        ipc_->frames=frames; ipc_->beat=beat; ipc_->bpm=bpm; ipc_->playing=playing;
        ipc_->noteCount=count_; std::copy_n(pending_.data(),count_,ipc_->notes); count_=0;
        ipc_->parameterCount=parameterCount_; std::copy_n(parameters_.data(),parameterCount_,ipc_->parameters); parameterCount_=0;
        ResetEvent(done_); InterlockedExchange(&ipc_->phase,1); SetEvent(wake_); WaitForSingleObject(done_,timeout_);
        if(!alive() || InterlockedCompareExchange(&ipc_->phase,0,0)!=2) { TerminateProcess(process_,2); throw std::runtime_error("Plugin processing failed"); }
        std::copy_n(ipc_->left,frames,left); std::copy_n(ipc_->right,frames,right); outputCount_=std::clamp(ipc_->midiOutputCount,0,2048); std::copy_n(ipc_->midiOutput,outputCount_,output_.data()); InterlockedExchange(&ipc_->phase,0); return;
    }
    LONG phase=InterlockedCompareExchange(&ipc_->phase,0,0);
    if (phase==1) {
        if (requested_ && GetTickCount64()-requested_>2000) { TerminateProcess(process_,2); count_=0; }
        return;
    }
    std::array<float,Block> inL{},inR{}; std::copy_n(left,frames,inL.data()); std::copy_n(right,frames,inR.data());
    if (phase==2) { std::copy_n(ipc_->left,frames,left); std::copy_n(ipc_->right,frames,right); outputCount_=std::clamp(ipc_->midiOutputCount,0,2048); std::copy_n(ipc_->midiOutput,outputCount_,output_.data()); }
    std::copy_n(inL.data(),frames,ipc_->left); std::copy_n(inR.data(),frames,ipc_->right);
    ipc_->frames=frames; ipc_->beat=beat; ipc_->bpm=bpm; ipc_->playing=playing;
    ipc_->noteCount=count_; std::copy_n(pending_.data(),count_,ipc_->notes); count_=0;
    ipc_->parameterCount=parameterCount_; std::copy_n(parameters_.data(),parameterCount_,ipc_->parameters); parameterCount_=0;
    requested_=GetTickCount64(); InterlockedExchange(&ipc_->phase,1); SetEvent(wake_);
}
bool RemotePlugin::openEditor(std::string& error) {
    if (!alive()) { error="Plugin worker exited; reload plugin"; return false; }
    AllowSetForegroundWindow(workerId());
    InterlockedExchange(&ipc_->editorResult,0); InterlockedExchange(&ipc_->editor,1);
    auto deadline=GetTickCount64()+5000; while(alive() && !InterlockedCompareExchange(&ipc_->editorResult,0,0) && GetTickCount64()<deadline) Sleep(10);
    if(InterlockedCompareExchange(&ipc_->editorResult,0,0)!=1) { error=ipc_->error[0]?ipc_->error:"Editor startup timed out"; return false; } return true;
}
bool RemotePlugin::stateRequest(int cmd) {
    if (!alive()) return false;
    controlBusy_.store(true);
    std::lock_guard<std::mutex> control(controlMutex_);
    InterlockedExchange(&ipc_->stateResult,0); InterlockedExchange(&ipc_->stateCommand,cmd);
    auto end=GetTickCount64()+5000;
    while (alive() && !InterlockedCompareExchange(&ipc_->stateResult,0,0) && GetTickCount64()<end) Sleep(10);
    bool ok=InterlockedCompareExchange(&ipc_->stateResult,0,0)==1; controlBusy_.store(false); return ok;
}
std::vector<unsigned char> RemotePlugin::state() {
    if (!stateRequest(1)) throw std::runtime_error("Plugin state save timed out or failed");
    std::ifstream input(statePath_,std::ios::binary); return {std::istreambuf_iterator<char>(input),{}};
}
bool RemotePlugin::state(const std::vector<unsigned char>& bytes) {
    { std::ofstream output(statePath_,std::ios::binary); output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); if (!output) return false; }
    return stateRequest(2);
}
}
