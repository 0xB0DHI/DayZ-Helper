#pragma once
#include "game.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace game {
constexpr std::size_t JointCount=16;
struct BoneBinding {
    std::uint64_t entity=0,type=0,root=0,anim=0,array=0;
    std::uint32_t count=0;
    bool infected=false;
    std::array<std::uint32_t,JointCount> indices{};
};
struct BonePose {
    std::array<Vec2,JointCount> screen{};
    std::array<Vec3,JointCount> world{};
    std::array<bool,JointCount> visible{};
};

inline std::uint32_t BoneHash(const char* name) {
    std::uint32_t hash=0x1505u;
    for (const auto* p=reinterpret_cast<const unsigned char*>(name);*p;++p)
        hash=hash*0x21u+static_cast<std::uint32_t>(*p|0x20u);
    return hash;
}

template<class Reader> bool ResolveBones(Reader& reader,const Config& config,
                                          std::uint64_t entity,std::uint64_t type,
                                          bool infected,BoneBinding& result) {
    constexpr const char* names[]={"head","neck","spine","pelvis",
        "leftshoulder","leftforearm","lefthand","rightshoulder","rightforearm",
        "righthand","leftupleg","leftleg","leftfoot","rightupleg","rightleg","rightfoot"};
    static_assert(std::size(names)==JointCount);
    const char* rootKey=infected?"InfectedSkeletonCandidate":"PlayerSkeletonCandidate";
    BoneBinding pending{};pending.entity=entity;pending.type=type;pending.infected=infected;
    std::uint64_t data=0,map=0,controls=0,entries=0,source=0;
    std::uint32_t mapCount=0,capacity=0;
    std::uint64_t at=0;
    if (!PtrAt(reader,entity,config.Offset(rootKey),pending.root) ||
        !PtrAt(reader,pending.root,config.Offset("SkeletonAnimFromRoot"),pending.anim) ||
        !PtrAt(reader,pending.anim,config.Offset("AnimData"),data) ||
        !AddAddress(data,config.Offset("BoneMap"),map) ||
        !AddAddress(map,config.Offset("BoneMapCount"),at) || !reader.Read(at,mapCount) ||
        !AddAddress(map,config.Offset("BoneMapCapacity"),at) || !reader.Read(at,capacity) ||
        !PtrAt(reader,map,config.Offset("BoneMapControls"),controls) ||
        !PtrAt(reader,map,config.Offset("BoneMapEntries"),entries) ||
        !AddAddress(pending.anim,config.Offset("AnimBoneCount"),at) ||
        !reader.Read(at,pending.count) ||
        !PtrAt(reader,pending.root,config.Offset("SkeletonMatrixSourceCandidate"),source) ||
        source!=pending.anim ||
        !PtrAt(reader,source,config.Offset("SkeletonMatrixArrayCandidate"),pending.array) ||
        capacity<JointCount || capacity>512 || (capacity&(capacity-1)) ||
        mapCount<JointCount || mapCount>capacity || pending.count<JointCount ||
        pending.count>1024 || config.Offset("BoneMapEntryStride")!=8) return false;

    std::array<std::uint8_t,512> controlBytes{};
    std::array<std::uint8_t,4096> entryBytes{};
    if (!reader.ReadRaw(controls,controlBytes.data(),capacity) ||
        !reader.ReadRaw(entries,entryBytes.data(),static_cast<std::size_t>(capacity)*8)) return false;
    for (std::size_t joint=0;joint<JointCount;++joint) {
        const auto hash=BoneHash(names[joint]);
        std::uint32_t slot=(hash>>7)&(capacity-1);
        bool found=false;
        for (std::uint32_t attempt=0;attempt<capacity;++attempt) {
            if (controlBytes[slot]==0x80) break;
            std::uint32_t key=0,index=0;
            std::memcpy(&key,entryBytes.data()+static_cast<std::size_t>(slot)*8,4);
            std::memcpy(&index,entryBytes.data()+static_cast<std::size_t>(slot)*8+4,4);
            if (controlBytes[slot]==(hash&0x7fu) && key==hash) {
                if (index>=pending.count) return false;
                pending.indices[joint]=index;found=true;break;
            }
            slot=(slot+1)&(capacity-1);
        }
        if (!found) return false;
    }
    std::uint64_t rootAgain=0,animAgain=0,arrayAgain=0,entityType=0;
    std::uint64_t controlsAgain=0,entriesAgain=0;
    std::uint32_t countAgain=0,capacityAgain=0,mapCountAgain=0;
    if (!PtrAt(reader,entity,config.Offset("EntityType"),entityType) || entityType!=type ||
        !PtrAt(reader,entity,config.Offset(rootKey),rootAgain) || rootAgain!=pending.root ||
        !PtrAt(reader,pending.root,config.Offset("SkeletonAnimFromRoot"),animAgain) ||
        animAgain!=pending.anim ||
        !PtrAt(reader,pending.anim,config.Offset("SkeletonMatrixArrayCandidate"),arrayAgain) ||
        arrayAgain!=pending.array ||
        !AddAddress(pending.anim,config.Offset("AnimBoneCount"),at) ||
        !reader.Read(at,countAgain) || countAgain!=pending.count ||
        !AddAddress(map,config.Offset("BoneMapCapacity"),at) ||
        !reader.Read(at,capacityAgain) || capacityAgain!=capacity ||
        !AddAddress(map,config.Offset("BoneMapCount"),at) ||
        !reader.Read(at,mapCountAgain) || mapCountAgain!=mapCount ||
        !PtrAt(reader,map,config.Offset("BoneMapControls"),controlsAgain) ||
        controlsAgain!=controls ||
        !PtrAt(reader,map,config.Offset("BoneMapEntries"),entriesAgain) ||
        entriesAgain!=entries) return false;
    result=pending;return true;
}

template<class Reader> bool SampleBonePose(Reader& reader,const Config& config,
                                            const BoneBinding& binding,
                                            const Vec3& localPosition,float gate,
                                            const CameraSample& camera,float width,float height,
                                            BonePose& result,std::uint32_t* stage=nullptr) {
    if (stage) *stage=1;
    if (!binding.entity || !binding.array || gate<=0 ||
        config.Offset("BoneStride")!=0x30 ||
        config.Offset("BoneTranslationCandidate")!=0x24) return false;
    std::uint64_t state=0,at=0;
    std::array<float,12> transform{};
    if (!PtrAt(reader,binding.entity,config.Offset("EntityVisualStateCandidate"),state) ||
        !AddAddress(state,config.Offset("VisualStateMatrix"),at) ||
        !reader.ReadRaw(at,transform.data(),sizeof(transform))) return false;
    if (stage) *stage=2;
    const Vec3 origin{transform[9],transform[10],transform[11]};
    if (!Plausible(origin)) return false;
    const auto delta=Difference(origin,localPosition);
    const float distance2=Dot(delta,delta);
    if (!std::isfinite(distance2) || distance2>gate*gate) return false;
    if (stage) *stage=3;
    for (const auto value:transform) if (!std::isfinite(value)) return false;
    std::uint32_t lowest=binding.count,highest=0;
    for (const auto index:binding.indices) {
        if (index>=binding.count) return false;
        lowest=std::min(lowest,index);highest=std::max(highest,index);
    }
    const std::size_t span=static_cast<std::size_t>(highest-lowest+1)*0x30;
    if (span>8192) return false;
    if (stage) *stage=4;
    std::array<std::uint8_t,8192> matrices;
    for (std::size_t offset=0;offset<span;) {
        const auto bytes=std::min<std::size_t>(4032,span-offset);
        if (!AddAddress(binding.array,static_cast<std::uint64_t>(lowest)*0x30+offset,at) ||
            !reader.ReadRaw(at,matrices.data()+offset,bytes)) return false;
        offset+=bytes;
    }
    if (stage) *stage=5;
    BonePose pending{};
    for (std::size_t joint=0;joint<JointCount;++joint) {
        Vec3 local{};
        const auto offset=static_cast<std::size_t>(binding.indices[joint]-lowest)*0x30+0x24;
        std::memcpy(&local,matrices.data()+offset,sizeof(local));
        if (!Plausible(local) || std::abs(local.x)>5.0f ||
            std::abs(local.y)>5.0f || std::abs(local.z)>5.0f) return false;
        if (stage) *stage=6;
        const Vec3 world{local.x*transform[0]+local.y*transform[3]+local.z*transform[6]+transform[9],
                         local.x*transform[1]+local.y*transform[4]+local.z*transform[7]+transform[10],
                         local.x*transform[2]+local.y*transform[5]+local.z*transform[8]+transform[11]};
        if (!Plausible(world)) return false;
        pending.world[joint]=world;
        pending.visible[joint]=ProjectWorld(camera,world,width,height,pending.screen[joint]);
    }
    if (stage) *stage=7;
    std::uint64_t typeAgain=0,stateAgain=0,rootAgain=0,animAgain=0,arrayAgain=0;
    std::uint32_t countAgain=0;
    if (!PtrAt(reader,binding.entity,config.Offset("EntityType"),typeAgain) ||
        typeAgain!=binding.type ||
        !PtrAt(reader,binding.entity,config.Offset("EntityVisualStateCandidate"),stateAgain) ||
        stateAgain!=state) return false;
    const char* rootKey=binding.infected?"InfectedSkeletonCandidate":"PlayerSkeletonCandidate";
    if (!PtrAt(reader,binding.entity,config.Offset(rootKey),rootAgain) ||
        rootAgain!=binding.root ||
        !PtrAt(reader,binding.root,config.Offset("SkeletonAnimFromRoot"),animAgain) ||
        animAgain!=binding.anim ||
        !PtrAt(reader,binding.anim,config.Offset("SkeletonMatrixArrayCandidate"),arrayAgain) ||
        arrayAgain!=binding.array ||
        !AddAddress(binding.anim,config.Offset("AnimBoneCount"),at) ||
        !reader.Read(at,countAgain) || countAgain!=binding.count) return false;
    if (stage) *stage=8;
    result=pending;return true;
}
}
