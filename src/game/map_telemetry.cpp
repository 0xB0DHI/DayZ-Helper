#include "map_telemetry.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace maptelemetry {
namespace {
bool Finite(float v) { return std::isfinite(v) && std::abs(v)<1000000.0f; }
bool Valid(const Snapshot& snapshot) {
    const auto& h=snapshot.header;
    if (h.magic!=Magic || h.version!=Version || h.count!=snapshot.points.size() ||
        snapshot.points.size()>MaxPoints || !Finite(h.localX) || !Finite(h.localY) ||
        !Finite(h.localZ) || !Finite(h.headingX) || !Finite(h.headingZ) ||
        !std::memchr(h.terrain,0,sizeof(h.terrain)) ||
        !std::memchr(h.localHeld,0,sizeof(h.localHeld))) return false;
    std::array<std::uint32_t,5> counts{};
    for (const auto& p:snapshot.points) {
        if (p.kind>static_cast<std::uint8_t>(Kind::Loot) || p.category>7 ||
            p.life>static_cast<std::uint8_t>(Life::Dead) ||
            !Finite(p.x) || !Finite(p.y) || !Finite(p.z) ||
            !Finite(p.distance) || !Finite(p.headingX) || !Finite(p.headingZ) ||
            !std::memchr(p.name,0,sizeof(p.name)) ||
            !std::memchr(p.held,0,sizeof(p.held))) return false;
        ++counts[p.kind];
    }
    return counts==h.counts;
}
}
bool Encode(const Snapshot& snapshot,std::vector<std::uint8_t>& bytes) {
    if (!Valid(snapshot)) return false;
    const auto size=sizeof(Header)+snapshot.points.size()*sizeof(Point);
    if (size>MaxPacketBytes || size>std::numeric_limits<std::uint32_t>::max()) return false;
    std::vector<std::uint8_t> pending(size);
    std::memcpy(pending.data(),&snapshot.header,sizeof(Header));
    if (!snapshot.points.empty())
        std::memcpy(pending.data()+sizeof(Header),snapshot.points.data(),
            snapshot.points.size()*sizeof(Point));
    bytes.swap(pending);
    return true;
}
bool Decode(const std::uint8_t* bytes,std::size_t length,Snapshot& snapshot) {
    if (!bytes || length<sizeof(Header) || length>MaxPacketBytes) return false;
    Header header{};std::memcpy(&header,bytes,sizeof(header));
    if (header.magic!=Magic || header.version!=Version || header.count>MaxPoints ||
        length!=sizeof(Header)+static_cast<std::size_t>(header.count)*sizeof(Point)) return false;
    Snapshot pending{};pending.header=header;pending.points.resize(header.count);
    if (header.count)
        std::memcpy(pending.points.data(),bytes+sizeof(Header),
            static_cast<std::size_t>(header.count)*sizeof(Point));
    if (!Valid(pending)) return false;
    snapshot=std::move(pending);
    return true;
}
std::uint64_t EphemeralId(std::uint64_t source,std::uint64_t salt) {
    std::uint64_t x=source^salt;
    x+=0x9e3779b97f4a7c15ull;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ull;
    x=(x^(x>>27))*0x94d049bb133111ebull;
    return x^(x>>31);
}
}
