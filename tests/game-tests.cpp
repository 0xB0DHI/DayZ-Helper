#include "../src/game/game.h"
#include "../src/game/visualization.h"
#include "../src/game/skeleton.h"
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
               {"CameraSampleBytes",0xE4},{"CameraRightCandidate",0x8},
               {"CameraUpCandidate",0x14},{"CameraForwardCandidate",0x20},
               {"CameraPosition",0x2C},{"CameraViewportCandidate",0x58},
               {"CameraTanHorizontalCandidate",0xD0},{"CameraTanVerticalCandidate",0xE0}};
    return c;
}
}
int main() {
    auto config=Setup(); FakeReader reader{};
    Config configured{};
    Check(configured.Load(L"dayz.ini") && configured.gateMeters[4]>0 &&
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
    const bool saved=loaded && roundtrip.Save();
    Config reread{};
    Check(saved && reread.Load(temporaryConfig.wstring()) &&
          reread.gateMeters[4]==1 && reread.itemCategoryGateMeters[6]==300 &&
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
    Check(reread.EditValue("overlay","item_gate_m","250") &&
          reread.gateMeters[4]==250 && reread.itemCategoryGateMeters[6]==300,
          "raising master range restores category coverage");
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
    std::printf("checks=%d failures=%d\n",checks,failures);
    return failures?1:0;
}
