#pragma once
#include <cstdint>

// Driver-free settings shared by the controller publisher and the web server.
namespace maptelemetry {
constexpr std::uint32_t PublishIntervalMs=200; // 5 Hz; leaves room under 500 ms
constexpr int DefaultPort=8765;
constexpr std::uint32_t MaxAuthFailuresPerMinute=10;
}
