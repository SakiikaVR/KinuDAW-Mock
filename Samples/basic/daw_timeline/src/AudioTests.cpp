// Copyright (c) 2026 KinuDAW contributors. MIT License.
#include "AudioEngine.h"
#include "PluginIPC.h"
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
