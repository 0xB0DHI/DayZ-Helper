#pragma once
#include "../config/config.h"
#include "../data/placeholder_reader.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace game {
struct Vec3 { float x, y, z; };
struct Vec2 { float x, y; };
struct CameraSample {
    std::uint64_t object = 0;
    Vec3 right{}, up{}, forward{}, position{};
    Vec2 viewportHalf{};
    float tanHorizontal = 0, tanVertical = 0;
};
struct LocalSample { std::uint64_t game = 0, link = 0, interior = 0, entity = 0; };
struct TableSample { std::uint64_t data = 0; std::uint32_t count = 0; std::vector<std::uint64_t> entries; };
struct HealthWatch {
    std::array<float,8> values{};
    std::array<bool,8> plausible{};
};
struct DeathWatch {
    std::array<std::uint32_t,3> words{};
    std::uint8_t byte=0;
};

template<class Reader> bool At(Reader& reader, std::uint64_t base, std::uint64_t offset,
                                std::uint64_t& value) {
    std::uint64_t address = 0;
    return AddAddress(base, offset, address) && reader.Read(address, value);
}
template<class Reader> bool PtrAt(Reader& reader, std::uint64_t base, std::uint64_t offset,
                                   std::uint64_t& value) {
    std::uint64_t candidate = 0;
    if (!At(reader, base, offset, candidate) || !UserRange(candidate, 1)) return false;
    value = candidate; return true;
}
template<class Reader> bool FetchLocal(Reader& reader, const Config& config,
                                       std::uint64_t module, LocalSample& result) {
    LocalSample pending{};
    if (!PtrAt(reader, module, config.Offset("GameWorldPointerRva"), pending.game) ||
        !PtrAt(reader, pending.game, config.Offset("GameLocalLink"), pending.link) ||
        !PtrAt(reader, pending.link, config.Offset("LocalLinkInterior"), pending.interior)) return false;
    const auto bias = config.Offset("LocalInteriorBias");
    if (pending.interior < bias) return false;
    pending.entity = pending.interior - bias;
    if (!UserRange(pending.entity, 1)) return false;
    std::uint64_t gameAgain = 0, linkAgain = 0, interiorAgain = 0;
    if (!PtrAt(reader, module, config.Offset("GameWorldPointerRva"), gameAgain) ||
        !PtrAt(reader, pending.game, config.Offset("GameLocalLink"), linkAgain) ||
        !PtrAt(reader, pending.link, config.Offset("LocalLinkInterior"), interiorAgain) ||
        gameAgain != pending.game || linkAgain != pending.link || interiorAgain != pending.interior) return false;
    result = pending;
    return true;
}

template<class Reader> bool FetchTable(Reader& reader, const Config& config, std::uint64_t game,
                                       std::uint64_t headerOffset, std::uint64_t countOffset,
                                       TableSample& result) {
    std::uint64_t header = 0, data = 0, dataAgain = 0;
    std::uint32_t count = 0, countAgain = 0;
    if (!AddAddress(game, headerOffset, header) ||
        !At(reader, header, config.Offset("TableData"), data)) return false;
    std::uint64_t address = 0;
    if (!AddAddress(header, countOffset, address) || !reader.Read(address, count) ||
        count > config.maxTableCount) return false;
    if (count && !UserRange(data, 1)) return false;
    TableSample pending{}; pending.data = data; pending.count = count;
    const auto selected = std::min(count, config.maxTableEntries);
    pending.entries.resize(selected);
    if (selected != 0) {
        const auto stride = config.Offset("TableStride");
        if (stride != sizeof(std::uint64_t) || selected > 4096 / sizeof(std::uint64_t) ||
            !reader.ReadRaw(data, pending.entries.data(), selected * sizeof(std::uint64_t))) return false;
        for (const auto entry : pending.entries) if (!UserRange(entry, 1)) return false;
    }
    if (!At(reader, header, config.Offset("TableData"), dataAgain) ||
        !reader.Read(address, countAgain) || dataAgain != data || countAgain != count) return false;
    result = std::move(pending); return true;
}

template<class Reader> bool ReadName(Reader& reader, const Config& config, std::uint64_t entity,
                                     std::string& result) {
    std::uint64_t type = 0, stringObject = 0, textAddress = 0, typeAgain = 0;
    if (!PtrAt(reader, entity, config.Offset("EntityType"), type) ||
        !PtrAt(reader, type, config.Offset("TypeName"), stringObject) ||
        !AddAddress(stringObject, config.Offset("StringChars"), textAddress)) return false;
    std::array<char, 64> chars{};
    if (!reader.ReadRaw(textAddress, chars.data(), chars.size())) return false;
    const auto terminator = std::find(chars.begin(), chars.end(), '\0');
    if (terminator == chars.end() || terminator == chars.begin()) return false;
    for (auto it = chars.begin(); it != terminator; ++it)
        if (static_cast<unsigned char>(*it) < 0x20 || static_cast<unsigned char>(*it) > 0x7E) return false;
    if (!PtrAt(reader, entity, config.Offset("EntityType"), typeAgain) || typeAgain != type) return false;
    result.assign(chars.begin(), terminator);
    return true;
}
template<class Reader> bool ReadTextObject(Reader& reader,const Config& config,
                                           std::uint64_t object,std::string& result) {
    std::uint64_t address=0;
    std::array<char,64> chars{};
    if (!AddAddress(object,config.Offset("StringChars"),address) ||
        !reader.ReadRaw(address,chars.data(),chars.size())) return false;
    const auto end=std::find(chars.begin(),chars.end(),'\0');
    if (end==chars.end() || end==chars.begin()) return false;
    for (auto it=chars.begin();it!=end;++it)
        if (static_cast<unsigned char>(*it)<0x20 || static_cast<unsigned char>(*it)>0x7E) return false;
    result.assign(chars.begin(),end);return true;
}
template<class Reader> bool ReadTypeText(Reader& reader,const Config& config,
                                         std::uint64_t type,const char* field,std::string& result) {
    std::uint64_t object=0;
    return PtrAt(reader,type,config.Offset(field),object) &&
           ReadTextObject(reader,config,object,result);
}

inline bool Plausible(Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::abs(value.x) < 1000000.0f && std::abs(value.y) < 1000000.0f &&
           std::abs(value.z) < 1000000.0f;
}
template<class Reader> bool CandidatePosition(Reader& reader, const Config& config,
                                               std::uint64_t entity, Vec3& result) {
    std::uint64_t state = 0, stateAgain = 0, address = 0;
    if (!PtrAt(reader, entity, config.Offset("EntityVisualStateCandidate"), state) ||
        !AddAddress(state, config.Offset("VisualStatePositionCandidate"), address)) return false;
    Vec3 candidate{};
    if (!reader.Read(address, candidate) || !Plausible(candidate) ||
        !PtrAt(reader, entity, config.Offset("EntityVisualStateCandidate"), stateAgain) ||
        stateAgain != state) return false;
    result = candidate; return true;
}
template<class Reader> bool CandidatePositionWithType(Reader& reader,const Config& config,
                                                       std::uint64_t entity,std::uint64_t expectedType,
                                                       Vec3& result) {
    const auto typeOffset=config.Offset("EntityType");
    const auto stateOffset=config.Offset("EntityVisualStateCandidate");
    if (stateOffset<typeOffset || stateOffset-typeOffset>56) return false;
    const auto span=static_cast<std::size_t>(stateOffset-typeOffset+sizeof(std::uint64_t));
    std::uint64_t at=0;
    std::array<unsigned char,64> raw{};
    if (!AddAddress(entity,typeOffset,at) || !reader.ReadRaw(at,raw.data(),span)) return false;
    std::uint64_t type=0,state=0,stateAgain=0,positionAt=0;
    std::memcpy(&type,raw.data(),sizeof(type));
    std::memcpy(&state,raw.data()+stateOffset-typeOffset,sizeof(state));
    if (type!=expectedType || !UserRange(state,1) ||
        !AddAddress(state,config.Offset("VisualStatePositionCandidate"),positionAt)) return false;
    Vec3 pending{};
    if (!reader.Read(positionAt,pending) || !Plausible(pending) ||
        !PtrAt(reader,entity,stateOffset,stateAgain) || stateAgain!=state) return false;
    result=pending;return true;
}
template<class Reader> bool CandidateInfectedAlive(Reader& reader, const Config& config,
                                                    std::uint64_t entity) {
    std::uint64_t address=0;
    std::uint8_t state=0;
    return AddAddress(entity,config.Offset("EntityDeadCandidate"),address) &&
           reader.Read(address,state) && (state & 0x04u)==0;
}
template<class Reader> bool CandidatePlayerAlive(Reader& reader,const Config& config,
                                                  std::uint64_t entity) {
    std::uint64_t address=0;
    std::uint8_t state=0;
    return AddAddress(entity,config.Offset("EntityDeadCandidate"),address) &&
           reader.Read(address,state) && (state & 0x04u)==0;
}
template<class Reader> bool SampleHealthWatch(Reader& reader,const Config& config,
                                                std::uint64_t entity,HealthWatch& result) {
    constexpr const char* keys[]={"HealthWatch1","HealthWatch2","HealthWatch3","HealthWatch4",
                                  "HealthWatch5","HealthWatch6","HealthWatch7","HealthWatch8"};
    HealthWatch pending{};
    std::uint64_t type=0,again=0;
    if (!PtrAt(reader,entity,config.Offset("EntityType"),type)) return false;
    for (std::size_t i=0;i<pending.values.size();++i) {
        const auto offset=config.Offset(keys[i]);
        std::uint64_t address=0;
        if (!offset || (offset&3) || offset>8192 || !AddAddress(entity,offset,address) ||
            !reader.Read(address,pending.values[i])) return false;
        const float value=pending.values[i];
        pending.plausible[i]=std::isfinite(value) && value>=0.0f && value<=5000.0f;
    }
    if (!PtrAt(reader,entity,config.Offset("EntityType"),again) || again!=type) return false;
    result=pending;return true;
}
template<class Reader> bool SampleDeathWatch(Reader& reader,const Config& config,
                                               std::uint64_t entity,DeathWatch& result) {
    constexpr const char* keys[]={"DeathWatchWord1","DeathWatchWord2","DeathWatchWord3"};
    DeathWatch pending{};
    std::uint64_t type=0,again=0;
    if (!PtrAt(reader,entity,config.Offset("EntityType"),type)) return false;
    for (std::size_t i=0;i<pending.words.size();++i) {
        const auto offset=config.Offset(keys[i]);
        std::uint64_t address=0;
        if (!offset || (offset&3) || offset>8192 || !AddAddress(entity,offset,address) ||
            !reader.Read(address,pending.words[i])) return false;
    }
    const auto byteOffset=config.Offset("DeathWatchByte1");
    std::uint64_t address=0;
    if (!byteOffset || byteOffset>8192 || !AddAddress(entity,byteOffset,address) ||
        !reader.Read(address,pending.byte) ||
        !PtrAt(reader,entity,config.Offset("EntityType"),again) || again!=type) return false;
    result=pending;return true;
}
template<class Reader> bool CandidateCamera(Reader& reader, const Config& config,
                                             std::uint64_t game, std::uint64_t& camera, Vec3& position) {
    std::uint64_t pending = 0, again = 0, address = 0;
    Vec3 p{};
    if (!PtrAt(reader, game, config.Offset("GameCamera"), pending) ||
        !AddAddress(pending, config.Offset("CameraPosition"), address) ||
        !reader.Read(address, p) || !Plausible(p) ||
        !PtrAt(reader, game, config.Offset("GameCamera"), again) || again != pending) return false;
    camera = pending; position = p; return true;
}

inline float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 Difference(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
template<class Reader> bool FetchCameraSample(Reader& reader, const Config& config,
                                               std::uint64_t game, CameraSample& result) {
    std::uint64_t camera=0,again=0;
    if (!PtrAt(reader,game,config.Offset("GameCamera"),camera)) return false;
    const auto size=config.Offset("CameraSampleBytes");
    std::array<unsigned char,512> raw{};
    if (size==0 || size>raw.size() || !reader.ReadRaw(camera,raw.data(),static_cast<std::size_t>(size))) return false;
    auto field=[&raw,size](std::uint64_t offset,auto& value) {
        if (offset>size || sizeof(value)>size-offset) return false;
        std::memcpy(&value,raw.data()+offset,sizeof(value));return true;
    };
    CameraSample pending{};pending.object=camera;
    if (!field(config.Offset("CameraRightCandidate"),pending.right) ||
        !field(config.Offset("CameraUpCandidate"),pending.up) ||
        !field(config.Offset("CameraForwardCandidate"),pending.forward) ||
        !field(config.Offset("CameraPosition"),pending.position) ||
        !field(config.Offset("CameraViewportCandidate"),pending.viewportHalf) ||
        !field(config.Offset("CameraTanHorizontalCandidate"),pending.tanHorizontal) ||
        !field(config.Offset("CameraTanVerticalCandidate"),pending.tanVertical)) return false;
    if (!Plausible(pending.position) || !Plausible(pending.right) ||
        !Plausible(pending.up) || !Plausible(pending.forward) ||
        !std::isfinite(pending.viewportHalf.x) || !std::isfinite(pending.viewportHalf.y) ||
        !std::isfinite(pending.tanHorizontal) || !std::isfinite(pending.tanVertical) ||
        pending.viewportHalf.x<100 || pending.viewportHalf.y<100 ||
        pending.tanHorizontal<0.1f || pending.tanHorizontal>4 ||
        pending.tanVertical<0.1f || pending.tanVertical>4) return false;
    for (const Vec3 axis:{pending.right,pending.up,pending.forward})
        if (std::abs(Dot(axis,axis)-1.0f)>0.2f) return false;
    if (std::abs(Dot(pending.right,pending.up))>0.2f ||
        std::abs(Dot(pending.right,pending.forward))>0.2f ||
        std::abs(Dot(pending.up,pending.forward))>0.2f) return false;
    if (!PtrAt(reader,game,config.Offset("GameCamera"),again) || again!=camera) return false;
    result=pending;return true;
}
inline bool ProjectWorld(const CameraSample& camera, Vec3 world, float width, float height, Vec2& result) {
    if (!Plausible(world) || !std::isfinite(width) || !std::isfinite(height) ||
        width<=0 || height<=0 || camera.tanHorizontal<=0 || camera.tanVertical<=0 ||
        camera.viewportHalf.y<=0 ||
        std::abs(camera.viewportHalf.x/camera.viewportHalf.y-width/height)>0.15f) return false;
    const Vec3 relative=Difference(world,camera.position);
    const float depth=Dot(relative,camera.forward);
    if (!std::isfinite(depth) || depth<=0.1f) return false;
    const float x=width*0.5f+Dot(relative,camera.right)*width*0.5f/(depth*camera.tanHorizontal);
    const float y=height*0.5f-Dot(relative,camera.up)*height*0.5f/(depth*camera.tanVertical);
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>width || y<0 || y>height) return false;
    result={x,y};return true;
}
}
