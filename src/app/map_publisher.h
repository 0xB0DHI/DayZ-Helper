#pragma once
#include "../game/map_telemetry.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class MapPublisher {
public:
    MapPublisher()=default;
    ~MapPublisher() { Stop(); }
    MapPublisher(const MapPublisher&)=delete;
    MapPublisher& operator=(const MapPublisher&)=delete;
    bool Start();
    void Stop();
    bool Connected() const { return connected_.load(std::memory_order_acquire); }
    void Submit(maptelemetry::Snapshot& snapshot);
private:
    void Run();
    std::atomic<bool> stop_{false},connected_{false};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    maptelemetry::Snapshot pending_{};
    bool hasPending_=false;
};
