#include "scene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace {
bool ReadSourceHeader(PlaceholderReader& reader,const Config& config,std::uint64_t header,
                      std::uint64_t countOffset,std::uint64_t& data,std::uint32_t& count) {
    const auto dataOffset=config.Offset("TableData");
    if (dataOffset==0 && (countOffset==8 || countOffset==12)) {
        std::array<unsigned char,16> raw{};
        if (!reader.ReadRawGrouped(header,raw.data(),
                static_cast<std::size_t>(countOffset+sizeof(count)),2))
            return false;
        std::memcpy(&data,raw.data(),sizeof(data));
        std::memcpy(&count,raw.data()+countOffset,sizeof(count));
        return true;
    }
    std::uint64_t countAt=0;
    return game::At(reader,header,dataOffset,data) &&
           AddAddress(header,countOffset,countAt) && reader.Read(countAt,count);
}
bool ReadHeldName(PlaceholderReader& reader,const Config& config,std::uint64_t player,
                  std::string& result) {
    std::uint64_t inventory=0,hands=0,type=0,handsAgain=0;
    std::string pending;
    if (!game::PtrAt(reader,player,config.Offset("EntityInventory"),inventory) ||
        !game::PtrAt(reader,inventory,config.Offset("InventoryHandsCandidate"),hands) ||
        !game::PtrAt(reader,hands,config.Offset("EntityType"),type) ||
        !game::ReadTypeText(reader,config,type,"TypeCleanCandidate",pending) ||
        !game::PtrAt(reader,inventory,config.Offset("InventoryHandsCandidate"),handsAgain) ||
        handsAgain!=hands) return false;
    result=std::move(pending);return true;
}
float Distance(game::Vec3 a,game::Vec3 b) {
    const auto difference=game::Difference(a,b);
    return std::sqrt(game::Dot(difference,difference));
}
std::size_t ItemCellHash(int x,int y,int z) {
    return (static_cast<std::uint32_t>(x)*73856093u ^
            static_cast<std::uint32_t>(y)*19349663u ^
            static_cast<std::uint32_t>(z)*83492791u) & 8191u;
}
bool ItemsRequested(const Config& config) {
    return config.showItems && std::any_of(config.showItemCategory.begin(),
        config.showItemCategory.end(),[](bool enabled){return enabled;});
}
float BoxHeight(visual::EntityKind kind) {
    switch(kind) {
    case visual::EntityKind::Player: case visual::EntityKind::Infected:return 1.8f;
    case visual::EntityKind::Animal:return 1.1f;
    case visual::EntityKind::Vehicle:return 2.4f;
    default:return 1.0f;
    }
}
float BoxWidthRatio(visual::EntityKind kind) {
    return kind==visual::EntityKind::Vehicle?1.4f:
           kind==visual::EntityKind::Animal?0.8f:0.46f;
}
const char* KindName(visual::EntityKind kind) {
    switch(kind) {
    case visual::EntityKind::Player:return "player";
    case visual::EntityKind::Infected:return "infected";
    case visual::EntityKind::Animal:return "animal";
    case visual::EntityKind::Vehicle:return "vehicle";
    default:return "";
    }
}
}

SceneCache::SceneCache() {
    objects_.reserve(4096);identities_.reserve(64);itemCandidates_.reserve(4096);
    mapOnlyItems_.reserve(4096);mapPlayerPoints_.reserve(512);
    sourceSlots_.reserve(512);skeletonCandidates_.reserve(4096);
    itemOrder_.reserve(4096);gridNext_.reserve(4096);
    selectedItems_.reserve(4096);groupCounts_.reserve(4096);
    previousVisibleItems_.reserve(4096);
    itemGroups_.reserve(4096);itemGroupOf_.reserve(4096);
    itemGroupHead_.reserve(8192);itemGroupNext_.reserve(4096);
}
void SceneCache::Clear() {
    world_=local_=nearestPlayer_=0;objects_.clear();identities_.clear();maxObserved_.fill(0);
    localPosition_={};
    heldWeapon_={};muzzleBinding_={};localVelocityHistory_.Reset();
    localVelocity_={};localVelocityValid_=false;currentSampleFrame_=0;
    sources_.fill({});
    scanSource_=scanSlot_=scanTotal_=0;sourceLoaded_=false;sourceSlots_.clear();
    previousVisibleItems_.clear();mapOnlyItems_.clear();mapPlayerPoints_.clear();
    mapItemsRequested_=false;
}
void SceneCache::CarryItemWinnerHistory(SceneCache& next) {
    previousVisibleItems_.swap(next.previousVisibleItems_);
    if (world_!=next.world_) return;
    if (local_==next.local_) next.localVelocityHistory_=localVelocityHistory_;
    if (std::none_of(objects_.begin(),objects_.end(),
            [](const Object& object){return object.velocityHistory.entity!=0 || object.bonesReady;})) return;
    std::unordered_map<std::uint64_t,const Object*> old;
    old.reserve(objects_.size());
    for (const auto& object:objects_) old.emplace(object.entity,&object);
    for (auto& object:next.objects_) {
        const auto found=old.find(object.entity);
        if (found!=old.end() && found->second->type==object.type &&
            found->second->kind==object.kind &&
            found->second->netId==object.netId) {
            object.velocityHistory=found->second->velocityHistory;
            if (!object.bonesReady && found->second->bonesReady) {
                object.bones=found->second->bones;
                object.bonesReady=true;
            }
        }
    }
}
bool SceneCache::LoadSource(PlaceholderReader& reader,const Config& config,std::uint32_t sourceIndex) {
    Source source{};
    if (sourceIndex<5) {
        constexpr const char* keys[]={"GameListA","GameListB","GameListC","GameListD","GameSlow"};
        if (!AddAddress(world_,config.Offset(keys[sourceIndex]),source.header)) return false;
        source.countOffset=config.Offset(sourceIndex==4?"TableCountSlow":"TableCountA");
    } else {
        std::uint64_t itemBase=0;
        if (!AddAddress(world_,config.Offset("GameItemsCandidate"),itemBase) ||
            !AddAddress(itemBase,static_cast<std::uint64_t>(sourceIndex-5)*config.Offset("ItemHeaderStride"),source.header)) return false;
        source.countOffset=config.Offset("ItemCountCandidate");
    }
    std::uint64_t dataAgain=0;
    std::uint32_t countAgain=0;
    if (!ReadSourceHeader(reader,config,source.header,source.countOffset,
            source.data,source.count) || source.count>config.maxTableCount ||
        source.count>config.entityScanLimit-scanTotal_ ||
        (source.count && !UserRange(source.data,1))) return false;
    scanTotal_+=source.count;
    sourceSlots_.resize(source.count);
    for (std::uint32_t offset=0;offset<source.count;offset+=512) {
        const auto n=std::min<std::uint32_t>(512,source.count-offset);
        std::uint64_t address=0;
        if (!AddAddress(source.data,static_cast<std::uint64_t>(offset)*8,address) ||
            !reader.ReadRaw(address,sourceSlots_.data()+offset,static_cast<std::size_t>(n)*8)) return false;
    }
    if (!ReadSourceHeader(reader,config,source.header,source.countOffset,
            dataAgain,countAgain) || dataAgain!=source.data || countAgain!=source.count) return false;
    sources_[sourceIndex]=source;
    membership_[sourceIndex].reserve(source.count);
    scanSlot_=0;sourceLoaded_=true;
    return true;
}
void SceneCache::ProcessCurrent(PlaceholderReader& reader,const Config& config,std::uint32_t limit) {
    const auto sourceIndex=scanSource_;
    const auto count=sources_[sourceIndex].count;
    std::uint32_t processed=0;
    while (scanSlot_<count && processed<limit && reader.Reads()+24<reader.PassCap()) {
        const auto slot=scanSlot_++;
        ++processed;
        const auto entity=sourceSlots_[slot];
        if (!UserRange(entity,1)) continue;
        std::string typeName;
        if (!game::ReadName(reader,config,entity,typeName)) continue;
        const bool item=sourceIndex>=5;
        const auto kind=item?visual::EntityKind::Item:visual::ClassifyEntity(typeName);
        if (kind==visual::EntityKind::Other) continue;
        std::uint64_t type=0;
        if (!game::PtrAt(reader,entity,config.Offset("EntityType"),type)) continue;
        Object object{};object.entity=entity;object.type=type;
        object.source=static_cast<std::uint8_t>(sourceIndex);object.kind=kind;
        if (kind==visual::EntityKind::Player) {
            std::uint64_t at=0;
            if (AddAddress(entity,config.Offset("EntityNetworkIdCandidate"),at))
                reader.Read(at,object.netId);
        } else if (item) {
            std::string className,model;
            game::ReadTypeText(reader,config,type,"TypeClassCandidate",className);
            game::ReadTypeText(reader,config,type,"TypeModelCandidate",model);
            if (!game::ReadTypeText(reader,config,type,"TypeCleanCandidate",object.cleanName))
                object.cleanName=className.empty()?typeName:className;
            object.itemKind=visual::ClassifyItem(typeName,className,model);
        }
        game::Vec3 position{};
        if (game::CandidatePosition(reader,config,entity,position)) {
            const float distance=Distance(position,local_?localPosition_:game::Vec3{});
            if (std::isfinite(distance)) {
                object.scanDistance=distance;object.hasScanDistance=true;
                maxObserved_[visual::Index(kind)]=std::max(maxObserved_[visual::Index(kind)],distance);
            }
        }
        const bool drawSkeleton=visual::SkeletonEnabled(kind,config.showEntities,
            config.showSkeleton);
        const bool resolveForAim=config.aimAssistEnabled && !config.aimSoloCalibration &&
            visual::AimTargetEnabled(kind,config.showEntities,config.aimTargetInfected);
        const float boneGate=static_cast<float>(std::max(
            drawSkeleton?config.skeletonGateMeters:0u,
            resolveForAim?std::min(config.aimGateMeters,config.aimPrebindGateMeters):0u));
        if (boneGate>0 &&
            object.hasScanDistance &&
            object.scanDistance<=boneGate+30.0f &&
            reader.Reads()+24<reader.PassCap()) {
            object.bonesReady=game::ResolveBones(reader,config,entity,type,
                kind==visual::EntityKind::Infected,object.bones);
        }
        objects_.push_back(std::move(object));
    }
}
void SceneCache::ReadIdentities(PlaceholderReader& reader,const Config& config,std::uint64_t module) {
    identities_.clear();
    std::uint64_t manager=0,client=0,list=0,listAgain=0,countAt=0;
    std::uint32_t count=0,countAgain=0,capacity=0;
    if (!AddAddress(module,config.Offset("NetworkManagerObjectRva"),manager) ||
        !game::PtrAt(reader,manager,config.Offset("NetworkClient"),client) ||
        !game::PtrAt(reader,client,config.Offset("ScoreboardDataCandidate"),list) ||
        !AddAddress(client,config.Offset("ScoreboardCountCandidate"),countAt) ||
        !reader.Read(countAt,count) ||
        !reader.Read(client+config.Offset("ScoreboardCapacityCandidate"),capacity) ||
        count>capacity || count>config.maxScoreboardEntries || count>512) return;
    std::array<std::uint64_t,512> pointers{};
    if (count && !reader.ReadRaw(list,pointers.data(),static_cast<std::size_t>(count)*8)) return;
    for (std::uint32_t i=0;i<count;++i) {
        if (!UserRange(pointers[i],1)) continue;
        Identity identity{};
        std::uint64_t nameObject=0;
        if (!reader.Read(pointers[i]+config.Offset("IdentityNetId"),identity.id) || !identity.id ||
            !game::PtrAt(reader,pointers[i],config.Offset("IdentityName"),nameObject) ||
            !game::ReadTextObject(reader,config,nameObject,identity.name)) continue;
        identities_.push_back(std::move(identity));
    }
    if (!game::PtrAt(reader,client,config.Offset("ScoreboardDataCandidate"),listAgain) ||
        !reader.Read(countAt,countAgain) || listAgain!=list || countAgain!=count)
        identities_.clear();
}
const char* SceneCache::PlayerName(std::uint32_t id) const {
    if (!id) return "";
    for (const auto& identity:identities_) if (identity.id==id) return identity.name.c_str();
    return "";
}
bool SceneCache::BeginRefresh(PlaceholderReader& reader,const Config& config,
                              std::uint64_t module,std::uint64_t world,bool mapItemsRequested) {
    Clear();
    for (auto& entries:membership_) entries.reserve(config.entityScanLimit);
    game::LocalSample local{};
    if (!game::FetchLocal(reader,config,module,local) || local.game!=world ||
        !game::CandidatePosition(reader,config,local.entity,localPosition_)) return false;
    world_=world;local_=local.entity;mapItemsRequested_=mapItemsRequested;
    if (config.aimAssistEnabled && config.aimPredictionEnabled &&
        game::ResolveHeldWeapon(reader,config,local_,heldWeapon_))
        game::ResolveMuzzleBinding(reader,config,heldWeapon_,muzzleBinding_);
    return true;
}
SceneCache::ScanStatus SceneCache::StepRefresh(PlaceholderReader& reader,const Config& config,
                                                std::uint64_t module) {
    if (!world_ || !local_) return ScanStatus::Failed;
    std::uint32_t processed=0;
    while (scanSource_<sources_.size()) {
        if (scanSource_>=5 && !ItemsRequested(config) && !mapItemsRequested_) {
            scanSource_=static_cast<std::uint32_t>(sources_.size());
            sourceLoaded_=false;sourceSlots_.clear();
            break;
        }
        if (reader.Reads()+64>=reader.PassCap()) return ScanStatus::Progress;
        if (!sourceLoaded_ && !LoadSource(reader,config,scanSource_)) return ScanStatus::Failed;
        const auto before=scanSlot_;
        ProcessCurrent(reader,config,32-processed);
        processed+=scanSlot_-before;
        if (scanSlot_<sources_[scanSource_].count) return ScanStatus::Progress;
        const auto& source=sources_[scanSource_];
        std::uint64_t dataAgain=0,countAt=0;
        std::uint32_t countAgain=0;
        if (!AddAddress(source.header,source.countOffset,countAt) ||
            !game::At(reader,source.header,config.Offset("TableData"),dataAgain) ||
            !reader.Read(countAt,countAgain) || dataAgain!=source.data || countAgain!=source.count)
            return ScanStatus::Failed;
        ++scanSource_;sourceLoaded_=false;sourceSlots_.clear();
        if (processed>=32) return ScanStatus::Progress;
    }
    std::unordered_set<std::uint64_t> visited;
    visited.reserve(objects_.size());
    objects_.erase(std::remove_if(objects_.begin(),objects_.end(),
        [&visited](const Object& object){return !visited.insert(object.entity).second;}),objects_.end());
    game::LocalSample local{};
    if (!game::FetchLocal(reader,config,module,local) || local.game!=world_ ||
        local.entity!=local_) return ScanStatus::Failed;
    if (config.showPlayerNames) {
        if (reader.Reads()+200>=reader.PassCap()) return ScanStatus::Progress;
        ReadIdentities(reader,config,module);
    }
    return ScanStatus::Complete;
}
bool SceneCache::ReadMembership(PlaceholderReader& reader,const Config& config,bool collectMap) {
    auto readEntries=[&](std::size_t sourceIndex,std::uint64_t data,std::uint32_t count) {
        auto& pointers=membership_[sourceIndex];
        if (count>config.maxTableCount || count>config.entityScanLimit ||
            (count && !UserRange(data,1))) return false;
        pointers.resize(count);
        for (std::uint32_t offset=0;offset<count;offset+=512) {
            const auto chunk=std::min<std::uint32_t>(512,count-offset);
            std::uint64_t address=0;
            if (!AddAddress(data,static_cast<std::uint64_t>(offset)*8,address) ||
                !reader.ReadRaw(address,pointers.data()+offset,static_cast<std::size_t>(chunk)*8)) {
                pointers.clear();return false;
            }
        }
        return true;
    };
    for (std::size_t sourceIndex=0;sourceIndex<5;++sourceIndex) {
        const Source& source=sources_[sourceIndex];
        auto& pointers=membership_[sourceIndex];
        bool valid=false;
        for (int attempt=0;attempt<2 && source.header;++attempt) {
            pointers.clear();
            std::uint64_t data=0,dataAgain=0;
            std::uint32_t count=0,countAgain=0;
            if (!ReadSourceHeader(reader,config,source.header,source.countOffset,data,count) ||
                (source.count && !readEntries(sourceIndex,data,count)) ||
                !ReadSourceHeader(reader,config,source.header,source.countOffset,
                    dataAgain,countAgain) || dataAgain!=data || countAgain!=count)
                continue;
            valid=true;break;
        }
        if (valid) visual::SortMembership(pointers);
        else pointers.clear();
    }
    if (!ItemsRequested(config) && !(collectMap && config.mapItemGateMeters>0)) {
        for (std::size_t i=5;i<sources_.size();++i) membership_[i].clear();
        return true;
    }
    const auto stride=config.Offset("ItemHeaderStride");
    const auto buckets=config.Offset("ItemBucketCount");
    std::array<unsigned char,4096> headers{},headersAgain{};
    std::uint64_t itemBase=0;
    if (stride<16 || buckets!=24 || stride*buckets>headers.size() ||
        config.Offset("ItemCountCandidate")+sizeof(std::uint32_t)>stride ||
        !AddAddress(world_,config.Offset("GameItemsCandidate"),itemBase) ||
        !reader.ReadRaw(itemBase,headers.data(),static_cast<std::size_t>(stride*buckets))) {
        for (std::size_t i=5;i<sources_.size();++i) membership_[i].clear();
        return false;
    }
    for (std::size_t i=5;i<sources_.size();++i) {
        auto& pointers=membership_[i];pointers.clear();
        if (sources_[i].count==0) continue;
        const auto* row=headers.data()+static_cast<std::size_t>(i-5)*stride;
        std::uint64_t data=0;std::uint32_t count=0;
        std::memcpy(&data,row,sizeof(data));
        std::memcpy(&count,row+config.Offset("ItemCountCandidate"),sizeof(count));
        readEntries(i,data,count);
    }
    if (!reader.ReadRaw(itemBase,headersAgain.data(),static_cast<std::size_t>(stride*buckets))) {
        for (std::size_t i=5;i<sources_.size();++i) membership_[i].clear();
    } else {
        for (std::size_t i=5;i<sources_.size();++i) {
            auto& pointers=membership_[i];
            const auto row=static_cast<std::size_t>(i-5)*stride;
            if (std::memcmp(headers.data()+row,headersAgain.data()+row,
                    static_cast<std::size_t>(stride))!=0) pointers.clear();
            else visual::SortMembership(pointers);
        }
    }
    return true;
}
bool SceneCache::Sample(PlaceholderReader& reader,const Config& config,std::uint64_t module,
                         std::uint64_t world,game::CameraSample& camera,
                         float width,float height,bool compactItems,OverlayFrame& frame,
                         bool collectMap,std::uint64_t sampleMs,
                         std::uint64_t frameSequence) {
    localVelocityValid_=false;currentSampleFrame_=0;
    if (!world_ || world_!=world) return false;
    game::LocalSample local{};game::Vec3 localPosition{};
    if (!game::FetchLocal(reader,config,module,local) || local.game!=world ||
        local.entity!=local_ ||
        !game::CandidatePosition(reader,config,local.entity,localPosition)) return false;
    frame.opticFov=0;
    const auto optic=game::ApplyActiveOpticProjection(reader,config,local.entity,camera);
    frame.opticProjection=static_cast<std::uint8_t>(optic);
    if (optic==game::OpticProjection::Invalid) return false;
    if (optic==game::OpticProjection::PictureInPicture) {
        const float opticFov=camera.opticFov;
        frame.opticFov=opticFov;
        game::CameraSample late{};
        if (!game::FetchCameraSample(reader,config,world,late) || late.object!=camera.object)
            return false;
        camera=late;
        if (!game::SetPictureInPictureProjection(camera,opticFov,config)) return false;
    }
    frame.localPosition=localPosition;
    frame.localPositionValid=true;
    frame.localEntity=local.entity;
    currentSampleFrame_=frameSequence;
    localVelocityValid_=false;
    if (config.aimAssistEnabled && config.aimPredictionEnabled && frameSequence)
        localVelocityValid_=localVelocityHistory_.Update(local.entity,localPosition,
            sampleMs,frameSequence,config.aimVelocityWindowMs,
            static_cast<float>(config.aimVelocityMaxMetersPerSecond),localVelocity_);
    ReadMembership(reader,config,collectMap);
    frame.funnel={};
    nearestPlayer_=0;
    float nearestDistance=static_cast<float>(config.gateMeters[0]);
    for (std::size_t i=0;i<maxObserved_.size();++i) frame.funnel[i].maxObserved=maxObserved_[i];
    itemCandidates_.clear();skeletonCandidates_.clear();mapOnlyItems_.clear();
    mapPlayerPoints_.clear();
    for (auto& object:objects_) {
        // Keep a partially exhausted sampling phase from issuing failed reads for
        // every remaining object; the reserved reads belong to cache refresh.
        if (reader.Reads()>=reader.PassCap() ||
            reader.PassCap()-reader.Reads()<=16) break;
        const auto category=visual::Index(object.kind);
        if (!visual::HasMember(membership_[object.source],object.entity)) continue;
        if (object.kind==visual::EntityKind::Player && object.entity!=local.entity &&
            object.hasScanDistance && object.scanDistance<nearestDistance) {
            nearestDistance=object.scanDistance;nearestPlayer_=object.entity;
        }
        ++frame.funnel[category].seen;
        bool overlayItem=false,mapItem=false;
        std::uint32_t overlayItemGate=0;
        if (object.kind==visual::EntityKind::Item) {
            overlayItem=config.showItems && config.showItemCategory[visual::Index(object.itemKind)];
            mapItem=collectMap && config.mapItemGateMeters>0;
            if (!overlayItem && !mapItem) continue;
            if (overlayItem) overlayItemGate=std::min(config.gateMeters[4],
                config.itemCategoryGateMeters[visual::Index(object.itemKind)]);
        } else if (!config.showEntities[category]) continue;
        const bool localHidden=object.entity==local.entity && !config.showLocalPlayer;
        const bool skeletonEnabled=visual::SkeletonEnabled(object.kind,config.showEntities,
            config.showSkeleton);
        if (localHidden && !(skeletonEnabled && object.bonesReady)) continue;
        const auto gate=object.kind==visual::EntityKind::Item?
            std::max(overlayItemGate,mapItem?config.mapItemGateMeters:0u):
            config.gateMeters[category];
        // Map-only items wait until boxes, local item labels, and skeletons have
        // consumed their reads. This keeps a wide map range from starving the overlay.
        if (object.kind==visual::EntityKind::Item && mapItem &&
            (!overlayItem || (object.hasScanDistance &&
             object.scanDistance>static_cast<float>(overlayItemGate)+30.0f))) {
            if ((!object.hasScanDistance || object.scanDistance<=
                    static_cast<float>(config.mapItemGateMeters)+30.0f) &&
                mapOnlyItems_.size()<mapOnlyItems_.capacity())
                mapOnlyItems_.push_back(&object);
            continue;
        }
        // Scan-cadence range is a read gate with a small movement allowance.
        if (object.hasScanDistance && object.scanDistance>static_cast<float>(gate)+30.0f) continue;
        game::Vec3 position{},playerFacing{};
        game::Vec3* facing=collectMap && object.kind==visual::EntityKind::Player &&
            object.entity!=local.entity?&playerFacing:nullptr;
        if (!game::CandidatePositionWithType(reader,config,object.entity,object.type,
                                             position,facing)) continue;
        const float distance=Distance(position,localPosition);
        if (!visual::WithinGate(distance,static_cast<float>(gate))) continue;
        const bool overlayWithin=object.kind!=visual::EntityKind::Item ||
            (overlayItem && visual::WithinGate(distance,static_cast<float>(overlayItemGate)));
        if (overlayWithin) ++frame.funnel[category].within;
        if (object.kind==visual::EntityKind::Infected &&
            !game::CandidateInfectedAlive(reader,config,object.entity)) continue;
        bool aliveForAim=true;
        if (object.kind==visual::EntityKind::Player &&
            (config.hideDeadPlayers || (config.aimAssistEnabled &&
             !config.aimSoloCalibration &&
             distance<=static_cast<float>(config.aimGateMeters)))) {
            aliveForAim=game::CandidatePlayerAlive(reader,config,object.entity);
            if (config.hideDeadPlayers && !aliveForAim) continue;
        }
        if (config.aimAssistEnabled && config.aimPredictionEnabled && frameSequence &&
            aliveForAim && object.entity!=local.entity &&
            visual::AimTargetEnabled(object.kind,config.showEntities,
                                     config.aimTargetInfected) &&
            distance<=static_cast<float>(std::min(config.aimPredictionGateMeters,
                                                   config.aimGateMeters))) {
            game::Vec3 velocity{};
            if (object.velocityHistory.Update(object.entity,position,sampleMs,frameSequence,
                    config.aimVelocityWindowMs,
                    static_cast<float>(config.aimVelocityMaxMetersPerSecond),velocity)) {
                object.velocity=velocity;object.velocityFrame=frameSequence;
            }
        }
        if (config.aimAssistEnabled && !config.aimSoloCalibration &&
            aliveForAim &&
            object.entity!=local.entity &&
            visual::AimTargetEnabled(object.kind,config.showEntities,
                                     config.aimTargetInfected) &&
            distance<=static_cast<float>(config.aimGateMeters) &&
            frame.aimCandidates.size()<frame.aimCandidates.capacity()) {
            game::Vec3 middle=position;middle.y+=1.2f;
            game::Vec2 projected{};bool mainView=false;
            if (game::ProjectWorld(camera,middle,width,height,projected,&mainView))
                frame.aimCandidates.push_back({object.entity,projected,distance,mainView});
        }
        const std::size_t mapIndex=frame.mapPoints.size();
        if (collectMap && object.entity!=local.entity &&
            (object.kind!=visual::EntityKind::Item ||
             (mapItem && visual::WithinGate(distance,static_cast<float>(config.mapItemGateMeters)))) &&
            mapIndex<maptelemetry::MaxPoints) {
            maptelemetry::Point point{};
            point.id=object.entity;
            point.x=position.x;point.y=position.y;point.z=position.z;
            point.distance=distance;
            point.kind=object.kind==visual::EntityKind::Player?0:
                object.kind==visual::EntityKind::Infected?1:
                object.kind==visual::EntityKind::Animal?2:
                object.kind==visual::EntityKind::Vehicle?3:4;
            if (point.kind==0) {
                point.headingX=playerFacing.x;
                point.headingZ=playerFacing.z;
            }
            point.category=object.kind==visual::EntityKind::Item?
                static_cast<std::uint8_t>(visual::Index(object.itemKind)):0;
            point.life=(object.kind==visual::EntityKind::Infected ||
                (object.kind==visual::EntityKind::Player && config.hideDeadPlayers))?
                static_cast<std::uint8_t>(maptelemetry::Life::Alive):
                static_cast<std::uint8_t>(maptelemetry::Life::Unknown);
            const char* mapName=object.kind==visual::EntityKind::Player?
                PlayerName(object.netId):object.cleanName.c_str();
            if (mapName) std::snprintf(point.name,sizeof(point.name),"%s",mapName);
            frame.mapPoints.push_back(point);
            if (point.kind==0) mapPlayerPoints_.push_back({&object,mapIndex});
        }
        if (skeletonEnabled && object.bonesReady) ++frame.skeletonBound;
        if (skeletonEnabled && object.bonesReady &&
            distance<=static_cast<float>(config.skeletonGateMeters) &&
            skeletonCandidates_.size()<skeletonCandidates_.capacity()) {
            game::Vec3 center=position;center.y+=0.9f;
            game::Vec2 projected{};
            bool mainView=false;
            if (game::ProjectWorld(camera,center,width,height,projected,&mainView)) {
                skeletonCandidates_.push_back({&object,distance,mainView});
                ++frame.skeletonEligible;
            }
        }
        if (object.kind==visual::EntityKind::Item) {
            if (!overlayWithin) continue;
            const auto itemIndex=visual::Index(object.itemKind);
            const auto band=visual::Band(distance,static_cast<float>(config.itemCategoryNearMeters[itemIndex]),
                static_cast<float>(config.itemCategoryMidMeters[itemIndex]),static_cast<float>(overlayItemGate),
                config.itemAnchoredLists || config.itemCategoryFarName[itemIndex]);
            if (band==visual::ItemBand::Hidden) continue;
            game::Vec2 screen{};
            if (!game::ProjectWorld(camera,position,width,height,screen)) continue;
            ItemCandidate item{};item.entity=object.entity;item.world=position;
            item.x=screen.x;item.y=screen.y;item.distance=distance;
            item.category=static_cast<std::uint8_t>(itemIndex);
            item.priority=config.itemCategoryPriority[visual::Index(object.itemKind)];
            item.nameVisible=visual::RevealItemName(distance,
                config.itemCategoryNameMeters[itemIndex],config.itemUseIcons);
            item.alpha=config.itemAnchoredLists?
                static_cast<std::uint8_t>(item.nameVisible?255:config.itemIconOpacity):
                band==visual::ItemBand::Far?static_cast<std::uint8_t>(config.itemFarAlpha):255;
            item.color=config.itemCategoryColors[itemIndex];
            std::uint32_t quality=UINT32_MAX;
            if (band==visual::ItemBand::Near && config.itemCategoryNearQuality[itemIndex] &&
                (!config.itemAnchoredLists || item.nameVisible)) {
                std::uint64_t at=0;
                if (AddAddress(object.entity,config.Offset("EntityQualityCandidate"),at))
                    reader.Read(at,quality);
                else quality=UINT32_MAX;
                if (!config.itemAnchoredLists &&
                    (quality&0xffu)<config.itemQualityColors.size())
                    item.color=config.itemQualityColors[quality&0xffu];
            }
            if (collectMap && mapIndex<frame.mapPoints.size() &&
                (quality&0xffu)<5u)
                frame.mapPoints[mapIndex].quality=static_cast<std::uint8_t>(quality&0xffu);
            visual::ItemLabel(item.label,sizeof(item.label),object.cleanName.c_str(),distance,
                quality,band,compactItems,config.itemCategoryNearDistance[itemIndex],
                config.itemCategoryNearQuality[itemIndex]);
            itemCandidates_.push_back(item);
            continue;
        }
        if (localHidden) continue;
        visual::Box box{};
        game::Vec3 boxCenter=position;
        boxCenter.y+=BoxHeight(object.kind)*0.5f;
        game::Vec2 projectedCenter{};
        bool mainView=false;
        if (!game::ProjectWorld(camera,boxCenter,width,height,projectedCenter,&mainView))
            continue;
        const auto boxCamera=game::SelectPictureInPictureView(camera,mainView);
        if (!visual::ProjectBox(boxCamera,position,BoxHeight(object.kind),
                BoxWidthRatio(object.kind),width,height,box)) continue;
        EntityBoxDraw draw{};draw.left=box.left;draw.top=box.top;
        draw.right=box.right;draw.bottom=box.bottom;
        draw.color=config.entityColors[category];draw.local=object.entity==local.entity;
        const char* name=KindName(object.kind);
        if (object.kind==visual::EntityKind::Infected && !config.showInfectedNames) name="";
        std::string held;
        if (object.kind==visual::EntityKind::Player) {
            if (draw.local) name=config.showPlayerNames?"YOU":"";
            else if (config.showPlayerNames) {
                std::uint32_t currentId=0;
                std::uint64_t at=0;
                if (AddAddress(object.entity,config.Offset("EntityNetworkIdCandidate"),at) &&
                    reader.Read(at,currentId) && currentId==object.netId) name=PlayerName(currentId);
                else name="";
            } else name="";
            if (config.showHeldItem) ReadHeldName(reader,config,object.entity,held);
        }
        visual::EntityLabel(draw.label,sizeof(draw.label),name,held.c_str(),distance);
        frame.boxes.push_back(draw);++frame.funnel[category].drawn;
    }
    std::sort(skeletonCandidates_.begin(),skeletonCandidates_.end(),
        [](const SkeletonCandidate& a,const SkeletonCandidate& b){return a.distance<b.distance;});
    for (const auto& candidate:skeletonCandidates_) {
        if (reader.Reads()+16>=reader.PassCap()) break;
        game::BonePose pose{};
        const auto rig=candidate.object->kind==visual::EntityKind::Infected?1u:0u;
        ++frame.skeletonEligibleByType[rig];
        std::uint32_t stage=0;
        const auto boneCamera=game::SelectPictureInPictureView(camera,candidate.mainView);
        if (!game::SampleBonePose(reader,config,candidate.object->bones,localPosition,
                static_cast<float>(config.skeletonGateMeters),boneCamera,width,height,pose,&stage)) {
            frame.skeletonFailureStage[rig]=stage;continue;
        }
        if (config.aimAssistEnabled && !config.aimSoloCalibration) {
            const auto aimCandidate=std::find_if(frame.aimCandidates.begin(),
                frame.aimCandidates.end(),[&](const aim::Candidate& item) {
                    return item.entity==candidate.object->entity &&
                           item.mainView==candidate.mainView;
                });
            if (aimCandidate!=frame.aimCandidates.end()) {
                const auto joint=static_cast<std::size_t>(config.aimBone);
                if (joint<pose.screen.size() && pose.visible[joint])
                    aimCandidate->screen=pose.screen[joint];
                else aimCandidate->screen={
                    std::numeric_limits<float>::quiet_NaN(),
                    std::numeric_limits<float>::quiet_NaN()};
            }
        }
        bool anyLine=false;
        for (std::size_t pairIndex=0;pairIndex<config.skeletonPairCounts[rig];++pairIndex) {
            const auto pair=config.skeletonPairs[rig][pairIndex];
            if (pose.visible[pair.from] && pose.visible[pair.to]) {anyLine=true;break;}
        }
        if (!anyLine) {frame.skeletonFailureStage[rig]=7;continue;}
        SkeletonDraw draw{};draw.entity=candidate.object->entity;
        draw.joints=pose.screen;draw.world=pose.world;draw.visible=pose.visible;
        draw.kind=static_cast<std::uint8_t>(rig);
        draw.mainView=candidate.mainView;
        draw.color=config.entityColors[visual::Index(candidate.object->kind)];
        frame.skeletons.push_back(draw);
        ++frame.skeletonSampled;
    }
    if (collectMap) {
        std::string held;
        if (reader.Reads()+12<reader.PassCap() &&
            ReadHeldName(reader,config,local.entity,held))
            std::snprintf(frame.localHeld.data(),frame.localHeld.size(),"%s",held.c_str());
        for (const auto& player:mapPlayerPoints_) {
            if (reader.Reads()+12>=reader.PassCap()) break;
            held.clear();
            if (player.index>=frame.mapPoints.size() ||
                !ReadHeldName(reader,config,player.object->entity,held)) continue;
            auto& point=frame.mapPoints[player.index];
            std::snprintf(point.held,sizeof(point.held),"%s",held.c_str());
        }
    }
    for (const Object* object:mapOnlyItems_) {
        if (reader.Reads()+16>=reader.PassCap() ||
            frame.mapPoints.size()>=maptelemetry::MaxPoints) break;
        game::Vec3 position{};
        if (!game::CandidatePositionWithType(reader,config,object->entity,object->type,
                                             position)) continue;
        const float distance=Distance(position,localPosition);
        if (!visual::WithinGate(distance,static_cast<float>(config.mapItemGateMeters))) continue;
        maptelemetry::Point point{};
        point.id=object->entity;
        point.x=position.x;point.y=position.y;point.z=position.z;
        point.distance=distance;point.kind=4;
        point.category=static_cast<std::uint8_t>(visual::Index(object->itemKind));
        std::snprintf(point.name,sizeof(point.name),"%s",object->cleanName.c_str());
        frame.mapPoints.push_back(point);
    }
    const auto columns=static_cast<std::uint32_t>(width)/config.itemCellPixels+1;
    const auto rows=static_cast<std::uint32_t>(height)/config.itemCellPixels+1;
    const auto cells=static_cast<std::size_t>(columns)*rows;
    if (config.itemAnchoredLists) {
        previousVisibleItems_.clear();
        itemGroups_.clear();itemGroupOf_.clear();itemGroupNext_.clear();
        itemGroupHead_.assign(8192,-1);
        const float inverseCell=100.0f/static_cast<float>(config.itemGroupRadiusCm);
        std::sort(itemCandidates_.begin(),itemCandidates_.end(),
            [](const ItemCandidate& a,const ItemCandidate& b){return a.entity<b.entity;});
        for (const auto& item:itemCandidates_) {
            const int cellX=static_cast<int>(std::floor(item.world.x*inverseCell));
            const int cellY=static_cast<int>(std::floor(item.world.y*inverseCell));
            const int cellZ=static_cast<int>(std::floor(item.world.z*inverseCell));
            int group=-1;
            for (int dz=-1;dz<=1 && group<0;++dz)
                for (int dy=-1;dy<=1 && group<0;++dy)
                    for (int dx=-1;dx<=1 && group<0;++dx) {
                        const auto bucket=ItemCellHash(cellX+dx,cellY+dy,cellZ+dz);
                        for (int probe=itemGroupHead_[bucket];probe>=0;
                             probe=itemGroupNext_[static_cast<std::size_t>(probe)]) {
                            const auto& existing=itemGroups_[static_cast<std::size_t>(probe)];
                            if (existing.cellX!=cellX+dx || existing.cellY!=cellY+dy ||
                                existing.cellZ!=cellZ+dz ||
                                !visual::SameItemPile(item.world,existing.world,
                                    config.itemGroupRadiusCm)) continue;
                            group=probe;break;
                        }
                    }
            if (group<0) {
                ItemGroupScratch created{};
                created.id=item.entity;
                created.world=item.world;created.x=item.x;created.y=item.y;
                created.count=1;created.priority=item.priority;
                created.cellX=cellX;created.cellY=cellY;created.cellZ=cellZ;
                group=static_cast<int>(itemGroups_.size());
                itemGroups_.push_back(created);
                const auto bucket=ItemCellHash(cellX,cellY,cellZ);
                itemGroupNext_.push_back(itemGroupHead_[bucket]);
                itemGroupHead_[bucket]=group;
            } else {
                auto& found=itemGroups_[static_cast<std::size_t>(group)];
                const float weight=1.0f/static_cast<float>(found.count+1);
                found.world.x+=(item.world.x-found.world.x)*weight;
                found.world.y+=(item.world.y-found.world.y)*weight;
                found.world.z+=(item.world.z-found.world.z)*weight;
                // Keep the first item's projection as the list anchor. The world
                // centroid still governs grouping, but changing membership must
                // not move an already visible callout around the screen.
                ++found.count;found.priority=std::max(found.priority,item.priority);
            }
            itemGroupOf_.push_back(static_cast<std::uint32_t>(group));
        }
        for (std::size_t group=0;group<itemGroups_.size();++group) {
            const auto& source=itemGroups_[group];
            ItemGroupDraw drawGroup{};
            drawGroup.id=source.id;
            drawGroup.x=source.x;drawGroup.y=source.y;
            drawGroup.first=static_cast<std::uint32_t>(frame.items.size());
            drawGroup.priority=source.priority;
            const float dx=source.x-width*0.5f,dy=source.y-height*0.5f;
            const float focus=static_cast<float>(config.itemFocusRadiusPixels);
            const bool reveal=!config.itemFocusOnly || dx*dx+dy*dy<=focus*focus;
            for (std::size_t i=0;i<itemCandidates_.size();++i) {
                if (itemGroupOf_[i]!=group) continue;
                const auto& item=itemCandidates_[i];
                ItemLabelDraw draw{};draw.entity=item.entity;
                draw.x=item.x;draw.y=item.y;draw.distance=item.distance;
                draw.color=item.color;draw.alpha=item.alpha;
                draw.category=item.category;
                draw.nameVisible=item.nameVisible && reveal;
                std::memcpy(draw.label,item.label,sizeof(draw.label));
                frame.items.push_back(draw);
                ++drawGroup.count;
            }
            frame.itemGroups.push_back(drawGroup);
        }
    } else if (config.itemGrid && cells<=100000) {
        gridHead_.assign(cells,-1);
        gridNext_.assign(itemCandidates_.size(),-1);
        itemOrder_.resize(itemCandidates_.size());
        std::iota(itemOrder_.begin(),itemOrder_.end(),0);
        for (std::size_t i=0;i<itemCandidates_.size();++i) {
            auto& item=itemCandidates_[i];
            item.wasVisible=std::binary_search(previousVisibleItems_.begin(),
                previousVisibleItems_.end(),item.entity);
            const float allowance=item.wasVisible?
                std::clamp(item.distance*0.02f,0.5f,2.0f):0.0f;
            item.selectionDistance=item.distance-allowance;
        }
        std::sort(itemOrder_.begin(),itemOrder_.end(),[this](int left,int right) {
            const auto& a=itemCandidates_[static_cast<std::size_t>(left)];
            const auto& b=itemCandidates_[static_cast<std::size_t>(right)];
            if (a.priority!=b.priority) return a.priority>b.priority;
            if (a.selectionDistance!=b.selectionDistance)
                return a.selectionDistance<b.selectionDistance;
            return a.entity<b.entity;
        });
        selectedItems_.clear();groupCounts_.clear();
        for (const int index:itemOrder_) {
            const auto& item=itemCandidates_[static_cast<std::size_t>(index)];
            const auto cell=visual::Cell(item.x,item.y,config.itemCellPixels,columns,rows);
            if (cell==UINT32_MAX) continue;
            const int col=static_cast<int>(cell%columns);
            const int row=static_cast<int>(cell/columns);
            int group=-1;
            float closestSquared=std::numeric_limits<float>::max();
            for (int dy=-2;dy<=2;++dy) {
                const int nearRow=row+dy;
                if (nearRow<0 || nearRow>=static_cast<int>(rows)) continue;
                for (int dx=-2;dx<=2;++dx) {
                    const int nearCol=col+dx;
                    if (nearCol<0 || nearCol>=static_cast<int>(columns)) continue;
                    const auto neighbor=static_cast<std::size_t>(nearRow)*columns+
                        static_cast<std::size_t>(nearCol);
                    for (int selected=gridHead_[neighbor];selected>=0;
                         selected=gridNext_[static_cast<std::size_t>(selected)]) {
                        const auto& winner=itemCandidates_[static_cast<std::size_t>(
                            selectedItems_[static_cast<std::size_t>(selected)])];
                        const float diffX=item.x-winner.x,diffY=item.y-winner.y;
                        const float squared=diffX*diffX+diffY*diffY;
                        if (squared<closestSquared &&
                            visual::SuppressNearbyItem(diffX,diffY,config.itemCellPixels,
                                item.wasVisible)) {
                            closestSquared=squared;group=selected;
                        }
                    }
                }
            }
            if (group>=0) {
                auto& count=groupCounts_[static_cast<std::size_t>(group)];
                if (count!=UINT16_MAX) ++count;
                continue;
            }
            const int selected=static_cast<int>(selectedItems_.size());
            selectedItems_.push_back(index);groupCounts_.push_back(1);
            gridNext_[static_cast<std::size_t>(selected)]=gridHead_[cell];
            gridHead_[cell]=selected;
        }
        previousVisibleItems_.clear();
        for (std::size_t selected=0;selected<selectedItems_.size();++selected) {
            const auto& item=itemCandidates_[static_cast<std::size_t>(selectedItems_[selected])];
            previousVisibleItems_.push_back(item.entity);
            ItemLabelDraw draw{};draw.entity=item.entity;
            draw.x=item.x;draw.y=item.y;draw.distance=item.distance;
            draw.alpha=item.alpha;draw.cluster=config.itemClusterCount?groupCounts_[selected]:1;
            std::memcpy(draw.label,item.label,sizeof(draw.label));
            frame.items.push_back(draw);
        }
        std::sort(previousVisibleItems_.begin(),previousVisibleItems_.end());
    } else {
        previousVisibleItems_.clear();
        for (const auto& item:itemCandidates_) {
            ItemLabelDraw draw{};draw.entity=item.entity;
            draw.x=item.x;draw.y=item.y;draw.distance=item.distance;
            draw.alpha=item.alpha;draw.cluster=1;
            std::memcpy(draw.label,item.label,sizeof(draw.label));
            frame.items.push_back(draw);
        }
    }
    if (config.itemAnchoredLists) {
        std::size_t accounted=0;
        for (const auto& group:frame.itemGroups) {
            if (group.first!=accounted || group.count==0 ||
                group.count>frame.items.size()-accounted) {
                frame.items.clear();frame.itemGroups.clear();
                break;
            }
            accounted+=group.count;
        }
        if (accounted!=itemCandidates_.size()) {
            frame.items.clear();frame.itemGroups.clear();
        }
    }
    frame.funnel[4].drawn=static_cast<std::uint32_t>(frame.items.size());
    return true;
}

bool SceneCache::SampleAimBone(PlaceholderReader& reader,const Config& config,
                                const game::CameraSample& camera,
                                const OverlayFrame& frame,
                                const aim::Selection& selected,
                                game::Vec2& result,game::Vec3* worldResult) {
    if (!config.aimAssistEnabled || !selected.entity || !frame.localPositionValid) return false;
    const auto found=std::find_if(objects_.begin(),objects_.end(),
        [&](const Object& object){return object.entity==selected.entity;});
    if (found==objects_.end() ||
        !visual::HasMember(membership_[found->source],found->entity) ||
        !visual::AimTargetEnabled(found->kind,config.showEntities,
                                  config.aimTargetInfected)) return false;
    const auto candidate=std::find_if(frame.aimCandidates.begin(),frame.aimCandidates.end(),
        [&](const aim::Candidate& item) {
            return item.entity==selected.entity && item.mainView==selected.mainView &&
                   item.distance<=static_cast<float>(config.aimGateMeters);
        });
    if (candidate==frame.aimCandidates.end()) return false;
    const auto bone=static_cast<std::size_t>(config.aimBone);
    const float cone=static_cast<float>(config.aimFovPixels);
    for (const auto& skeleton:frame.skeletons) {
        if (skeleton.entity!=selected.entity || skeleton.mainView!=selected.mainView) continue;
        if (bone>=skeleton.joints.size() || !skeleton.visible[bone] ||
            !aim::InCone(skeleton.joints[bone],static_cast<float>(frame.viewportWidth),
                         static_cast<float>(frame.viewportHeight),
                         cone)) return false;
        result=skeleton.joints[bone];
        if (worldResult) *worldResult=skeleton.world[bone];
        return true;
    }
    if (!found->bonesReady) {
        if (reader.Reads()+32>=reader.PassCap()) return false;
        game::BoneBinding binding{};
        if (!game::ResolveBones(reader,config,found->entity,found->type,
                                found->kind==visual::EntityKind::Infected,binding)) return false;
        found->bones=binding;
        found->bonesReady=true;
    }
    if (reader.Reads()+16>=reader.PassCap()) return false;
    const auto boneCamera=game::SelectPictureInPictureView(camera,selected.mainView);
    game::BonePose pose{};
    if (!game::SampleBonePose(reader,config,found->bones,frame.localPosition,
            static_cast<float>(config.aimGateMeters),boneCamera,
            static_cast<float>(frame.viewportWidth),
            static_cast<float>(frame.viewportHeight),pose)) return false;
    if (bone>=pose.screen.size() || !pose.visible[bone] ||
        !aim::InCone(pose.screen[bone],static_cast<float>(frame.viewportWidth),
                     static_cast<float>(frame.viewportHeight),
                     cone)) return false;
    result=pose.screen[bone];
    if (worldResult) *worldResult=pose.world[bone];
    return true;
}

bool SceneCache::TargetVelocity(std::uint64_t entity,game::Vec3& result) const {
    const auto found=std::find_if(objects_.begin(),objects_.end(),
        [entity](const Object& object){return object.entity==entity;});
    if (found==objects_.end() || !currentSampleFrame_ ||
        found->velocityFrame!=currentSampleFrame_) return false;
    result=found->velocity;return true;
}
bool SceneCache::LocalVelocity(game::Vec3& result) const {
    if (!localVelocityValid_) return false;
    result=localVelocity_;return true;
}
bool SceneCache::PredictionSpeed(PlaceholderReader& reader,const Config& config,
                                game::MuzzleSpeed& result,bool& fallback) const {
    fallback=false;
    if (!config.aimAssistEnabled || !config.aimPredictionEnabled ||
        !heldWeapon_.weapon) return false;
    if (muzzleBinding_.weapon &&
        game::SampleMuzzleSpeed(reader,config,muzzleBinding_,result)) return true;
    const float speed=config.AimFallbackMps(heldWeapon_.weaponClass);
    if (speed<1.0f || !game::ValidateHeldWeapon(reader,config,heldWeapon_)) return false;
    result={speed,1.0f,speed};fallback=true;return true;
}
