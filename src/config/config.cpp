#include "config.h"
#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
std::string Trim(const std::string& input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return input.substr(first, last - first + 1);
}
std::wstring WidenAscii(const std::string& text) { return {text.begin(), text.end()}; }
bool ParseBonePairs(const std::string& text,std::array<Config::BonePair,24>& pairs,
                    std::uint8_t& count) {
    std::array<Config::BonePair,24> pending{};
    std::uint8_t next=0;
    std::istringstream input(text);
    std::string token;
    while (std::getline(input,token,',')) {
        const auto dash=token.find('-');
        if (dash==std::string::npos || next>=pending.size()) return false;
        const auto left=Trim(token.substr(0,dash));
        const auto right=Trim(token.substr(dash+1));
        if (left.empty() || right.empty()) return false;
        std::size_t endLeft=0,endRight=0;
        const auto from=std::stoul(left,&endLeft,10);
        const auto to=std::stoul(right,&endRight,10);
        if (endLeft!=left.size() || endRight!=right.size() || from>=16 || to>=16 || from==to)
            return false;
        pending[next++]={static_cast<std::uint8_t>(from),static_cast<std::uint8_t>(to)};
    }
    if (!next) return false;
    pairs=pending;count=next;return true;
}
}

bool Config::Load(const std::wstring& filename) {
    path = filename;
    std::ifstream input(filename);
    if (!input) return false;
    std::string section, line;
    std::array<bool,8> explicitItemNameRange{};
    bool explicitCopyBinding=false;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size()-2); continue; }
        const auto sep = line.find('=');
        if (sep == std::string::npos) return false;
        const auto key = Trim(line.substr(0, sep));
        const auto value = Trim(line.substr(sep+1));
        try {
            if (section == "offsets") offsets[key] = std::stoull(value, nullptr, 0);
            else if (section == "aim_muzzle_fallback_mps") {
                const float speed=std::stof(value);
                if (key.empty() || key.size()>64 || aimMuzzleFallbacks.size()>=64 ||
                    !std::isfinite(speed) || speed<1.0f || speed>2500.0f) return false;
                aimMuzzleFallbacks[key]=speed;
            }
            else if (section == "target") {
                if (key == "process") process = WidenAscii(value);
                else if (key == "module") module = WidenAscii(value);
                else if (key == "version") version = WidenAscii(value);
                else if (key == "image") image = WidenAscii(value);
            } else if (section == "probe") {
                const auto n = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
                if (key == "max_reads") maxReads = n;
                else if (key == "max_table_entries") maxTableEntries = n;
                else if (key == "max_table_count") maxTableCount = n;
                else if (key == "max_map_capacity") maxMapCapacity = n;
                else if (key == "max_infected_scan") maxInfectedScan = n;
                else if (key == "max_item_entries_per_bucket") maxItemEntriesPerBucket = n;
                else if (key == "max_scoreboard_entries") maxScoreboardEntries = n;
                else if (key == "player_region_bytes") playerRegionBytes = n;
            } else if (section == "skeleton") {
                if (key=="player_pairs" && !ParseBonePairs(value,skeletonPairs[0],skeletonPairCounts[0]))
                    return false;
                if (key=="infected_pairs" && !ParseBonePairs(value,skeletonPairs[1],skeletonPairCounts[1]))
                    return false;
            } else if (section == "overlay") {
                const auto n = static_cast<int>(std::stoul(value, nullptr, 0));
                if (key == "static_rectangle") staticRectangle = n != 0;
                else if (key == "infected_marker") infectedMarker = n != 0;
                else if (key == "marker_scan_ms") markerScanMs = static_cast<std::uint32_t>(n);
                else if (key == "marker_max_distance") markerMaxDistance = static_cast<std::uint32_t>(n);
                else if (key == "marker_max_entities") markerMaxEntities = static_cast<std::uint32_t>(n);
                else if (key == "marker_height_cm") markerHeightCm = static_cast<std::uint32_t>(n);
                else if (key == "panel_key") panelKey = n;
                else if (key == "end_key") endKey = n;
                else if (key == "item_toggle_key") itemToggleKey = n;
                else if (key == "coordinate_copy_key") {
                    coordinateCopyKey = n;explicitCopyBinding=true;
                }
                else if (key == "entity_scan_ms") entityScanMs = static_cast<std::uint32_t>(n);
                else if (key == "entity_scan_limit") entityScanLimit = static_cast<std::uint32_t>(n);
                else if (key == "player_gate_m") gateMeters[0] = static_cast<std::uint32_t>(n);
                else if (key == "infected_gate_m") gateMeters[1] = static_cast<std::uint32_t>(n);
                else if (key == "animal_gate_m") gateMeters[2] = static_cast<std::uint32_t>(n);
                else if (key == "vehicle_gate_m") gateMeters[3] = static_cast<std::uint32_t>(n);
                else if (key == "item_gate_m") gateMeters[4] = static_cast<std::uint32_t>(n);
                else if (key == "map_item_gate_m") mapItemGateMeters = static_cast<std::uint32_t>(n);
                else if (key == "skeleton_gate_m") skeletonGateMeters = static_cast<std::uint32_t>(n);
                else if (key == "pip_lens_radius_percent") pipLensRadiusPercent = static_cast<std::uint32_t>(n);
                else if (key == "pip_horizontal_scale_percent") pipHorizontalScalePercent = static_cast<std::uint32_t>(n);
                else if (key == "show_players") showEntities[0] = n!=0;
                else if (key == "show_infected") showEntities[1] = n!=0;
                else if (key == "show_animals") showEntities[2] = n!=0;
                else if (key == "show_vehicles") showEntities[3] = n!=0;
                else if (key == "show_local_player") showLocalPlayer = n!=0;
                else if (key == "show_player_names") showPlayerNames = n!=0;
                else if (key == "show_infected_names") showInfectedNames = n!=0;
                else if (key == "show_boxes") showBoxes = n!=0;
                else if (key == "hide_dead_players") hideDeadPlayers = n!=0;
                else if (key == "show_held_item") showHeldItem = n!=0;
                else if (key == "show_items") showItems = n!=0;
                else if (key == "compact_hud") compactHud = n!=0;
                else if (key == "camera_hud") cameraHud = n!=0;
                else if (key == "funnel_hud") funnelHud = n!=0;
                else if (key == "debug_read_hud") debugReadHud = n!=0;
                else if (key == "show_skeleton") showSkeleton = n!=0;
                else if (key == "raw_bone_points") rawBonePoints = n!=0;
                else if (key == "aim_assist_enabled") aimAssistEnabled = n!=0;
                else if (key == "aim_target_infected") aimTargetInfected = n!=0;
                else if (key == "aim_debug" || key == "aim_show_fov") aimShowFov = n!=0;
                else if (key == "aim_solo_calibration") aimSoloCalibration = n!=0;
                else if (key == "aim_hold_key") aimHoldKey = n;
                else if (key == "aim_fov_px") aimFovPixels = static_cast<std::uint32_t>(n);
                else if (key == "aim_gate_m") aimGateMeters = static_cast<std::uint32_t>(n);
                else if (key == "aim_prebind_gate_m") aimPrebindGateMeters = static_cast<std::uint32_t>(n);
                else if (key == "aim_bone") aimBone = static_cast<std::uint32_t>(n);
                else if (key == "aim_hysteresis_percent") aimHysteresisPercent = static_cast<std::uint32_t>(n);
                else if (key == "aim_snap_strength") aimSnapStrength = static_cast<std::uint32_t>(n);
                else if (key == "aim_rate_px_s") aimRatePixelsPerSecond = static_cast<std::uint32_t>(n);
                else if (key == "aim_accel_px_s2") aimAccelerationPixelsPerSecond2 = static_cast<std::uint32_t>(n);
                else if (key == "aim_prediction_enabled") aimPredictionEnabled = n!=0;
                else if (key == "aim_prediction_gate_m") aimPredictionGateMeters = static_cast<std::uint32_t>(n);
                else if (key == "aim_velocity_window_ms") aimVelocityWindowMs = static_cast<std::uint32_t>(n);
                else if (key == "aim_velocity_max_mps") aimVelocityMaxMetersPerSecond = static_cast<std::uint32_t>(n);
                else if (key == "watch_health_candidates") watchHealthCandidates = n!=0;
                else if (key == "watch_death_candidates") watchDeathCandidates = n!=0;
                else if (key == "color_player") entityColors[0] = static_cast<std::uint32_t>(n);
                else if (key == "color_infected") entityColors[1] = static_cast<std::uint32_t>(n);
                else if (key == "color_animal") entityColors[2] = static_cast<std::uint32_t>(n);
                else if (key == "color_vehicle") entityColors[3] = static_cast<std::uint32_t>(n);
                else if (key == "color_quality_pristine") itemQualityColors[0] = static_cast<std::uint32_t>(n);
                else if (key == "color_quality_worn") itemQualityColors[1] = static_cast<std::uint32_t>(n);
                else if (key == "color_quality_damaged") itemQualityColors[2] = static_cast<std::uint32_t>(n);
                else if (key == "color_quality_badly_damaged") itemQualityColors[3] = static_cast<std::uint32_t>(n);
                else if (key == "color_quality_ruined") itemQualityColors[4] = static_cast<std::uint32_t>(n);
                else if (key == "item_near_m") {itemNearMeters = static_cast<std::uint32_t>(n);itemCategoryNearMeters.fill(itemNearMeters);}
                else if (key == "item_mid_m") {itemMidMeters = static_cast<std::uint32_t>(n);itemCategoryMidMeters.fill(itemMidMeters);}
                else if (key == "item_far_name") {itemFarName = n!=0;itemCategoryFarName.fill(itemFarName);}
                else if (key == "item_far_alpha") itemFarAlpha = static_cast<std::uint32_t>(n);
                else if (key == "item_near_distance") {itemNearDistance = n!=0;itemCategoryNearDistance.fill(itemNearDistance);}
                else if (key == "item_near_quality") {itemNearQuality = n!=0;itemCategoryNearQuality.fill(itemNearQuality);}
                else if (key == "item_grid") itemGrid = n!=0;
                else if (key == "item_cell_px") itemCellPixels = static_cast<std::uint32_t>(n);
                else if (key == "item_cluster_count") itemClusterCount = n!=0;
                else if (key == "item_compact") itemCompact = n!=0;
                else if (key == "item_use_icons") itemUseIcons = n!=0;
                else if (key == "item_anchored_lists") itemAnchoredLists = n!=0;
                else if (key == "item_focus_only") itemFocusOnly = n!=0;
                else if (key == "item_focus_radius_px") itemFocusRadiusPixels = static_cast<std::uint32_t>(n);
                else if (key == "item_group_radius_cm") itemGroupRadiusCm = static_cast<std::uint32_t>(n);
                else if (key == "item_icon_opacity") itemIconOpacity = static_cast<std::uint32_t>(n);
                else if (key == "item_name_m") itemNameMeters=static_cast<std::uint32_t>(n);
                else if (key == "item_split_m") itemSplitMeters=static_cast<std::uint32_t>(n);
                else if (key == "item_weapons") showItemCategory[0] = n!=0;
                else if (key == "item_ammo") showItemCategory[1] = n!=0;
                else if (key == "item_medical") showItemCategory[2] = n!=0;
                else if (key == "item_food") showItemCategory[3] = n!=0;
                else if (key == "item_tools") showItemCategory[4] = n!=0;
                else if (key == "item_clothing") showItemCategory[5] = n!=0;
                else if (key == "item_other") showItemCategory[6] = n!=0;
                else if (key == "item_drinks") showItemCategory[7] = n!=0;
                else if (key == "item_weapons_gate_m") itemCategoryGateMeters[0] = static_cast<std::uint32_t>(n);
                else if (key == "item_ammo_gate_m") itemCategoryGateMeters[1] = static_cast<std::uint32_t>(n);
                else if (key == "item_medical_gate_m") itemCategoryGateMeters[2] = static_cast<std::uint32_t>(n);
                else if (key == "item_food_gate_m") itemCategoryGateMeters[3] = static_cast<std::uint32_t>(n);
                else if (key == "item_tools_gate_m") itemCategoryGateMeters[4] = static_cast<std::uint32_t>(n);
                else if (key == "item_clothing_gate_m") itemCategoryGateMeters[5] = static_cast<std::uint32_t>(n);
                else if (key == "item_other_gate_m") itemCategoryGateMeters[6] = static_cast<std::uint32_t>(n);
                else if (key == "item_drinks_gate_m") itemCategoryGateMeters[7] = static_cast<std::uint32_t>(n);
                else if (key == "item_medical_priority") itemCategoryPriority[2] = n;
                else {
                    constexpr const char* categories[]={"weapons","ammo","medical","food","tools","clothing","other","drinks"};
                    for (std::size_t i=0;i<showItemCategory.size();++i) {
                        const std::string prefix=std::string("item_")+categories[i]+"_";
                        if (key==prefix+"near_m") itemCategoryNearMeters[i]=static_cast<std::uint32_t>(n);
                        else if (key==prefix+"mid_m") itemCategoryMidMeters[i]=static_cast<std::uint32_t>(n);
                        else if (key==prefix+"far_name") itemCategoryFarName[i]=n!=0;
                        else if (key==prefix+"near_distance") itemCategoryNearDistance[i]=n!=0;
                        else if (key==prefix+"near_quality") itemCategoryNearQuality[i]=n!=0;
                        else if (key==prefix+"priority") itemCategoryPriority[i]=n;
                        else if (key==prefix+"name_m") {
                            itemCategoryNameMeters[i]=static_cast<std::uint32_t>(n);
                            explicitItemNameRange[i]=true;
                        }
                        else if (key==prefix+"color") itemCategoryColors[i]=static_cast<std::uint32_t>(n);
                    }
                }
            }
        } catch (...) { return false; }
    }
    for (std::size_t i=0;i<itemCategoryNameMeters.size();++i)
        if (!explicitItemNameRange[i])
            itemCategoryNameMeters[i]=std::min(itemNameMeters,itemCategoryGateMeters[i]);
    if (!explicitCopyBinding && (coordinateCopyKey==panelKey ||
        coordinateCopyKey==endKey || coordinateCopyKey==itemToggleKey)) {
        for (int candidate=0x72;candidate<=0x7B;++candidate)
            if (candidate!=panelKey && candidate!=endKey && candidate!=itemToggleKey) {
                coordinateCopyKey=candidate;break;
            }
    }
    return !process.empty() && !module.empty() && !version.empty() && !image.empty() &&
           maxReads > 0 && maxTableEntries > 0 && maxTableCount > 0 && maxMapCapacity > 0 &&
           maxItemEntriesPerBucket > 0 && maxScoreboardEntries > 0 &&
           playerRegionBytes > 0 && playerRegionBytes <= 8192 && playerRegionBytes % 512 == 0 &&
           panelKey > 0 && panelKey <= 255 && endKey > 0 && endKey <= 255 &&
           itemToggleKey > 0 && itemToggleKey <= 255 &&
           coordinateCopyKey > 0 && coordinateCopyKey <= 255 &&
           itemToggleKey != panelKey && itemToggleKey != endKey &&
           coordinateCopyKey != panelKey && coordinateCopyKey != endKey &&
           coordinateCopyKey != itemToggleKey &&
           markerScanMs > 0 && markerMaxDistance > 0 &&
           markerMaxEntities > 0 && markerMaxEntities <= 512 && markerHeightCm <= 300 &&
           entityScanMs >= 100 && entityScanLimit > 0 && entityScanLimit <= 4096 &&
           skeletonGateMeters > 0 && skeletonPairCounts[0] && skeletonPairCounts[1] &&
           aimHoldKey>0 && aimHoldKey<=255 && aimHoldKey!=panelKey &&
           aimHoldKey!=endKey && aimHoldKey!=itemToggleKey &&
           aimHoldKey!=coordinateCopyKey && aimFovPixels>=10 &&
           aimFovPixels<=1000 && aimGateMeters>0 && aimGateMeters<=1200 &&
           aimPrebindGateMeters>0 && aimPrebindGateMeters<=1200 && aimBone<=2 &&
           aimHysteresisPercent<=400 && aimSnapStrength>=1 &&
           aimSnapStrength<=20 && aimRatePixelsPerSecond>=10 &&
           aimRatePixelsPerSecond<=5000 &&
           aimAccelerationPixelsPerSecond2>=10 &&
           aimAccelerationPixelsPerSecond2<=20000 &&
           aimPredictionGateMeters>0 && aimPredictionGateMeters<=1200 &&
           aimVelocityWindowMs>=100 && aimVelocityWindowMs<=300 &&
           aimVelocityMaxMetersPerSecond>=5 && aimVelocityMaxMetersPerSecond<=50 &&
           pipLensRadiusPercent >= 20 && pipLensRadiusPercent <= 60 &&
           pipHorizontalScalePercent >= 70 && pipHorizontalScalePercent <= 110 &&
           itemNearMeters <= itemMidMeters &&
           itemFarAlpha <= 255 &&
           itemCellPixels >= 16 && itemCellPixels <= 256 &&
           itemNameMeters <= 3000 && itemSplitMeters <= 3000 &&
           itemFocusRadiusPixels >= 32 &&
           itemFocusRadiusPixels <= 1000 && itemGroupRadiusCm >= 25 &&
           itemGroupRadiusCm <= 500 && itemIconOpacity <= 255 &&
           std::all_of(gateMeters.begin(),gateMeters.end(),[](auto n){return n>0 && n<=10000;}) &&
           mapItemGateMeters <= 3000 &&
           std::all_of(itemCategoryGateMeters.begin(),itemCategoryGateMeters.end(),[](auto n){return n>0 && n<=10000;}) &&
           [&](){for (std::size_t i=0;i<itemCategoryGateMeters.size();++i)
               if (itemCategoryNearMeters[i]>itemCategoryMidMeters[i] ||
                   itemCategoryMidMeters[i]>itemCategoryGateMeters[i] ||
                   itemCategoryNameMeters[i]>itemCategoryGateMeters[i] ||
                   itemCategoryPriority[i]<0 || itemCategoryPriority[i]>10) return false;
               return true;}();
}

std::uint64_t Config::Offset(const char* key) const {
    const auto it = offsets.find(key);
    return it == offsets.end() ? 0 : it->second;
}

namespace {
bool RewriteIni(const std::wstring& path,
                const std::unordered_map<std::string,std::string>& changes) {
    std::ifstream input(path);
    if (!input) return false;
    const std::wstring temporary=path+L".pending";
    std::ofstream output(temporary,std::ios::trunc);
    if (!output) return false;
    std::string section,line;
    std::unordered_set<std::string> written;
    auto appendMissing=[&](const std::string& currentSection) {
        if (currentSection.empty()) return;
        const std::string prefix=currentSection+"/";
        std::vector<std::pair<std::string,std::string>> missing;
        for (const auto& change:changes)
            if (change.first.compare(0,prefix.size(),prefix)==0 &&
                written.find(change.first)==written.end())
                missing.emplace_back(change.first.substr(prefix.size()),change.second);
        std::sort(missing.begin(),missing.end());
        for (const auto& value:missing) output<<value.first<<'='<<value.second<<'\n';
    };
    while (std::getline(input,line)) {
        const auto trimmed=Trim(line);
        if (trimmed.size()>2 && trimmed.front()=='[' && trimmed.back()==']') {
            appendMissing(section);
            section=trimmed.substr(1,trimmed.size()-2);
        } else {
            const auto equal=trimmed.find('=');
            if (equal!=std::string::npos) {
                const auto key=Trim(trimmed.substr(0,equal));
                const auto found=changes.find(section+"/"+key);
                if (found!=changes.end()) {
                    line=key+"="+found->second;
                    written.insert(found->first);
                }
            }
        }
        output<<line<<'\n';
        if (!output) return false;
    }
    appendMissing(section);
    output.close();input.close();
    Config checked{};
    if (!checked.Load(temporary)) { DeleteFileW(temporary.c_str());return false; }
    if (!MoveFileExW(temporary.c_str(),path.c_str(),
                     MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());return false;
    }
    return true;
}
}

bool Config::Save() const {
    if (path.empty()) return false;
    std::unordered_map<std::string,std::string> values;
    auto put=[&values](const std::string& key,std::uint32_t value) {
        values["overlay/"+key]=std::to_string(value);
    };
    auto boolean=[&put](const std::string& key,bool value) {put(key,value?1u:0u);};
    auto color=[&values](const std::string& key,std::uint32_t value) {
        std::ostringstream stream;stream<<"0x"<<std::hex<<std::uppercase<<value;
        values["overlay/"+key]=stream.str();
    };
    boolean("static_rectangle",staticRectangle);boolean("infected_marker",infectedMarker);
    put("marker_scan_ms",markerScanMs);put("marker_max_distance",markerMaxDistance);
    put("marker_max_entities",markerMaxEntities);put("marker_height_cm",markerHeightCm);
    put("entity_scan_ms",entityScanMs);put("entity_scan_limit",entityScanLimit);
    put("player_gate_m",gateMeters[0]);put("infected_gate_m",gateMeters[1]);
    put("animal_gate_m",gateMeters[2]);put("vehicle_gate_m",gateMeters[3]);
    put("item_gate_m",gateMeters[4]);put("map_item_gate_m",mapItemGateMeters);
    put("skeleton_gate_m",skeletonGateMeters);
    put("pip_lens_radius_percent",pipLensRadiusPercent);
    put("pip_horizontal_scale_percent",pipHorizontalScalePercent);
    boolean("show_players",showEntities[0]);boolean("show_infected",showEntities[1]);
    boolean("show_animals",showEntities[2]);boolean("show_vehicles",showEntities[3]);
    boolean("show_local_player",showLocalPlayer);boolean("show_player_names",showPlayerNames);
    boolean("show_infected_names",showInfectedNames);boolean("show_boxes",showBoxes);
    boolean("hide_dead_players",hideDeadPlayers);
    boolean("show_held_item",showHeldItem);boolean("show_items",showItems);
    put("item_toggle_key",static_cast<std::uint32_t>(itemToggleKey));
    put("coordinate_copy_key",static_cast<std::uint32_t>(coordinateCopyKey));
    boolean("compact_hud",compactHud);boolean("camera_hud",cameraHud);
    boolean("funnel_hud",funnelHud);boolean("show_skeleton",showSkeleton);
    boolean("debug_read_hud",debugReadHud);
    boolean("raw_bone_points",rawBonePoints);
    boolean("aim_assist_enabled",aimAssistEnabled);
    boolean("aim_target_infected",aimTargetInfected);
    boolean("aim_show_fov",aimShowFov);
    boolean("aim_solo_calibration",aimSoloCalibration);
    put("aim_hold_key",static_cast<std::uint32_t>(aimHoldKey));
    put("aim_fov_px",aimFovPixels);put("aim_gate_m",aimGateMeters);
    put("aim_prebind_gate_m",aimPrebindGateMeters);
    put("aim_bone",aimBone);put("aim_hysteresis_percent",aimHysteresisPercent);
    put("aim_snap_strength",aimSnapStrength);
    put("aim_rate_px_s",aimRatePixelsPerSecond);
    put("aim_accel_px_s2",aimAccelerationPixelsPerSecond2);
    boolean("aim_prediction_enabled",aimPredictionEnabled);
    put("aim_prediction_gate_m",aimPredictionGateMeters);
    put("aim_velocity_window_ms",aimVelocityWindowMs);
    put("aim_velocity_max_mps",aimVelocityMaxMetersPerSecond);
    boolean("watch_health_candidates",watchHealthCandidates);
    boolean("watch_death_candidates",watchDeathCandidates);
    const char* entityNames[]={"player","infected","animal","vehicle"};
    for (std::size_t i=0;i<4;++i) color(std::string("color_")+entityNames[i],entityColors[i]);
    const char* qualityNames[]={"pristine","worn","damaged","badly_damaged","ruined"};
    for (std::size_t i=0;i<5;++i)
        color(std::string("color_quality_")+qualityNames[i],itemQualityColors[i]);
    put("item_near_m",itemNearMeters);put("item_mid_m",itemMidMeters);
    boolean("item_far_name",itemFarName);put("item_far_alpha",itemFarAlpha);
    boolean("item_near_distance",itemNearDistance);boolean("item_near_quality",itemNearQuality);
    boolean("item_grid",itemGrid);put("item_cell_px",itemCellPixels);
    boolean("item_cluster_count",itemClusterCount);
    boolean("item_compact",itemCompact);
    boolean("item_use_icons",itemUseIcons);boolean("item_anchored_lists",itemAnchoredLists);
    boolean("item_focus_only",itemFocusOnly);
    put("item_focus_radius_px",itemFocusRadiusPixels);
    put("item_group_radius_cm",itemGroupRadiusCm);
    put("item_icon_opacity",itemIconOpacity);put("item_name_m",itemNameMeters);
    put("item_split_m",itemSplitMeters);
    const char* categories[]={"weapons","ammo","medical","food","tools","clothing","other","drinks"};
    for (std::size_t i=0;i<8;++i) {
        const std::string prefix=std::string("item_")+categories[i];
        boolean(prefix,showItemCategory[i]);put(prefix+"_gate_m",itemCategoryGateMeters[i]);
        put(prefix+"_near_m",itemCategoryNearMeters[i]);
        put(prefix+"_mid_m",itemCategoryMidMeters[i]);
        boolean(prefix+"_far_name",itemCategoryFarName[i]);
        boolean(prefix+"_near_distance",itemCategoryNearDistance[i]);
        boolean(prefix+"_near_quality",itemCategoryNearQuality[i]);
        put(prefix+"_name_m",itemCategoryNameMeters[i]);
        put(prefix+"_priority",static_cast<std::uint32_t>(itemCategoryPriority[i]));
        color(prefix+"_color",itemCategoryColors[i]);
    }
    put("panel_key",static_cast<std::uint32_t>(panelKey));
    put("end_key",static_cast<std::uint32_t>(endKey));
    for (const auto& entry:aimMuzzleFallbacks)
        values["aim_muzzle_fallback_mps/"+entry.first]=std::to_string(entry.second);
    return RewriteIni(path,values);
}

bool Config::EditValue(const std::string& section,const std::string& key,
                       const std::string& value) {
    if (path.empty() || section.empty() || key.empty() ||
        section.find_first_of("\r\n[]=")!=std::string::npos ||
        key.find_first_of("\r\n[]=")!=std::string::npos ||
        value.find_first_of("\r\n")!=std::string::npos) return false;
    const std::wstring currentPath=path;
    if (!RewriteIni(path,{{section+"/"+key,value}})) return false;
    Config replacement{};
    if (!replacement.Load(currentPath)) return false;
    *this=std::move(replacement);
    return true;
}

float Config::AimFallbackMps(const std::string& weaponClass) const {
    const auto found=aimMuzzleFallbacks.find(weaponClass);
    return found==aimMuzzleFallbacks.end()?0.0f:found->second;
}
