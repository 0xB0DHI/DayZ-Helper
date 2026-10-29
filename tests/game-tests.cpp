#include "../src/game/game.h"
#include "../src/game/visualization.h"
#include "../src/game/skeleton.h"
#include "../src/game/aim_assist.h"
#include "../src/game/prediction.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace {
struct FakeReader {
    std::unordered_map<std::uint64_t, std::uint8_t> bytes;
    std::uint64_t failAddress=0;
    std::unordered_map<std::uint64_t,std::uint32_t> readsAt;
    std::size_t maxReadSize=0;
    template<class T> void Put(std::uint64_t address, const T& value) {
        const auto* p=reinterpret_cast<const std::uint8_t*>(&value);
        for (std::size_t i=0;i<sizeof(T);++i) bytes[address+i]=p[i];
    }
    bool ReadRaw(std::uint64_t address, void* result, std::size_t length) {
        ++readsAt[address];maxReadSize=std::max(maxReadSize,length);
        if (address==failAddress) return false;
        auto* p=static_cast<std::uint8_t*>(result);
        for (std::size_t i=0;i<length;++i) {
            auto it=bytes.find(address+i);
            if (it==bytes.end()) return false;
            p[i]=it->second;
        }
        return true;
    }
    template<class T> bool Read(std::uint64_t address,T& result) {
        T temporary{};
        if (!ReadRaw(address,&temporary,sizeof(T))) return false;
        result=temporary;return true;
    }
};
int failures=0,checks=0;
void Check(bool ok,const char* name) {
    ++checks; if (!ok) {++failures;std::printf("FAIL %s\n",name);}
}
Config Setup() {
    Config c{};
    c.maxTableCount=100;
    c.maxTableEntries=4;
    c.offsets={{"GameWorldPointerRva",0x100},{"GameLocalLink",0x20},
               {"LocalLinkInterior",8},{"LocalInteriorBias",0xA8},
               {"TableData",0},{"TableStride",8},{"GameCamera",0x40},
               {"EntityDeadCandidate",0x156},
               {"EntityType",0x130},{"TypeName",0xD0},{"TypeCleanCandidate",0x4C0},
               {"StringChars",0x10},{"EntityVisualStateCandidate",0x158},
               {"VisualStatePositionCandidate",0x2C},
               {"VisualStateMatrix",0x8},
               {"CameraSampleBytes",0xE4},{"CameraRightCandidate",0x8},
               {"CameraUpCandidate",0x14},{"CameraForwardCandidate",0x20},
               {"CameraPosition",0x2C},{"CameraViewportCandidate",0x58},
               {"CameraTanHorizontalCandidate",0xD0},{"CameraTanVerticalCandidate",0xE0},
               {"EntityInventory",0x5D8},{"InventoryHandsCandidate",0x190},
               {"OpticAttachmentEntries",0x130},{"OpticAttachmentCount",0x13C},
               {"OpticAttachmentStride",0x10},{"OpticAttachmentEntity",0x8},
               {"OpticCurrentStepFov",0x638},{"OpticIsInOptics",0x640},
               {"OpticWeaponOverride",0x641},{"OpticModel2D",0x930},
               {"OpticZoomInit",0x960},{"OpticStepFovData",0x9A8},
               {"OpticStepFovCount",0x9B0}};
    return c;
}
}
int main() {
    auto config=Setup(); FakeReader reader{};
    config.offsets["CurrentWorldNameRva"]=0x200;
    {
        std::array<char,32> bytes{};
        std::memcpy(bytes.data(),"Nasdara",8);
        reader.Put(0x100200,bytes);
        char terrain[32]{};
        Check(game::FetchTerrainName(reader,config,0x100000,terrain) &&
              std::strcmp(terrain,"nasdara")==0,"terrain name canonicalized");
        bytes.fill(0);
        std::memcpy(bytes.data(),"chernarusplus",14);
        reader.Put(0x100200,bytes);
        Check(game::FetchTerrainName(reader,config,0x100000,terrain) &&
              std::strcmp(terrain,"chernarusplus")==0,"terrain switch replaces slug");
        reader.failAddress=0x100200;
        std::memcpy(terrain,"retained",9);
        Check(!game::FetchTerrainName(reader,config,0x100000,terrain) &&
              std::strcmp(terrain,"retained")==0,"terrain read failure does not publish");
        reader.failAddress=0;
        bytes.fill(0);
        std::memcpy(bytes.data(),"unknownworld",13);
        reader.Put(0x100200,bytes);
        Check(!game::FetchTerrainName(reader,config,0x100000,terrain) &&
              std::strcmp(terrain,"retained")==0,"unsupported terrain does not publish");
        bytes.fill('a');reader.Put(0x100200,bytes);
        Check(!game::FetchTerrainName(reader,config,0x100000,terrain),
              "unterminated terrain rejected");
    }
    Config configured{};
    Check(configured.Load(L"dayz.ini") && configured.gateMeters[4]>0 &&
          configured.mapItemGateMeters==1200 &&
          configured.itemCategoryGateMeters[0]>0 &&
          configured.itemNearMeters<=configured.itemMidMeters &&
          configured.skeletonGateMeters>0 && configured.panelKey==0x70 &&
          configured.coordinateCopyKey==0x72,
          "current config loads with F1 panel key");
    const std::filesystem::path temporaryConfig=L"out\\config-roundtrip-test.ini";
    std::filesystem::copy_file(L"dayz.ini",temporaryConfig,
        std::filesystem::copy_options::overwrite_existing);
    Config roundtrip{};
    const bool loaded=roundtrip.Load(temporaryConfig.wstring());
    roundtrip.gateMeters[4]=1;
    roundtrip.mapItemGateMeters=1350;
    roundtrip.itemCategoryGateMeters.fill(300);
    roundtrip.itemCategoryNearMeters.fill(30);
    roundtrip.itemCategoryMidMeters.fill(150);
    roundtrip.showPlayerNames=false;
    roundtrip.showInfectedNames=false;
    roundtrip.showBoxes=false;
    roundtrip.hideDeadPlayers=false;
    roundtrip.itemAnchoredLists=true;
    roundtrip.itemUseIcons=true;
    roundtrip.itemFocusOnly=true;
    roundtrip.itemCategoryNameMeters[7]=42;
    roundtrip.itemCategoryColors[7]=0x46D9EA;
    roundtrip.showItemCategory[7]=false;
    roundtrip.itemToggleKey=0x75;
    roundtrip.coordinateCopyKey=0x76;
    roundtrip.itemSplitMeters=12;
    roundtrip.aimAssistEnabled=true;
    roundtrip.aimTargetInfected=false;
    roundtrip.aimShowFov=true;
    roundtrip.aimSoloCalibration=true;
    roundtrip.aimHoldKey=0x06;
    roundtrip.aimFovPixels=175;
    roundtrip.aimGateMeters=200;
    roundtrip.aimBone=2;
    roundtrip.aimHysteresisPercent=400;
    roundtrip.aimSnapStrength=20;
    roundtrip.aimRatePixelsPerSecond=700;
    roundtrip.aimAccelerationPixelsPerSecond2=3000;
    const bool saved=loaded && roundtrip.Save();
    Config reread{};
    Check(saved && reread.Load(temporaryConfig.wstring()) &&
          reread.gateMeters[4]==1 && reread.mapItemGateMeters==1350 &&
          reread.itemCategoryGateMeters[6]==300 &&
          reread.itemCategoryNearMeters[6]==30 && reread.itemCategoryMidMeters[6]==150,
          "master range preserves category settings");
    Check(!reread.showPlayerNames && !reread.showInfectedNames && !reread.showBoxes &&
          reread.EditValue("overlay","show_boxes","1") && reread.showBoxes,
          "entity display toggles persist and edit");
    Check(!reread.hideDeadPlayers &&
          reread.EditValue("overlay","hide_dead_players","1") && reread.hideDeadPlayers,
          "player corpse filter persists and edits");
    Check(reread.itemAnchoredLists && reread.itemUseIcons && reread.itemFocusOnly &&
          reread.itemCategoryNameMeters[7]==42 &&
          reread.itemCategoryColors[7]==0x46D9EA &&
          !reread.showItemCategory[7] && reread.itemToggleKey==0x75 &&
          reread.coordinateCopyKey==0x76 &&
          reread.itemSplitMeters==12,
          "item presentation category and hotkey persist");
    Check(reread.aimAssistEnabled && !reread.aimTargetInfected &&
          reread.aimShowFov &&
          reread.aimSoloCalibration && reread.aimHoldKey==0x06 &&
          reread.aimFovPixels==175 && reread.aimGateMeters==200 &&
          reread.aimBone==2 && reread.aimHysteresisPercent==400 &&
          reread.aimSnapStrength==20 &&
          reread.aimRatePixelsPerSecond==700 &&
          reread.aimAccelerationPixelsPerSecond2==3000,
          "assist settings persist through INI roundtrip");
    Check(visual::AimTargetEnabled(visual::EntityKind::Player,
                                   reread.showEntities,reread.aimTargetInfected) &&
          !visual::AimTargetEnabled(visual::EntityKind::Infected,
                                    reread.showEntities,reread.aimTargetInfected) &&
          visual::AimTargetEnabled(visual::EntityKind::Infected,
                                   reread.showEntities,true) &&
          !visual::AimTargetEnabled(visual::EntityKind::Animal,
                                    reread.showEntities,true),
          "infected assist toggle filters aim targets without affecting players");
    Check(reread.EditValue("overlay","item_gate_m","250") &&
          reread.gateMeters[4]==250 && reread.mapItemGateMeters==1350 &&
          reread.itemCategoryGateMeters[6]==300,
          "raising master range restores category coverage");
    Check(reread.EditValue("overlay","map_item_gate_m","900") &&
          reread.mapItemGateMeters==900 && reread.gateMeters[4]==250,
          "map item range edits independently of overlay range");
    std::filesystem::remove(temporaryConfig);
    const std::filesystem::path upgradePath=L"out\\config-upgrade-test.ini";
    {
        std::ifstream original("dayz.ini");
        std::ofstream upgradeFile(upgradePath);
        std::string line;
        while (std::getline(original,line)) {
            if (line.rfind("item_toggle_key=",0)==0) {
                upgradeFile<<"item_toggle_key=114\n";continue;
            }
            if (line.rfind("coordinate_copy_key=",0)!=0 &&
                line.rfind("item_name_m=",0)!=0 &&
                line.rfind("item_drinks_name_m=",0)!=0) upgradeFile<<line<<'\n';
        }
    }
    Config upgrade{};
    const bool upgradeLoaded=upgrade.Load(upgradePath.wstring());
    Check(upgradeLoaded && upgrade.itemToggleKey==0x72 &&
          upgrade.coordinateCopyKey==0x73,
          "legacy F3 item binding shifts new copy binding to F4");
    upgrade.itemToggleKey=0x74;
    upgrade.coordinateCopyKey=0x76;
    upgrade.itemNameMeters=70;
    upgrade.itemCategoryNameMeters[7]=42;
    const bool upgradeSaved=upgradeLoaded && upgrade.Save();
    Config upgraded{};
    Check(upgradeSaved && upgraded.Load(upgradePath.wstring()) &&
          upgraded.itemToggleKey==0x74 && upgraded.coordinateCopyKey==0x76 &&
          upgraded.itemNameMeters==70 &&
          upgraded.itemCategoryNameMeters[7]==42,
          "new item controls persist when upgrading an older INI");
    std::filesystem::remove(upgradePath);
    Check(visual::ClassifyItem("inventoryItem","SodaCan_Kvass",
          "dz\\gear\\drinks\\sodacan.p3d")==visual::ItemKind::Drinks &&
          visual::ClassifyItem("inventoryItem","FoodCan",
          "dz\\gear\\food\\can.p3d")==visual::ItemKind::Food,
          "drinks separate from food for quick filtering");
    Check(visual::RevealItemName(42.0f,42,true) &&
          !visual::RevealItemName(42.1f,42,true) &&
          visual::RevealItemName(200.0f,0,false),
          "name reveal threshold and icon override");
    Check(visual::AttachItemLabel(8.0f,8,true) &&
          !visual::AttachItemLabel(8.1f,8,true) &&
          !visual::AttachItemLabel(1.0f,8,false),
          "close labels attach only after name reveal");
    Check(visual::SameItemPile({0,0,0},{1.5f,0,0},160) &&
          !visual::SameItemPile({0,0,0},{2.0f,0,0},160) &&
          !visual::SameItemPile({0,0,0},{0,2.0f,0},160),
          "world-space pile radius separates distant floors");
    const std::uint64_t module=0x100000,gameWorld=0x200000,link=0x300000,entity=0x400000;
    reader.Put(module+0x100,gameWorld);
    reader.Put(gameWorld+0x20,link);
    reader.Put(link+8,entity+0xA8);
    game::LocalSample local{};
    Check(game::FetchLocal(reader,config,module,local) && local.entity==entity,"local success");
    local.entity=0xDEADBEEF;
    reader.failAddress=link+8;
    Check(!game::FetchLocal(reader,config,module,local) && local.entity==0xDEADBEEF,"local no partial on failure");
    reader.failAddress=0;
    reader.Put(link+8,std::uint64_t{0});
    Check(!game::FetchLocal(reader,config,module,local),"local null interior");
    reader.Put(link+8,entity+0xA8);
    const std::uint64_t header=gameWorld+0x80,data=0x500000;
    reader.Put(header,data);
    reader.Put(header+8,std::uint32_t{2});
    reader.Put(data,entity);
    reader.Put(data+8,entity+0x100);
    game::TableSample table{};
    Check(game::FetchTable(reader,config,gameWorld,0x80,8,table) && table.count==2 && table.entries.size()==2,"table success");
    table.count=77;
    reader.failAddress=data;
    Check(!game::FetchTable(reader,config,gameWorld,0x80,8,table) && table.count==77,"table no partial on failure");
    reader.failAddress=0;
    reader.Put(header+8,std::uint32_t{101});
    Check(!game::FetchTable(reader,config,gameWorld,0x80,8,table),"table cap");
    Check(!UserRange(0xFFFFFFFFFFFFFFFFULL,8),"range overflow");
    std::uint64_t sum=0;
    Check(!AddAddress(0xFFFFFFFFFFFFFFF0ULL,0x100,sum),"address overflow");
    const std::uint64_t cameraAddress=0x600000;
    reader.Put(gameWorld+0x40,cameraAddress);
    std::array<unsigned char,0xE4> cameraBytes{};
    reader.Put(cameraAddress,cameraBytes);
    reader.Put(cameraAddress+0x8,game::Vec3{1,0,0});
    reader.Put(cameraAddress+0x14,game::Vec3{0,1,0});
    reader.Put(cameraAddress+0x20,game::Vec3{0,0,1});
    reader.Put(cameraAddress+0x2C,game::Vec3{0,0,0});
    reader.Put(cameraAddress+0x58,game::Vec2{960,540});
    reader.Put(cameraAddress+0xD0,0.989f);
    reader.Put(cameraAddress+0xE0,0.556f);
    game::CameraSample camera{};
    Check(game::FetchCameraSample(reader,config,gameWorld,camera) && camera.object==cameraAddress,
          "camera complete sample");
    game::Vec2 screen{};
    Check(game::ProjectWorld(camera,{0,0,10},1920,1080,screen) &&
          std::abs(screen.x-960)<0.01f && std::abs(screen.y-540)<0.01f,"projection center");
    Check(!game::ProjectWorld(camera,{0,0,-10},1920,1080,screen),"projection behind");
    reader.Put(cameraAddress+0xD0,0.130867f);
    reader.Put(cameraAddress+0xE0,0.0736125f);
    Check(game::FetchCameraSample(reader,config,gameWorld,camera) &&
          game::ProjectWorld(camera,{1,0,10},1920,1080,screen) &&
          screen.x>1600 && screen.x<1920,"live hunting scope projection");
    reader.Put(cameraAddress+0xE0,0.0f);
    camera.object=0xDEADBEEF;
    Check(!game::FetchCameraSample(reader,config,gameWorld,camera) &&
          camera.object==0xDEADBEEF,"invalid scoped projection publishes nothing");
    reader.Put(cameraAddress+0xD0,0.989f);
    reader.Put(cameraAddress+0xE0,0.556f);
    Check(game::FetchCameraSample(reader,config,gameWorld,camera) &&
          game::ProjectWorld(camera,{1,0,10},1920,1080,screen) &&
          screen.x>1000 && screen.x<1100,"projection recovers after scope");
    {
        FakeReader opticReader{};
        constexpr std::uint64_t opticLocal=0xA00000,playerInv=0xA10000,hands=0xA20000;
        constexpr std::uint64_t weaponInv=0xA30000,entries=0xA40000,optic=0xA50000;
        constexpr std::uint64_t opticType=0xA60000,model=0xA70000,steps=0xA80000;
        opticReader.Put(opticLocal+0x5D8,playerInv);
        opticReader.Put(playerInv+0x190,hands);
        opticReader.Put(hands+0x5D8,weaponInv);
        std::array<std::uint8_t,16> opticHeader{};opticReader.Put(weaponInv+0x130,opticHeader);
        opticReader.Put(weaponInv+0x130,entries);
        opticReader.Put(weaponInv+0x13c,std::uint32_t{1});
        std::array<std::uint8_t,16> slot{};opticReader.Put(entries,slot);
        opticReader.Put(entries+8,optic);
        std::array<std::uint8_t,10> state{};opticReader.Put(optic+0x638,state);
        opticReader.Put(optic+0x640,std::uint8_t{1});
        opticReader.Put(optic+0x641,std::uint8_t{0});
        opticReader.Put(optic+0x130,opticType);
        std::array<std::uint8_t,132> typeRaw{};opticReader.Put(opticType+0x930,typeRaw);
        opticReader.Put(opticType+0x930,model);
        opticReader.Put(model+0x10,std::array<char,2>{'-','\0'});
        opticReader.Put(opticType+0x960,0.112171f);
        game::CameraSample pipCamera=camera;
        pipCamera.tanHorizontal=0.149562f;pipCamera.tanVertical=0.0841286f;
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::PictureInPicture &&
              std::abs(pipCamera.opticFov-0.112171f)<0.00001f &&
              std::abs(pipCamera.mainTanHorizontal-0.149562f)<0.00001f &&
              opticReader.maxReadSize<=4096,
              "PU optic preserves both complete view projections");
        bool mainView=true;
        Check(game::ProjectWorld(pipCamera,{0.3f,0,10},1920,1080,screen,&mainView) &&
              !mainView && screen.x>1190 && screen.x<1210,
              "PIP lens uses calibrated optic projection");
        Check(game::ProjectWorld(pipCamera,{1,0,10},1920,1080,screen,&mainView) &&
              mainView && screen.x>1590 && screen.x<1620,
              "outside PIP lens uses the main camera projection");
        const auto outsideCamera=game::SelectPictureInPictureView(pipCamera,true);
        Check(game::ProjectWorld(outsideCamera,{1,0,10},1920,1080,screen) &&
              screen.x>1590 && screen.x<1620,
              "whole skeleton can select one consistent main view");
        opticReader.Put(optic+0x640,std::uint8_t{0});
        pipCamera=camera;pipCamera.tanHorizontal=0.149562f;
        pipCamera.tanVertical=0.0841286f;
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::Main && pipCamera.tanHorizontal==0.149562f,
              "inactive optic leaves main projection unchanged");
        opticReader.Put(optic+0x640,std::uint8_t{1});
        opticReader.Put(model+0x10,std::array<char,2>{'a','\0'});
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::Main && pipCamera.tanHorizontal==0.149562f,
              "2D optic model keeps main camera projection");
        opticReader.Put(model+0x10,std::array<char,2>{'-','\0'});
        opticReader.Put(opticType+0x9a8,steps);
        opticReader.Put(opticType+0x9b0,std::uint32_t{2});
        opticReader.Put(steps,std::array<float,2>{0.11f,0.09f});
        opticReader.Put(optic+0x638,std::uint32_t{1});
        pipCamera=camera;pipCamera.tanHorizontal=0.149562f;
        pipCamera.tanVertical=0.0841286f;
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::PictureInPicture &&
              std::abs(pipCamera.opticFov-0.09f)<0.00001f,
              "adjustable optic follows current FOV step");
        pipCamera=camera;pipCamera.tanHorizontal=0.149562f;
        pipCamera.tanVertical=0.0841286f;
        opticReader.failAddress=steps+sizeof(float);
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::Invalid && pipCamera.tanHorizontal==0.149562f,
              "optic FOV read failure publishes no partial projection");
        opticReader.failAddress=0;
        opticReader.Put(optic+0x638,std::uint32_t{3});
        Check(game::ApplyActiveOpticProjection(opticReader,config,opticLocal,pipCamera)==
              game::OpticProjection::Invalid && pipCamera.tanHorizontal==0.149562f,
              "out-of-range optic step rejected without publication");
    }
    camera.object=0xDEADBEEF;
    reader.failAddress=cameraAddress;
    Check(!game::FetchCameraSample(reader,config,gameWorld,camera) && camera.object==0xDEADBEEF,
          "camera no partial on failure");
    reader.failAddress=0;
    reader.Put(entity+0x156,std::uint8_t{194});
    Check(game::CandidateInfectedAlive(reader,config,entity),"infected live bit clear");
    Check(game::CandidatePlayerAlive(reader,config,entity),"player live bit clear");
    reader.Put(entity+0x156,std::uint8_t{198});
    Check(!game::CandidateInfectedAlive(reader,config,entity),"infected dead bit set");
    Check(!game::CandidatePlayerAlive(reader,config,entity),"player corpse bit set");
    reader.failAddress=entity+0x156;
    Check(!game::CandidateInfectedAlive(reader,config,entity),"infected failed state read suppresses marker");
    Check(!game::CandidatePlayerAlive(reader,config,entity),"player failed state read suppresses visualization");
    reader.failAddress=0;
    const std::uint64_t entityType=0x700000,typeName=0x710000,cleanName=0x720000;
    reader.Put(entity+0x130,std::array<unsigned char,0x30>{});
    reader.Put(entity+0x130,entityType);
    reader.Put(entityType+0xD0,typeName);
    reader.Put(entityType+0x4C0,cleanName);
    std::array<char,64> typeBytes{},cleanBytes{};
    std::memcpy(typeBytes.data(),"dayzinfected",13);
    std::memcpy(cleanBytes.data(),"Field Infected",15);
    reader.Put(typeName+0x10,typeBytes);
    reader.Put(cleanName+0x10,cleanBytes);
    std::string parsed="old";
    Check(game::ReadName(reader,config,entity,parsed) && parsed=="dayzinfected",
          "entity type parse");
    Check(game::ReadTypeText(reader,config,entityType,"TypeCleanCandidate",parsed) &&
          parsed=="Field Infected","type clean text parse");
    reader.failAddress=cleanName+0x10;
    Check(!game::ReadTypeText(reader,config,entityType,"TypeCleanCandidate",parsed) &&
          parsed=="Field Infected","type text no partial publication");
    reader.failAddress=0;
    const std::uint64_t visualState=0x730000;
    reader.Put(entity+0x158,visualState);
    reader.Put(visualState+0x2C,game::Vec3{10,2,30});
    game::Vec3 parsedPosition{};
    Check(game::CandidatePosition(reader,config,entity,parsedPosition) &&
          parsedPosition.x==10 && parsedPosition.z==30,"entity position parse");
    Check(game::CandidatePositionWithType(reader,config,entity,entityType,parsedPosition) &&
          parsedPosition.x==10,"batched type and position parse");
    Check(!game::CandidatePositionWithType(reader,config,entity,entityType+1,parsedPosition) &&
          parsedPosition.x==10,"batched type mismatch no publication");
    std::array<float,12> facingMatrix{{1,0,0,0,1,0,0.6f,0,0.8f,10,2,30}};
    reader.Put(visualState+0x8,facingMatrix);
    const auto positionReads=reader.readsAt[visualState+0x2C];
    const auto matrixReads=reader.readsAt[visualState+0x8];
    game::Vec3 playerFacing{};
    Check(game::CandidatePositionWithType(reader,config,entity,entityType,
                                          parsedPosition,&playerFacing) &&
          parsedPosition.z==30 && std::abs(playerFacing.x-0.6f)<0.001f &&
          std::abs(playerFacing.z-0.8f)<0.001f &&
          reader.readsAt[visualState+0x8]==matrixReads+1 &&
          reader.readsAt[visualState+0x2C]==positionReads,
          "map heading shares one complete position read");
    reader.failAddress=visualState+0x8;
    const auto retainedPosition=parsedPosition;
    const auto retainedFacing=playerFacing;
    Check(!game::CandidatePositionWithType(reader,config,entity,entityType,
                                           parsedPosition,&playerFacing) &&
          parsedPosition.z==retainedPosition.z &&
          playerFacing.x==retainedFacing.x,
          "failed facing matrix publishes neither position nor heading");
    reader.failAddress=0;
    facingMatrix[6]=std::nanf("");
    reader.Put(visualState+0x8,facingMatrix);
    Check(game::CandidatePositionWithType(reader,config,entity,entityType,
                                          parsedPosition,&playerFacing) &&
          playerFacing.x==0 && playerFacing.z==0,
          "invalid facing basis omits cone but retains valid position");
    config.offsets["VisualStateMatrix"]=0x9;
    Check(game::CandidatePositionWithType(reader,config,entity,entityType,
                                          parsedPosition,&playerFacing) &&
          parsedPosition.z==30 && playerFacing.z==0,
          "unavailable matrix layout keeps the original position sample");
    config.offsets["VisualStateMatrix"]=0x8;
    Check(visual::ClassifyEntity("dayzplayer")==visual::EntityKind::Player &&
          visual::ClassifyEntity("dayzinfected")==visual::EntityKind::Infected &&
          visual::ClassifyEntity("dayzanimal")==visual::EntityKind::Animal &&
          visual::ClassifyEntity("car")==visual::EntityKind::Vehicle &&
          visual::ClassifyEntity("house")==visual::EntityKind::Other,"entity classification");
    Check(visual::ClassifyItem("ProxyMagazines","Ammo_357","dz\\weapons\\ammunition\\357.p3d") ==
          visual::ItemKind::Ammo &&
          visual::ClassifyItem("clothing","MiniDress_Pink","dz\\characters\\tops\\dress.p3d") ==
          visual::ItemKind::Clothing &&
          visual::ClassifyItem("inventoryItem","Hammer","dz\\gear\\tools\\hammer.p3d") ==
          visual::ItemKind::Tools,"item classification");
    Check(visual::WithinGate(1200,1200) && !visual::WithinGate(1200.1f,1200) &&
          !visual::WithinGate(-1,1200),"per type distance gate");
    Check(std::strcmp(visual::CardinalDirection({-0.149f,-0.095f,0.984f}),"N")==0 &&
          std::strcmp(visual::CardinalDirection({1,0,0}),"E")==0 &&
          std::strcmp(visual::CardinalDirection({0,0,-1}),"S")==0 &&
          std::strcmp(visual::CardinalDirection({-1,0,0}),"W")==0,
          "compass axes and live north calibration");
    Check(std::strcmp(visual::CardinalDirection({1,0,1}),"NE")==0 &&
          std::strcmp(visual::CardinalDirection({-1,0,-1}),"SW")==0 &&
          std::strcmp(visual::CardinalDirection({0,1,0}),"--")==0,
          "intercardinal and vertical view heading");
    char mapCoordinates[32]{};
    Check(visual::FormatIzurviveCoordinates(mapCoordinates,sizeof(mapCoordinates),
              {11280.870f,166.068f,2569.531f}) &&
          std::strcmp(mapCoordinates,"11281/2570")==0,
          "iZurvive pair uses horizontal X/Z and omits altitude");
    char shortCoordinates[4]{};
    Check(!visual::FormatIzurviveCoordinates(shortCoordinates,
              sizeof(shortCoordinates),{11280.870f,166.068f,2569.531f}) &&
          shortCoordinates[0]=='\0',"iZurvive pair rejects truncated output");
    std::vector<std::uint64_t> members{0x3000,0x1000,0x2000};
    visual::SortMembership(members);
    Check(visual::HasMember(members,0x1000) && visual::HasMember(members,0x3000),
          "membership survives source reordering");
    members={0x4000,0x3000,0x1000,0x2000};
    visual::SortMembership(members);
    Check(visual::HasMember(members,0x1000) && visual::HasMember(members,0x4000) &&
          !visual::HasMember(members,0x5000),"membership covers newly appended slots");
    std::array<bool,4> enabled{{true,false,true,true}};
    Check(visual::SkeletonEnabled(visual::EntityKind::Player,enabled,true) &&
          !visual::SkeletonEnabled(visual::EntityKind::Infected,enabled,true) &&
          !visual::SkeletonEnabled(visual::EntityKind::Animal,enabled,true) &&
          !visual::SkeletonEnabled(visual::EntityKind::Player,enabled,false),
          "entity category gates skeleton sampling");
    Check(visual::AdaptiveReadCap(0,6000)==1200 &&
          visual::AdaptiveReadCap(200,6000)==2624 &&
          visual::AdaptiveReadCap(1000,6000)==6000,"adaptive bounded read cap");
    const auto sparseCap=visual::AdaptiveReadCap(0,6000);
    const auto denseCap=visual::AdaptiveReadCap(1000,6000);
    Check(sparseCap-visual::ScanSampleCap(sparseCap)>=96 &&
          denseCap-visual::ScanSampleCap(denseCap)>=96 &&
          visual::ScanSampleCap(128)>8,
          "cache refresh retains progress budget after frame sampling");
    Check(visual::Band(20,30,300,1200,true)==visual::ItemBand::Near &&
          visual::Band(100,30,300,1200,true)==visual::ItemBand::Mid &&
          visual::Band(800,30,300,1200,true)==visual::ItemBand::Far &&
          visual::Band(800,30,300,1200,false)==visual::ItemBand::Hidden,"item bands");
    char label[128]{};
    visual::EntityLabel(label,sizeof(label),"Alice","Hatchet",41);
    Check(std::strcmp(label,"Alice | Hatchet | 41 m")==0,"entity label");
    visual::EntityLabel(label,sizeof(label),"","",41);
    Check(std::strcmp(label,"41 m")==0,"entity distance remains without names");
    visual::ItemLabel(label,sizeof(label),"Hatchet",20,0x01C20002,
        visual::ItemBand::Near,false,true,true);
    Check(std::strcmp(label,"Hatchet | 20 m | Damaged")==0,"near item label");
    visual::ItemLabel(label,sizeof(label),"Hatchet",20,0x01C20002,
        visual::ItemBand::Near,true,true,true);
    Check(std::strcmp(label,"Hatchet")==0,"compact item label");
    Check(visual::Cell(63,63,64,30,17)==0 && visual::Cell(64,0,64,30,17)==1 &&
          visual::Cell(-1,0,64,30,17)==UINT32_MAX,"density grid cell");
    Check(visual::NewItemWins(5,0,10,0) && !visual::NewItemWins(10,0,5,0) &&
          visual::NewItemWins(10,1,5,0),"density closest and priority");
    Check(visual::SuppressNearbyItem(65,0,64,false) &&
          !visual::SuppressNearbyItem(65,0,64,true),
          "density spacing uses entry and exit hysteresis");
    Check(!visual::SuppressNearbyItem(75,0,64,false) &&
          visual::SuppressNearbyItem(40,40,64,true),
          "density spacing follows screen distance across cell edges");
    visual::Box box{};
    reader.failAddress=0;
    Check(visual::ProjectBox(camera,{0,0,10},1.8f,0.46f,1920,1080,box) &&
          box.left<960 && box.right>960 && box.bottom>box.top,"projected box");
    config.offsets["PlayerSkeletonCandidate"]=0xB98;
    config.offsets["InfectedSkeletonCandidate"]=0x620;
    config.offsets["SkeletonAnimFromRoot"]=0x190;
    config.offsets["SkeletonMatrixSourceCandidate"]=0x188;
    config.offsets["SkeletonMatrixArrayCandidate"]=0x38;
    config.offsets["AnimData"]=0x20;
    config.offsets["AnimBoneCount"]=0x68;
    config.offsets["BoneMap"]=0xE0;
    config.offsets["BoneMapControls"]=0x58;
    config.offsets["BoneMapCount"]=0x60;
    config.offsets["BoneMapCapacity"]=0x68;
    config.offsets["BoneMapEntries"]=0x70;
    config.offsets["BoneMapEntryStride"]=8;
    config.offsets["BoneStride"]=0x30;
    config.offsets["BoneTranslationCandidate"]=0x24;
    config.offsets["VisualStateMatrix"]=0x8;
    constexpr const char* boneNames[]={"head","neck","spine","pelvis",
        "leftshoulder","leftforearm","lefthand","rightshoulder","rightforearm",
        "righthand","leftupleg","leftleg","leftfoot","rightupleg","rightleg","rightfoot"};
    const std::uint64_t root=0x740000,anim=0x750000,dataBone=0x760000;
    const std::uint64_t controlsAddress=0x770000,entriesAddress=0x780000,arrayAddress=0x790000;
    reader.Put(entity+0xB98,root);
    reader.Put(root+0x188,anim);reader.Put(root+0x190,anim);
    reader.Put(anim+0x20,dataBone);reader.Put(anim+0x38,arrayAddress);
    reader.Put(anim+0x68,std::uint32_t{16});
    reader.Put(dataBone+0xE0+0x58,controlsAddress);
    reader.Put(dataBone+0xE0+0x60,std::uint32_t{16});
    reader.Put(dataBone+0xE0+0x68,std::uint32_t{64});
    reader.Put(dataBone+0xE0+0x70,entriesAddress);
    std::array<std::uint8_t,64> controls{};controls.fill(0x80);
    std::array<std::uint32_t,128> entries{};
    for (std::uint32_t i=0;i<16;++i) {
        const auto hash=game::BoneHash(boneNames[i]);
        auto slot=(hash>>7)&63u;
        while (controls[slot]!=0x80) slot=(slot+1)&63u;
        controls[slot]=static_cast<std::uint8_t>(hash&0x7fu);
        entries[slot*2]=hash;entries[slot*2+1]=i;
    }
    reader.Put(controlsAddress,controls);reader.Put(entriesAddress,entries);
    std::array<std::uint8_t,16*0x30> matrixBytes{};
    for (std::size_t i=0;i<16;++i) {
        const game::Vec3 translation{float(i%4)*0.1f,float(i/4)*0.3f,0.0f};
        std::memcpy(matrixBytes.data()+i*0x30+0x24,&translation,sizeof(translation));
    }
    reader.Put(arrayAddress,matrixBytes);
    reader.Put(visualState+0x8,std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,10});
    game::BoneBinding binding{};
    Check(game::ResolveBones(reader,config,entity,entityType,false,binding) &&
          binding.indices[0]==0 && binding.indices[15]==15,
          "bone names resolve by bounded hash map");
    game::BonePose pose{};
    const auto matrixReadsBefore=reader.readsAt[arrayAddress];
    Check(game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          pose.screen[0].x==960 && pose.screen[0].y==540 &&
          reader.readsAt[arrayAddress]==matrixReadsBefore+1 && reader.maxReadSize<=4096,
          "bone pose transform and contiguous matrix read");
    reader.Put(visualState+0x8,std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,1});
    reader.Put(arrayAddress+15*0x30+0x24,game::Vec3{4,0,0});
    Check(game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          pose.visible[0] && !pose.visible[15],
          "offscreen joint clips without discarding complete pose");
    reader.Put(visualState+0x8,std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,10});
    const auto gatedReads=reader.readsAt[arrayAddress];
    Check(!game::SampleBonePose(reader,config,binding,{1000,0,0},300,camera,1920,1080,pose) &&
          reader.readsAt[arrayAddress]==gatedReads,"skeleton distance gate before matrix read");
    Check(game::SampleBonePose(reader,config,binding,{0,0,900},1000,camera,1920,1080,pose) &&
          reader.readsAt[arrayAddress]==gatedReads+1,
          "selected long-range bone sample honors 1000 m gate");
    pose.screen[0]={77,88};reader.failAddress=arrayAddress;
    Check(!game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          pose.screen[0].x==77,"bone read failure publishes no partial pose");
    reader.failAddress=0;
    const float invalid=std::numeric_limits<float>::quiet_NaN();
    reader.Put(arrayAddress+0x24,invalid);
    Check(!game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          pose.screen[0].x==77,"invalid bone translation suppresses whole pose");
    std::array<std::uint8_t,135*0x30> wideMatrices{};
    reader.Put(arrayAddress,wideMatrices);
    binding.indices[15]=134;binding.count=172;
    reader.Put(anim+0x68,std::uint32_t{172});
    const auto firstBlockBefore=reader.readsAt[arrayAddress];
    const auto secondBlockBefore=reader.readsAt[arrayAddress+4032];
    Check(game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          reader.readsAt[arrayAddress]==firstBlockBefore+1 &&
          reader.readsAt[arrayAddress+4032]==secondBlockBefore+1 &&
          reader.maxReadSize<=4096,"wide bone span uses bounded contiguous blocks");
    pose.screen[0]={66,77};reader.failAddress=arrayAddress+4032;
    Check(!game::SampleBonePose(reader,config,binding,{0,0,0},300,camera,1920,1080,pose) &&
          pose.screen[0].x==66,"second bone block failure publishes nothing");
    reader.failAddress=0;
    constexpr const char* healthKeys[]={"HealthWatch1","HealthWatch2","HealthWatch3","HealthWatch4",
        "HealthWatch5","HealthWatch6","HealthWatch7","HealthWatch8"};
    constexpr std::uint64_t healthOffsets[]={0x120,0x14C,0x150,0x230,0x734,0x794,0x798,0xBC0};
    for (std::size_t i=0;i<8;++i) {
        config.offsets[healthKeys[i]]=healthOffsets[i];
        reader.Put(entity+healthOffsets[i],float(i+1));
    }
    game::HealthWatch healthWatch{};
    Check(game::SampleHealthWatch(reader,config,entity,healthWatch) &&
          healthWatch.values[0]==1.0f && healthWatch.values[7]==8.0f &&
          healthWatch.plausible[7],"bounded local float watcher");
    healthWatch.values[0]=777.0f;reader.failAddress=entity+0x734;
    Check(!game::SampleHealthWatch(reader,config,entity,healthWatch) &&
          healthWatch.values[0]==777.0f,"float watcher no partial publication");
    reader.failAddress=0;
    config.offsets["DeathWatchWord1"]=0x154;
    config.offsets["DeathWatchWord2"]=0x230;
    config.offsets["DeathWatchWord3"]=0xBC0;
    config.offsets["DeathWatchByte1"]=0x156;
    reader.Put(entity+0x154,std::uint32_t{0x01020304});
    reader.Put(entity+0x156,std::uint8_t{0x55});
    game::DeathWatch deathWatch{};
    Check(game::SampleDeathWatch(reader,config,entity,deathWatch) &&
          deathWatch.byte==0x55,"bounded death watcher");
    deathWatch.words[0]=0xDEADBEEF;reader.failAddress=entity+0x156;
    Check(!game::SampleDeathWatch(reader,config,entity,deathWatch) &&
          deathWatch.words[0]==0xDEADBEEF,"death watcher no partial publication");
    {
        std::vector<aim::Candidate> candidates{
            {101,{1010.0f,540.0f},40.0f,false},
            {202,{1005.0f,540.0f},35.0f,false}};
        auto selected=aim::Select(candidates,101,1920.0f,1080.0f,140.0f,400.0f);
        Check(selected.entity==202,"assist prioritizes nearer screen center over prior lock");
        candidates[1].screen.x=980.0f;
        selected=aim::Select(candidates,101,1920.0f,1080.0f,140.0f,25.0f);
        Check(selected.entity==202,"assist selects the center-nearest target");
        candidates[0].screen.x=1200.0f;candidates[1].screen.x=1201.0f;
        Check(aim::Select(candidates,202,1920.0f,1080.0f,140.0f,25.0f).entity==0,
              "assist loses target outside cone");
        const std::vector<aim::Candidate> stickyCandidates{
            {101,{1140.0f,540.0f},40.0f,false},
            {202,{1080.0f,540.0f},35.0f,false}};
        const auto sticky=aim::Select(stickyCandidates,101,1920.0f,1080.0f,
                                      140.0f,400.0f);
        Check(sticky.entity==202 &&
              aim::Select(stickyCandidates,0,1920.0f,1080.0f,
                           140.0f,400.0f).entity==202 &&
              aim::Select(stickyCandidates,101,1920.0f,1080.0f,
                           140.0f,0.0f).entity==202,
              "high stickiness cannot retain a target outside visible FOV");
        const std::vector<aim::Candidate> nearTie{
            {101,{990.2f,540.0f},40.0f,false},
            {202,{990.0f,540.0f},35.0f,false}};
        const auto tie=aim::Select(nearTie,101,1920.0f,1080.0f,140.0f,400.0f);
        Check(tie.entity==101 && tie.retained &&
              aim::TieTolerancePixels(400.0f)==1.0f &&
              aim::Select(nearTie,0,1920.0f,1080.0f,
                          140.0f,400.0f).entity==202,
              "stickiness stabilizes only a near-exact center tie");
        Check(aim::Select({{101,{1101.0f,540.0f},40.0f,false},
                           {202,{1090.0f,540.0f},35.0f,false}},101,
                          1920.0f,1080.0f,140.0f,400.0f).entity==202 &&
              aim::Select({{101,{1101.0f,540.0f},40.0f,false}},101,
                          1920.0f,1080.0f,140.0f,400.0f).entity==0,
              "maximum stickiness cannot retain an off-circle target");
        aim::Motion motion{};
        game::Vec2 target{1180.0f,620.0f};
        bool bounded=true;
        for (int i=0;i<240;++i) {
            const auto step=aim::Step(motion,target,1920.0f,1080.0f,
                                      1.0f/60.0f,900.0f,3600.0f);
            bounded&=std::abs(step.x)<=15 && std::abs(step.y)<=15;
            target.x-=static_cast<float>(step.x);
            target.y-=static_cast<float>(step.y);
        }
        Check(bounded && aim::CenterDistance(target,1920.0f,1080.0f)<=
              aim::SettleRadiusPixels,
              "assist time-based closed-loop motion converges with bounded steps");
        motion.Reset();
        Check(motion.velocityX==0 && motion.velocityY==0 &&
              motion.remainderX==0 && motion.remainderY==0 && !motion.settled,
              "assist release clears motion immediately");
        const auto quiet=aim::Step(motion,{962.0f,540.0f},1920.0f,1080.0f,
                                   1.0f/60.0f,900.0f,3600.0f);
        bool quietBand=motion.settled && quiet.x==0 && quiet.y==0;
        for (int i=0;i<30;++i) {
            const float wobble=i%2?5.0f:-5.0f;
            const auto step=aim::Step(motion,{960.0f+wobble,540.0f},
                                      1920.0f,1080.0f,1.0f/60.0f,900.0f,3600.0f);
            quietBand&=step.x==0 && step.y==0;
        }
        Check(quietBand,"assist settling ignores small projection jitter");
        motion.Reset();
        game::Vec2 delayed{1080.0f,540.0f};
        int firstPending=0,secondPending=0,tailMovement=0;
        for (int i=0;i<600;++i) {
            const auto step=aim::Step(motion,delayed,1920.0f,1080.0f,
                                      1.0f/60.0f,900.0f,3600.0f);
            delayed.x-=static_cast<float>(firstPending);
            firstPending=secondPending;secondPending=step.x;
            if (i>=480 && step.x!=0) ++tailMovement;
        }
        Check(std::abs(delayed.x-960.0f)<=aim::ResumeRadiusPixels &&
              tailMovement==0,"assist settles with delayed screen feedback");
        aim::Motion lowSnap{},highSnap{};
        game::Vec2 lowTarget{1080.0f,540.0f},highTarget=lowTarget;
        for (int i=0;i<12;++i) {
            const auto low=aim::Step(lowSnap,lowTarget,1920.0f,1080.0f,
                                      1.0f/60.0f,900.0f,3600.0f,1.0f);
            const auto high=aim::Step(highSnap,highTarget,1920.0f,1080.0f,
                                       1.0f/60.0f,900.0f,3600.0f,10.0f);
            lowTarget.x-=static_cast<float>(low.x);
            highTarget.x-=static_cast<float>(high.x);
        }
        Check(highTarget.x<lowTarget.x,
              "higher snap strength closes the same target error faster");
        aim::Motion oldMaximum{},extendedMaximum{};
        const auto oldStep=aim::Step(oldMaximum,{1080.0f,540.0f},
            1920.0f,1080.0f,1.0f/60.0f,900.0f,3600.0f,10.0f);
        const auto extendedStep=aim::Step(extendedMaximum,{1080.0f,540.0f},
            1920.0f,1080.0f,1.0f/60.0f,900.0f,3600.0f,20.0f);
        Check(extendedStep.x>oldStep.x && extendedStep.y==0,
              "extended snap range exceeds the prior maximum response");
        motion.Reset();delayed={1080.0f,540.0f};
        firstPending=secondPending=tailMovement=0;
        for (int i=0;i<600;++i) {
            const auto step=aim::Step(motion,delayed,1920.0f,1080.0f,
                                      1.0f/60.0f,900.0f,3600.0f,20.0f);
            delayed.x-=static_cast<float>(firstPending);
            firstPending=secondPending;secondPending=step.x;
            if (i>=480 && step.x!=0) ++tailMovement;
        }
        Check(std::abs(delayed.x-960.0f)<=aim::ResumeRadiusPixels &&
              tailMovement==0,"extended maximum snap settles with delayed feedback");
        game::Vec3 predicted{77.0f,77.0f,77.0f};
        Check(aim::Lead({10.0f,2.0f,10.0f},{4.0f,0.0f,0.0f},100.0f,
                        200.0f,predicted) && predicted.x==12.0f,
              "prediction lead math for valid speed");
        predicted={77.0f,77.0f,77.0f};
        Check(!aim::Lead({10.0f,2.0f,10.0f},{4.0f,0.0f,0.0f},100.0f,
                         0.0f,predicted) && predicted.x==77.0f,
              "invalid projectile speed publishes no predicted point");
    }
    {
        aim::VelocityHistory history{};
        game::Vec3 velocity{};
        bool measured=false;
        for (std::uint64_t frame=1;frame<=41;++frame)
            measured=history.Update(0x123450,{1.7f*static_cast<float>(frame-1)*0.01f,0,0},
                1000+(frame-1)*10,frame,200,25.0f,velocity);
        Check(measured && std::abs(velocity.x-1.7f)<0.02f,
              "fresh identity-matched walk positions derive velocity");
        for (std::uint64_t frame=42;frame<=81;++frame)
            measured=history.Update(0x123450,
                {0.68f+4.5f*static_cast<float>(frame-41)*0.01f,0,0},
                1000+(frame-1)*10,frame,200,25.0f,velocity);
        Check(measured && std::abs(velocity.x-4.5f)<0.02f,
              "derived velocity tracks sprint after the configured window");
        for (std::uint64_t frame=82;frame<=121;++frame)
            measured=history.Update(0x123450,{2.48f,0,0},
                1000+(frame-1)*10,frame,200,25.0f,velocity);
        Check(measured && std::abs(velocity.x)<0.02f,
              "derived velocity settles to zero after stopping");
        velocity={77,0,0};
        Check(!history.Update(0x123450,{1000,0,0},2210,122,200,25.0f,velocity) &&
              velocity.x==77,
              "teleport rejected without publishing a velocity");
        Check(!history.Update(0x123451,{0,0,0},2220,123,200,25.0f,velocity) &&
              history.entity==0x123451,
              "identity change resets velocity history");
        Check(!history.Update(0x123451,{0,0,0},2500,140,200,25.0f,velocity) &&
              history.count==1,
              "missed frames clear rather than reuse stale velocity");
        game::Vec3 intercept{77,77,77};
        Check(aim::Intercept({0,0,0},{100,0,0},{0,0,10},100,intercept) &&
              std::abs(intercept.z-10.05f)<0.05f,
              "iterative intercept leads lateral motion");
        Check(aim::Intercept({0,0,0},{1000,0,0},{0,0,6.8f},808.5f,intercept) &&
              std::abs(intercept.z-8.41f)<0.15f,
              "intercept computes 1000 m lead with observed B95 speed");
        intercept={77,77,77};
        Check(!aim::Intercept({0,0,0},{100,0,0},{0,0,10},0,intercept) &&
              intercept.x==77,"invalid intercept speed publishes nothing");
    }
    {
        auto c=Setup();FakeReader memory{};
        c.offsets["TypeClassCandidate"]=0x98;
        c.offsets["WeaponCurrentMuzzle"]=0x620;
        c.offsets["WeaponMuzzleArray"]=0x628;
        c.offsets["WeaponMuzzleCount"]=0x634;
        c.offsets["WeaponMuzzleEntryStride"]=0x100;
        c.offsets["ChamberAmmoType"]=0x20;
        c.offsets["MuzzleAuxiliary"]=0x10;
        c.offsets["AmmoTypeInitSpeed"]=0x394;
        c.offsets["MuzzleAuxiliarySpeedMultiplier"]=0xB4;
        constexpr std::uint64_t testEntity=0x100000,inventory=0x200000,weapon=0x300000;
        constexpr std::uint64_t type=0x400000,name=0x500000,array=0x600000;
        constexpr std::uint64_t ammo=0x700000,auxiliary=0x800000;
        memory.Put(testEntity+0x5D8,inventory);
        memory.Put(inventory+0x190,weapon);
        memory.Put(weapon+0x130,type);
        memory.Put(type+0x98,name);
        std::array<char,64> classText{};
        std::memcpy(classText.data(),"TestRifle",10);
        memory.Put(name+0x10,classText);
        memory.Put(weapon+0x620,std::array<std::uint8_t,24>{});
        memory.Put(weapon+0x620,std::uint32_t{0});
        memory.Put(weapon+0x628,array);
        memory.Put(weapon+0x634,std::uint32_t{1});
        memory.Put(array+0x10,auxiliary);
        memory.Put(array+0x20,ammo);
        memory.Put(ammo+0x394,800.0f);
        memory.Put(auxiliary+0xB4,1.1f);
        game::HeldWeapon held{};game::MuzzleBinding muzzleBinding{};
        Check(game::ResolveHeldWeapon(memory,c,testEntity,held) &&
              game::ResolveMuzzleBinding(memory,c,held,muzzleBinding) &&
              muzzleBinding.weaponClass=="TestRifle" && muzzleBinding.count==1,
              "selected chamber resolves with a stable held-weapon identity");
        game::MuzzleSpeed speed{};
        Check(game::SampleMuzzleSpeed(memory,c,muzzleBinding,speed) &&
              std::abs(speed.effective-880.0f)<0.01f,
              "ammo initSpeed and active multiplier yield effective speed");
        speed={77,77,77};memory.failAddress=ammo+0x394;
        Check(!game::SampleMuzzleSpeed(memory,c,muzzleBinding,speed) &&
              speed.effective==77,"failed ammo read cannot publish partial speed");
        memory.failAddress=0;
        memory.Put(weapon+0x620,std::uint32_t{1});
        Check(!game::SampleMuzzleSpeed(memory,c,muzzleBinding,speed) &&
              speed.effective==77,"muzzle transition rejects prior speed binding");
        memory.Put(weapon+0x620,std::uint32_t{0});
        memory.Put(inventory+0x190,std::uint64_t{0x900000});
        Check(!game::ValidateHeldWeapon(memory,c,held),
              "weapon swap invalidates configured fallback identity");
    }
    {
        const std::filesystem::path path=L"out\\prediction-config-test.ini";
        std::filesystem::copy_file(L"dayz.ini",path,
            std::filesystem::copy_options::overwrite_existing);
        Config prediction{};
        bool ok=prediction.Load(path.wstring());
        prediction.aimPredictionEnabled=true;
        prediction.skeletonGateMeters=300;
        prediction.aimGateMeters=1000;
        prediction.aimPrebindGateMeters=250;
        prediction.aimPredictionGateMeters=1000;
        prediction.aimVelocityWindowMs=220;
        prediction.aimMuzzleFallbacks["TestRifle"]=850.0f;
        ok=ok && prediction.Save();
        Config predictionLoaded{};ok=ok && predictionLoaded.Load(path.wstring());
        Check(ok && predictionLoaded.aimPredictionEnabled &&
              predictionLoaded.skeletonGateMeters==300 &&
              predictionLoaded.aimGateMeters==1000 &&
              predictionLoaded.aimPrebindGateMeters==250 &&
              predictionLoaded.aimPredictionGateMeters==1000 &&
              predictionLoaded.aimVelocityWindowMs==220 &&
              predictionLoaded.AimFallbackMps("TestRifle")==850.0f,
              "long-range prediction and per-class speed persist through INI");
        std::filesystem::remove(path);
    }
    std::printf("checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
