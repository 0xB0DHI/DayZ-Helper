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
    float mainTanHorizontal = 0, mainTanVertical = 0;
    float opticFov = 0, pipLensRadius = 0;
};
enum class OpticProjection { Main, PictureInPicture, Invalid };
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

// The loaded CfgWorlds name is an inline, NUL-terminated module global in
// DayZ 1.30.164200.56. Only publish names with a corresponding map image.
template<class Reader> bool FetchTerrainName(Reader& reader, const Config& config,
                                              std::uint64_t module, char (&result)[32]) {
    const auto offset=config.Offset("CurrentWorldNameRva");
    std::uint64_t address=0;
    if (!offset || !AddAddress(module,offset,address) || !UserRange(address,32)) return false;
    std::array<char,32> raw{};
    if (!reader.ReadRaw(address,raw.data(),raw.size())) return false;
    std::size_t length=0;
    while (length<raw.size() && raw[length]) ++length;
    if (!length || length==raw.size()) return false;
    for (std::size_t i=0;i<length;++i) {
        if (raw[i]>='A' && raw[i]<='Z') raw[i]=static_cast<char>(raw[i]-'A'+'a');
        if ((raw[i]<'a' || raw[i]>'z') && (raw[i]<'0' || raw[i]>'9')) return false;
    }
    const bool supported=std::strcmp(raw.data(),"chernarusplus")==0 ||
        std::strcmp(raw.data(),"namalsk")==0 ||
        std::strcmp(raw.data(),"nasdara")==0;
    if (!supported) return false;
    std::memcpy(result,raw.data(),length+1);
    return true;
}

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
                                                       Vec3& result,Vec3* facing=nullptr) {
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
    Vec3 pending{},pendingFacing{};
    const auto matrixOffset=facing?config.Offset("VisualStateMatrix"):0;
    const auto positionOffset=config.Offset("VisualStatePositionCandidate");
    if (facing && positionOffset>=matrixOffset &&
        positionOffset-matrixOffset==9*sizeof(float)) {
        // The visual-state position is the final three floats of this matrix.
        // Reading its complete span replaces, rather than adds to, the position
        // request on a map-publish frame. The basis is never reused across frames.
        std::uint64_t matrixAt=0;
        std::array<float,12> matrix{};
        if (!AddAddress(state,matrixOffset,matrixAt) ||
            !reader.ReadRaw(matrixAt,matrix.data(),sizeof(matrix))) return false;
        pending={matrix[9],matrix[10],matrix[11]};
        const float horizontal=matrix[6]*matrix[6]+matrix[8]*matrix[8];
        const float full=horizontal+matrix[7]*matrix[7];
        if (std::isfinite(horizontal) && std::isfinite(full) &&
            horizontal>0.01f && full>0.64f && full<1.44f) {
            const float inverse=1.0f/std::sqrt(horizontal);
            pendingFacing={matrix[6]*inverse,0,matrix[8]*inverse};
        }
    } else if (!reader.Read(positionAt,pending)) return false;
    if (!Plausible(pending) ||
        !PtrAt(reader,entity,stateOffset,stateAgain) || stateAgain!=state) return false;
    result=pending;
    if (facing) *facing=pendingFacing;
    return true;
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
        pending.tanHorizontal<=std::numeric_limits<float>::epsilon() ||
        pending.tanHorizontal>4 ||
        pending.tanVertical<=std::numeric_limits<float>::epsilon() ||
        pending.tanVertical>4) return false;
    for (const Vec3 axis:{pending.right,pending.up,pending.forward})
        if (std::abs(Dot(axis,axis)-1.0f)>0.2f) return false;
    if (std::abs(Dot(pending.right,pending.up))>0.2f ||
        std::abs(Dot(pending.right,pending.forward))>0.2f ||
        std::abs(Dot(pending.up,pending.forward))>0.2f) return false;
    if (!PtrAt(reader,game,config.Offset("GameCamera"),again) || again!=camera) return false;
    result=pending;return true;
}
inline bool SetPictureInPictureProjection(CameraSample& camera,float fov,const Config& config) {
    if (!std::isfinite(fov) || fov<=std::numeric_limits<float>::epsilon() || fov>4.0f ||
        camera.tanHorizontal<=std::numeric_limits<float>::epsilon() ||
        camera.tanVertical<=std::numeric_limits<float>::epsilon() ||
        config.pipLensRadiusPercent<20 || config.pipLensRadiusPercent>60 ||
        config.pipHorizontalScalePercent<70 || config.pipHorizontalScalePercent>110)
        return false;
    CameraSample pending=camera;
    pending.mainTanHorizontal=camera.tanHorizontal;
    pending.mainTanVertical=camera.tanVertical;
    pending.opticFov=fov;
    pending.pipLensRadius=static_cast<float>(config.pipLensRadiusPercent)*0.01f;
    pending.tanVertical=fov*(camera.tanVertical/camera.tanHorizontal);
    pending.tanHorizontal=fov/(static_cast<float>(config.pipHorizontalScalePercent)*0.01f);
    if (!std::isfinite(pending.tanHorizontal) || !std::isfinite(pending.tanVertical) ||
        pending.tanHorizontal<=std::numeric_limits<float>::epsilon() ||
        pending.tanVertical<=std::numeric_limits<float>::epsilon()) return false;
    camera=pending;
    return true;
}
template<class Reader> OpticProjection ApplyActiveOpticProjection(
    Reader& reader,const Config& config,std::uint64_t localEntity,CameraSample& camera) {
    std::uint64_t playerInventory=0,hands=0,weaponInventory=0,headerAddress=0;
    if (!PtrAt(reader,localEntity,config.Offset("EntityInventory"),playerInventory) ||
        !PtrAt(reader,playerInventory,config.Offset("InventoryHandsCandidate"),hands) ||
        !PtrAt(reader,hands,config.Offset("EntityInventory"),weaponInventory) ||
        !AddAddress(weaponInventory,config.Offset("OpticAttachmentEntries"),headerAddress))
        return OpticProjection::Main;
    const auto countOffset=config.Offset("OpticAttachmentCount");
    const auto entriesOffset=config.Offset("OpticAttachmentEntries");
    const auto stride=config.Offset("OpticAttachmentStride");
    const auto itemOffset=config.Offset("OpticAttachmentEntity");
    if (countOffset<entriesOffset || countOffset-entriesOffset>12 ||
        stride!=16 || itemOffset>8) return OpticProjection::Invalid;
    std::array<unsigned char,16> header{};
    if (!reader.ReadRaw(headerAddress,header.data(),header.size()))
        return OpticProjection::Invalid;
    std::uint64_t entries=0;
    std::uint32_t count=0;
    std::memcpy(&entries,header.data(),sizeof(entries));
    std::memcpy(&count,header.data()+countOffset-entriesOffset,sizeof(count));
    if (count==0) return OpticProjection::Main;
    if (count>32 || !UserRange(entries,count*stride)) return OpticProjection::Invalid;
    std::array<unsigned char,512> list{};
    if (!reader.ReadRaw(entries,list.data(),count*stride)) return OpticProjection::Invalid;
    for (std::uint32_t i=0;i<count;++i) {
        std::uint64_t optic=0;
        std::memcpy(&optic,list.data()+i*stride+itemOffset,sizeof(optic));
        if (!UserRange(optic,1)) continue;
        const auto stateOffset=config.Offset("OpticCurrentStepFov");
        const auto activeOffset=config.Offset("OpticIsInOptics");
        const auto overrideOffset=config.Offset("OpticWeaponOverride");
        if (activeOffset<stateOffset || overrideOffset<activeOffset ||
            overrideOffset-stateOffset>=32) return OpticProjection::Invalid;
        std::array<unsigned char,32> state{};
        std::uint64_t stateAt=0;
        if (!AddAddress(optic,stateOffset,stateAt) ||
            !reader.ReadRaw(stateAt,state.data(),overrideOffset-stateOffset+1)) continue;
        if (state[activeOffset-stateOffset]!=1 || state[overrideOffset-stateOffset]!=0) continue;
        std::uint32_t step=0;
        std::memcpy(&step,state.data(),sizeof(step));
        std::uint64_t type=0,typeAt=0;
        if (!PtrAt(reader,optic,config.Offset("EntityType"),type) ||
            !AddAddress(type,config.Offset("OpticModel2D"),typeAt))
            return OpticProjection::Invalid;
        const auto modelOffset=config.Offset("OpticModel2D");
        const auto fovInitOffset=config.Offset("OpticZoomInit");
        const auto stepsOffset=config.Offset("OpticStepFovData");
        const auto stepCountOffset=config.Offset("OpticStepFovCount");
        if (fovInitOffset<modelOffset || stepsOffset<fovInitOffset ||
            stepCountOffset<stepsOffset || stepCountOffset-modelOffset+4>132)
            return OpticProjection::Invalid;
        std::array<unsigned char,132> typeRaw{};
        if (!reader.ReadRaw(typeAt,typeRaw.data(),typeRaw.size()))
            return OpticProjection::Invalid;
        std::uint64_t model=0,stepArray=0;
        std::uint32_t stepCount=0;
        float fov=0;
        std::memcpy(&model,typeRaw.data(),sizeof(model));
        std::memcpy(&fov,typeRaw.data()+fovInitOffset-modelOffset,sizeof(fov));
        std::memcpy(&stepArray,typeRaw.data()+stepsOffset-modelOffset,sizeof(stepArray));
        std::memcpy(&stepCount,typeRaw.data()+stepCountOffset-modelOffset,sizeof(stepCount));
        if (fov==0.0f && stepCount==0) continue;
        // A configured 2D optics model already uses the main camera projection.
        if (model) {
            std::uint64_t textAt=0;
            std::array<char,2> modelPrefix{};
            if (!AddAddress(model,config.Offset("StringChars"),textAt) ||
                !reader.ReadRaw(textAt,modelPrefix.data(),modelPrefix.size()))
                return OpticProjection::Invalid;
            if (modelPrefix[0]!='-' || modelPrefix[1]!='\0')
                return OpticProjection::Main;
        }
        if (stepCount) {
            if (stepCount>32 || step>=stepCount ||
                !UserRange(stepArray,static_cast<std::size_t>(stepCount)*sizeof(float)) ||
                !reader.Read(stepArray+static_cast<std::uint64_t>(step)*sizeof(float),fov))
                return OpticProjection::Invalid;
        }
        if (!std::isfinite(fov) || fov<=std::numeric_limits<float>::epsilon() ||
            fov>4.0f) return OpticProjection::Invalid;
        std::uint64_t handsAgain=0,weaponInventoryAgain=0,typeAgain=0;
        std::array<unsigned char,16> headerAgain{};
        std::array<unsigned char,32> stateAgain{};
        if (!PtrAt(reader,playerInventory,config.Offset("InventoryHandsCandidate"),handsAgain) ||
            handsAgain!=hands ||
            !PtrAt(reader,hands,config.Offset("EntityInventory"),weaponInventoryAgain) ||
            weaponInventoryAgain!=weaponInventory ||
            !reader.ReadRaw(headerAddress,headerAgain.data(),headerAgain.size()) ||
            headerAgain!=header ||
            !PtrAt(reader,optic,config.Offset("EntityType"),typeAgain) ||
            typeAgain!=type ||
            !reader.ReadRaw(stateAt,stateAgain.data(),overrideOffset-stateOffset+1) ||
            stateAgain[activeOffset-stateOffset]!=1 ||
            stateAgain[overrideOffset-stateOffset]!=0 ||
            std::memcmp(state.data(),stateAgain.data(),sizeof(step))!=0)
            return OpticProjection::Invalid;
        CameraSample pending=camera;
        if (!SetPictureInPictureProjection(pending,fov,config))
            return OpticProjection::Invalid;
        camera=pending;
        return OpticProjection::PictureInPicture;
    }
    return OpticProjection::Main;
}
inline CameraSample SelectPictureInPictureView(const CameraSample& camera,bool mainView) {
    CameraSample selected=camera;
    if (mainView && camera.mainTanHorizontal>0 && camera.mainTanVertical>0) {
        selected.tanHorizontal=camera.mainTanHorizontal;
        selected.tanVertical=camera.mainTanVertical;
    }
    selected.mainTanHorizontal=0;
    selected.mainTanVertical=0;
    selected.pipLensRadius=0;
    return selected;
}
inline bool ProjectWorld(const CameraSample& camera, Vec3 world, float width, float height,
                         Vec2& result,bool* mainView=nullptr) {
    if (!Plausible(world) || !std::isfinite(width) || !std::isfinite(height) ||
        width<=0 || height<=0 ||
        camera.tanHorizontal<=std::numeric_limits<float>::epsilon() ||
        camera.tanVertical<=std::numeric_limits<float>::epsilon() ||
        camera.viewportHalf.y<=0 ||
        std::abs(camera.viewportHalf.x/camera.viewportHalf.y-width/height)>0.15f) return false;
    const Vec3 relative=Difference(world,camera.position);
    const float depth=Dot(relative,camera.forward);
    if (!std::isfinite(depth) || depth<=0.1f) return false;
    const float horizontal=Dot(relative,camera.right)*width*0.5f/depth;
    const float vertical=Dot(relative,camera.up)*height*0.5f/depth;
    float x=width*0.5f+horizontal/camera.tanHorizontal;
    float y=height*0.5f-vertical/camera.tanVertical;
    bool outside=false;
    if (camera.mainTanHorizontal>0 && camera.mainTanVertical>0 && camera.pipLensRadius>0) {
        const float dx=x-width*0.5f,dy=y-height*0.5f;
        const float radius=height*camera.pipLensRadius;
        outside=dx*dx+dy*dy>radius*radius;
        if (outside) {
            x=width*0.5f+horizontal/camera.mainTanHorizontal;
            y=height*0.5f-vertical/camera.mainTanVertical;
        }
    }
    if (!std::isfinite(x) || !std::isfinite(y) || x<0 || x>width || y<0 || y>height) return false;
    if (mainView) *mainView=outside;
    result={x,y};return true;
}
}
