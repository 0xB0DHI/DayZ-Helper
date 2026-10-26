#pragma once
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include "../game/game.h"
#include "../game/skeleton.h"

struct Config;
struct ImFont;

struct EntityBoxDraw {
    float left=0,top=0,right=0,bottom=0;
    std::uint32_t color=0;
    bool local=false;
    char label[128]{};
};
struct ItemLabelDraw {
    std::uint64_t entity=0;
    float x=0,y=0;
    float distance=0;
    std::uint32_t color=0;
    std::uint8_t alpha=255;
    std::uint8_t category=0;
    bool nameVisible=true;
    std::uint16_t cluster=0;
    char label[128]{};
};
struct ItemGroupDraw {
    std::uint64_t id=0;
    float x=0,y=0;
    std::uint32_t first=0,count=0;
    int priority=0;
};
struct SkeletonDraw {
    std::array<game::Vec2,game::JointCount> joints{};
    std::array<bool,game::JointCount> visible{};
    std::uint32_t color=0;
    std::uint8_t kind=0;
};
struct FunnelCount { std::uint32_t seen=0,within=0,drawn=0; float maxObserved=0; };

struct OverlayFrame {
    bool attached=false;
    bool dataSourceConnected=false;
    bool cameraValid=false;
    bool localPositionValid=false;
    game::Vec3 localPosition{},cameraForward{};
    std::uint64_t moduleBase=0;
    float fps=0;
    std::uint32_t reads=0,failed=0;
    std::uint8_t coordinateCopyNotice=0;
    std::uint32_t logicalReads=0,refreshesPerMinute=0;
    bool markerValid=false;
    float markerX=0,markerY=0;
    std::uint32_t markerStage=0;
    std::uint32_t viewportWidth=0,viewportHeight=0;
    bool scanLocal=false;
    std::uint32_t scanEntries=0,scanInfected=0,scanPositioned=0,scanFront=0,scanProjected=0;
    std::array<FunnelCount,5> funnel{};
    std::vector<EntityBoxDraw> boxes;
    std::vector<ItemLabelDraw> items;
    std::vector<ItemGroupDraw> itemGroups;
    std::vector<SkeletonDraw> skeletons;
    std::uint32_t skeletonBound=0,skeletonEligible=0,skeletonSampled=0;
    std::array<std::uint32_t,2> skeletonEligibleByType{},skeletonFailureStage{};
    bool compactItems=false;
    bool healthWatchValid=false,deathLocalValid=false,deathNearbyValid=false;
    game::HealthWatch healthWatch{};
    game::DeathWatch deathLocal{},deathNearby{};
};

class Overlay {
public:
    bool Initialize(HWND window);
    void Shutdown();
    bool Resize(UINT width,UINT height);
    bool Draw(const OverlayFrame& frame,bool menuOpen,Config& config,
              bool& compactItems,bool& configChanged);
    bool BindingHotkey() const { return bindingItemKey_ || bindingCopyKey_; }
private:
    bool CreateTarget();
    void RefreshAdvanced(const Config& config);
    struct RawEntry {
        std::string section,key;
        char value[256]{};
    };
    std::vector<RawEntry> advancedEntries_;
    std::vector<std::size_t> itemGroupOrder_;
    std::vector<std::size_t> itemRowOrder_;
    std::vector<std::size_t> itemSplitOrder_;
    bool advancedReady_=false;
    bool pendingSave_=false;
    bool bindingItemKey_=false;
    bool bindingCopyKey_=false;
    ImFont* itemFont_=nullptr;
    ImFont* navFont_=nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swap_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    UINT width_=0,height_=0;
    bool imgui_=false;
};
