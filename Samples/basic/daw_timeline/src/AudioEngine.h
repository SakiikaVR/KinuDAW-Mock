// Copyright (c) 2026 KinuDAW contributors. MIT License.
#pragma once
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Kinu {
constexpr int Tracks = 32, Block = 128, MaxClips = 256;
struct PluginInfo { std::string path, uid, name, category, vendor; };
struct Note { int pitch = 60, velocity = 100; double beat = 0, duration = 1; };
struct AutomationPoint { double beat = 0; float db = 0; };
struct AudioFile { std::string path; std::vector<float> samples; };
struct RenderClip { int track = 0; double beat = 0, length = 4, offset = 0; const AudioFile* audio = nullptr; std::array<Note, 128> notes{}; int noteCount = 0; double pattern = 16; };
struct Channel { float gain = 1, pan = 0; bool mute = false, solo = false; bool effects[3]{}; std::array<AutomationPoint,64> automation{}; int automationCount=0; };
struct Scene { std::array<RenderClip, MaxClips> clips{}; int count = 0; std::array<Channel, Tracks> channels{}; double bpm = 124, end = 128, loopStart = 0, loopEnd = 4; bool loop = false; };
class RemotePlugin;
class AudioEngine {
public:
    AudioEngine(); ~AudioEngine();
    bool start(std::string& error); void stop();
    unsigned bufferFrames() const;
    unsigned nativeSampleRate() const;
    std::string deviceName() const;
    bool configure(unsigned frames,bool exclusive,std::string& error);
    bool beginRecording(const std::filesystem::path&, std::string& error);
    std::filesystem::path endRecording(std::string& error);
    void publish(const Scene& scene);
    void transport(bool play, double beat);
    double position() const { return deviceActive_.load()?audiblePosition_.load():position_.load(); }
    float peak(int track) const { return peaks_[track].load(); }
    float waveSample(int side,int index) const { return waveform_[side][index].load(std::memory_order_relaxed); }
    const AudioFile* import(const std::filesystem::path& path, std::string& error);
    bool loadPlugin(int track, const PluginInfo& info, std::string& error, int slot=0);
    bool unloadPlugin(int track,std::string& error,int slot=0);
    bool editor(int track, std::string& error, int slot=0);
    bool hasPlugin(int track, int slot=0) const;
    bool pluginHealthy(int track, int slot=0) const;
    unsigned underruns() const;
    void midi(int track, int pitch, int velocity, bool on);
    std::vector<unsigned char> pluginState(int track, int slot=0);
    bool restorePluginState(int track, const std::vector<unsigned char>& bytes, int slot=0);
    void render(float* output, unsigned frames);
    bool exportWav(const std::filesystem::path& path, double beats, std::string& error);
    const PluginInfo* pluginInfo(int track, int slot=0) const;
private:
    struct Device; std::unique_ptr<Device> device_;
    struct Effects; std::unique_ptr<Effects> effects_;
    std::array<std::unique_ptr<RemotePlugin>, Tracks> plugins_;
    std::array<std::array<std::unique_ptr<RemotePlugin>,3>,Tracks> effectsPlugins_;
    std::vector<std::unique_ptr<AudioFile>> media_;
    std::array<Scene, 3> scenes_{};
    std::atomic<int> published_{0}, reading_{-1};
    std::atomic<double> position_{0}, seek_{0};
    std::atomic<double> audiblePosition_{0}; std::atomic<bool> deviceActive_{false};
    std::array<double,Block> renderedBeats_{};
    std::array<std::atomic<bool>,Tracks> pluginFault_{};
    bool exporting_=false;
    unsigned requestedFrames_=Block; bool exclusive_=false;
    std::atomic<unsigned> seekVersion_{0}; unsigned appliedSeek_ = 0;
    std::atomic<bool> playing_{false};
    std::array<std::atomic<float>, Tracks> peaks_{};
    std::array<std::array<std::atomic<float>,Block>,2> waveform_{};
    struct Midi { int track, pitch, velocity; bool on; };
    std::array<Midi, 1024> midiQueue_{}; std::atomic<unsigned> midiWrite_{0}, midiRead_{0};
    std::atomic<bool> midiPanic_{false};
    std::array<std::array<bool,128>,Tracks> held_{};
    std::array<std::array<double,128>,Tracks> phases_{};
    double beat_ = 0; bool wasPlaying_ = false;
};
std::vector<PluginInfo> scanModule(const std::string& path, std::string& error);
std::vector<std::string> modulePaths();
}
