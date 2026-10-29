#pragma once
#include "../config/config.h"
#include "../data/placeholder_reader.h"
#include "../game/game.h"
#include "../game/skeleton.h"
#include "../game/prediction.h"
#include "../game/visualization.h"
#include "../overlay/overlay.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class SceneCache {
public:
    enum class ScanStatus { Failed, Progress, Complete };
    SceneCache();
    bool BeginRefresh(PlaceholderReader& reader,const Config& config,std::uint64_t module,
                      std::uint64_t world,bool mapItemsRequested=false);
    ScanStatus StepRefresh(PlaceholderReader& reader,const Config& config,std::uint64_t module);
    bool Sample(PlaceholderReader& reader,const Config& config,std::uint64_t module,
                std::uint64_t world,game::CameraSample& camera,
                float width,float height,bool compactItems,OverlayFrame& frame,
                bool collectMap=false,std::uint64_t sampleMs=0,
                std::uint64_t frameSequence=0);
    bool SampleAimBone(PlaceholderReader& reader,const Config& config,
                       const game::CameraSample& camera,const OverlayFrame& frame,
                       const aim::Selection& selected,game::Vec2& result,
                       game::Vec3* worldResult=nullptr);
    bool TargetVelocity(std::uint64_t entity,game::Vec3& result) const;
    bool LocalVelocity(game::Vec3& result) const;
    bool PredictionSpeed(PlaceholderReader& reader,const Config& config,
                         game::MuzzleSpeed& result,bool& fallback) const;
    const std::string& WeaponClass() const { return heldWeapon_.weaponClass; }
    void Clear();
    void CarryItemWinnerHistory(SceneCache& next);
    std::uint64_t World() const { return world_; }
    std::size_t ObjectCount() const { return objects_.size(); }
    std::uint64_t NearestPlayer() const { return nearestPlayer_; }
private:
    struct Source {
        std::uint64_t header=0,data=0;
        std::uint32_t count=0;
        std::uint64_t countOffset=0;
    };
    struct Object {
        std::uint64_t entity=0,type=0;
        std::uint32_t netId=0;
        std::uint8_t source=0;
        visual::EntityKind kind=visual::EntityKind::Other;
        visual::ItemKind itemKind=visual::ItemKind::Other;
        std::string cleanName;
        float scanDistance=0;
        bool hasScanDistance=false;
        game::BoneBinding bones{};
        bool bonesReady=false;
        aim::VelocityHistory velocityHistory{};
        game::Vec3 velocity{};
        std::uint64_t velocityFrame=0;
    };
    struct Identity { std::uint32_t id=0; std::string name; };
    struct ItemCandidate {
        std::uint64_t entity=0;
        game::Vec3 world{};
        float x=0,y=0,distance=0;
        float selectionDistance=0;
        int priority=0;
        bool wasVisible=false;
        std::uint32_t color=0xD6FFAD;
        std::uint8_t alpha=255;
        std::uint8_t category=0;
        bool nameVisible=true;
        char label[128]{};
    };
    struct ItemGroupScratch {
        std::uint64_t id=0;
        game::Vec3 world{};
        float x=0,y=0;
        std::uint32_t count=0;
        int priority=0;
        int cellX=0,cellY=0,cellZ=0;
    };
    bool LoadSource(PlaceholderReader& reader,const Config& config,std::uint32_t sourceIndex);
    void ProcessCurrent(PlaceholderReader& reader,const Config& config,std::uint32_t limit);
    bool ReadMembership(PlaceholderReader& reader,const Config& config,bool collectMap);
    void ReadIdentities(PlaceholderReader& reader,const Config& config,std::uint64_t module);
    const char* PlayerName(std::uint32_t id) const;
    std::uint64_t world_=0,local_=0;
    game::HeldWeapon heldWeapon_{};
    game::MuzzleBinding muzzleBinding_{};
    aim::VelocityHistory localVelocityHistory_{};
    game::Vec3 localVelocity_{};
    bool localVelocityValid_=false;
    std::uint64_t currentSampleFrame_=0;
    std::uint64_t nearestPlayer_=0;
    game::Vec3 localPosition_{};
    std::array<Source,29> sources_{};
    std::array<std::vector<std::uint64_t>,29> membership_{};
    std::vector<Object> objects_;
    std::vector<Identity> identities_;
    std::vector<ItemCandidate> itemCandidates_;
    std::vector<const Object*> mapOnlyItems_;
    struct MapPlayerPoint { const Object* object=nullptr; std::size_t index=0; };
    std::vector<MapPlayerPoint> mapPlayerPoints_;
    struct SkeletonCandidate { const Object* object=nullptr; float distance=0; bool mainView=false; };
    std::vector<SkeletonCandidate> skeletonCandidates_;
    std::vector<int> itemOrder_,gridHead_,gridNext_,selectedItems_;
    std::vector<std::uint16_t> groupCounts_;
    std::vector<std::uint64_t> previousVisibleItems_;
    std::vector<ItemGroupScratch> itemGroups_;
    std::vector<std::uint32_t> itemGroupOf_;
    std::vector<int> itemGroupHead_,itemGroupNext_;
    std::array<float,5> maxObserved_{};
    std::vector<std::uint64_t> sourceSlots_;
    std::uint32_t scanSource_=0,scanSlot_=0,scanTotal_=0;
    bool sourceLoaded_=false;
    bool mapItemsRequested_=false;
};
