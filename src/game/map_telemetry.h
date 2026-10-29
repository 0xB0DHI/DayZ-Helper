#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// Driver-free wire contract shared by the controller and map telemetry server.
// Values are copied only from a completed controller frame. No target addresses
// or driver protocol types are part of this contract.
namespace maptelemetry {
constexpr std::uint32_t Magic=0x50414D44u; // DMAP
constexpr std::uint16_t Version=2;
constexpr std::size_t MaxPoints=16384;
constexpr std::size_t MaxPacketBytes=4u*1024u*1024u;
constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\DayZMapTelemetryV2";

enum class Kind : std::uint8_t { Survivor, Infected, Animal, Vehicle, Loot };
enum class Life : std::uint8_t { Unknown, Alive, Dead };

#pragma pack(push,1)
struct Point {
    std::uint64_t id=0; // ephemeral, salted ID; never a target address
    float x=0,y=0,z=0,distance=0;
    float headingX=0,headingZ=0;
    std::uint8_t kind=0,category=0,life=0,quality=255;
    char name[96]{};
    char held[64]{}; // current-frame clean name; empty if unavailable
};
struct Header {
    std::uint32_t magic=Magic;
    std::uint16_t version=Version;
    std::uint16_t flags=0; // bit 0: local valid, bit 1: camera heading valid
    std::uint64_t sequence=0;
    std::uint64_t utcMilliseconds=0;
    std::uint32_t count=0;
    std::array<std::uint32_t,5> counts{};
    float localX=0,localY=0,localZ=0;
    float headingX=0,headingZ=0;
    char terrain[32]{};
    char localHeld[64]{};
};
#pragma pack(pop)

struct Snapshot {
    Header header{};
    std::vector<Point> points;
};

bool Encode(const Snapshot& snapshot,std::vector<std::uint8_t>& bytes);
bool Decode(const std::uint8_t* bytes,std::size_t length,Snapshot& snapshot);
std::uint64_t EphemeralId(std::uint64_t source,std::uint64_t salt);
}
