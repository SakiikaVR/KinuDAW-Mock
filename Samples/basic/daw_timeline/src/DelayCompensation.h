// Copyright (c) 2026 KinuDAW contributors. MIT License.
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
namespace Kinu {
constexpr unsigned MaxPathLatency = 96000; // Two seconds at the engine's 48 kHz.
// Allocated with the engine, never resized by the renderer or device callback.
template<class T, unsigned Capacity = MaxPathLatency + 1> class SampleDelay {
public:
    void reset() { written_=0; }
    T push(T value, unsigned delay) {
        if(delay>=Capacity) throw std::runtime_error("Plugin latency exceeds the two-second compensation limit");
        auto index=written_%Capacity;
        buffer_[index]=value;
        T result=written_>=delay?buffer_[(written_-delay)%Capacity]:T{};
        ++written_; return result;
    }
private:
    std::array<T,Capacity> buffer_{};
    uint64_t written_=0;
};
}
