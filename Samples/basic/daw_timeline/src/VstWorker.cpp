// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "VstHost.h"
#include "PluginIPC.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <shellapi.h>
#include <objbase.h>
#include <cmath>
using nlohmann::json;
static Kinu::PluginInfo parse(const json& j) { return {j.at("path"),j.at("uid"),j.at("name"),j.value("category",""),j.value("vendor","")}; }
int wmain(int argc,wchar_t** argv) {
    Kinu::PluginIPC* shared=nullptr;
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    try {
        if (argc>=3 && std::wstring(argv[1])==L"--scan") {
            std::string error; auto path=std::filesystem::path(argv[2]).u8string();
            auto plugins=Kinu::scanModule(path,error); json result={{"path",path},{"error",error},{"plugins",json::array()}};
            for(const auto& p:plugins) result["plugins"].push_back({{"path",p.path},{"uid",p.uid},{"name",p.name},{"category",p.category},{"vendor",p.vendor}});
            if(argc>=4) { std::ofstream out{std::filesystem::path(argv[3])}; out<<result.dump(2); }
            else std::cout<<result.dump(2); return plugins.empty()?1:0;
        }
        if (argc==2 && std::wstring(argv[1])==L"--paths") { std::cout<<json(Kinu::modulePaths()).dump(2); return 0; }
        if(argc>=3 && std::wstring(argv[1])==L"--editor-test") {
            std::ifstream input{std::filesystem::path(argv[2])}; json j; input>>j; Kinu::Plugin plugin(parse(j)); std::string error;
            if(!plugin.openEditor(error,argc>=4 && std::wstring(argv[3])==L"--generic")) throw std::runtime_error(error);
            auto deadline=GetTickCount64()+15000; float left[Kinu::Block]{},right[Kinu::Block]{};
            while(GetTickCount64()<deadline) { MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } std::fill_n(left,Kinu::Block,.05f); std::fill_n(right,Kinu::Block,.05f); plugin.process(left,right,Kinu::Block,0,120,false); Sleep(3); }
            std::cout<<"Editor opened and processing continued\n"; return 0;
        }
        if (argc>=3 && std::wstring(argv[1])==L"--probe") {
            std::ifstream input{std::filesystem::path(argv[2])}; json j; input>>j; Kinu::Plugin plugin(parse(j));
            double energy=0; float left[Kinu::Block]{},right[Kinu::Block]{};
            for(int b=0;b<512;++b) {
                MSG message{}; while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                if(b==0) plugin.note(60,100,true); if(b==250) plugin.note(60,0,false);
                for(int i=0;i<Kinu::Block;++i) left[i]=right[i]=j.value("category","").find("Instrument")!=std::string::npos?0.f:float(.05*std::sin((b*Kinu::Block+i)*.0575959));
                plugin.process(left,right,Kinu::Block,b*Kinu::Block/48000.*124/60,124,true);
                for(auto v:left) { if(!std::isfinite(v)) throw std::runtime_error("Non-finite plugin output"); energy+=v*v; }
            }
            auto bytes=plugin.state(); bool restore=plugin.state(bytes);
            json result={{"name",j.at("name")},{"processedBlocks",512},{"energy",energy},{"stateBytes",bytes.size()},{"restored",restore}};
            if(argc>=4) { std::ofstream out{std::filesystem::path(argv[3])}; out<<result.dump(2); } else std::cout<<result.dump(2);
            return restore?0:1;
        }
        if(argc<5 || std::wstring(argv[1])!=L"--worker") return 2;
        std::ifstream input{std::filesystem::path(argv[2])}; json j; input>>j;
        HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,argv[3]);
        auto* ipc=static_cast<Kinu::PluginIPC*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Kinu::PluginIPC)));
        shared=ipc;
        if(!ipc) throw std::runtime_error("IPC mapping failed");
        HANDLE owner=OpenProcess(SYNCHRONIZE,FALSE,_wtoi(argv[4]));
        HANDLE wake=OpenEventW(SYNCHRONIZE,FALSE,(std::wstring(argv[3])+L"-request").c_str());
        HANDLE done=OpenEventW(EVENT_MODIFY_STATE,FALSE,(std::wstring(argv[3])+L"-done").c_str());
        if(!owner) throw std::runtime_error("Owner not available");
        Kinu::Plugin plugin(parse(j)); std::mutex processing; std::atomic<bool> stop{false};
        InterlockedExchange(&ipc->phase,2);
        std::thread audio([&] {
            CoInitializeEx(nullptr,COINIT_MULTITHREADED); SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
            try { while(!stop.load()) {
                if(InterlockedCompareExchange(&ipc->phase,0,0)==1) {
                    std::lock_guard<std::mutex> lock(processing);
                    int count=std::clamp(ipc->noteCount,0,2048),frames=std::clamp(ipc->frames,1,Kinu::Block);
                    for(int i=0;i<count;++i) { auto e=ipc->notes[i]; plugin.note(std::clamp(e.pitch,0,127),std::clamp(e.velocity,0,127),e.on,std::clamp(e.offset,0,frames-1)); }
                    plugin.process(ipc->left,ipc->right,frames,ipc->beat,ipc->bpm,ipc->playing);
                    InterlockedExchange(&ipc->phase,2);
                    SetEvent(done);
                } else if(wake) WaitForSingleObject(wake,50); else Sleep(1);
            } } catch(const std::exception& e) { strncpy_s(ipc->error,e.what(),_TRUNCATE); InterlockedExchange(&ipc->phase,-1); SetEvent(done); ExitProcess(1); }
            CoUninitialize();
        });
        while(!InterlockedCompareExchange(&ipc->quit,0,0) && WaitForSingleObject(owner,0)==WAIT_TIMEOUT) {
            MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            if(InterlockedExchange(&ipc->editor,0)) { std::string error; bool ok=plugin.openEditor(error); if(!ok) strncpy_s(ipc->error,error.c_str(),_TRUNCATE); InterlockedExchange(&ipc->editorResult,ok?1:-1); }
            if(auto cmd=InterlockedExchange(&ipc->stateCommand,0)) {
                std::lock_guard<std::mutex> lock(processing); bool ok=false;
                try { if(cmd==1) { auto bytes=plugin.state(); std::ofstream out(ipc->statePath,std::ios::binary); out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()); ok=bool(out); }
                    else { std::ifstream in(ipc->statePath,std::ios::binary); std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in),{}}; ok=plugin.state(bytes); }
                } catch(...) {} InterlockedExchange(&ipc->stateResult,ok?1:-1);
            }
            Sleep(1);
        }
        stop.store(true); audio.join(); if(wake) CloseHandle(wake); if(done) CloseHandle(done); CloseHandle(owner); UnmapViewOfFile(ipc); CloseHandle(mapping);
    } catch(const std::exception& e) { if(shared) { strncpy_s(shared->error,e.what(),_TRUNCATE); InterlockedExchange(&shared->phase,-1); } std::cerr<<e.what()<<"\n"; return 1; }
    CoUninitialize(); return 0;
}
