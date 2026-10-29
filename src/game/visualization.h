#pragma once
#include "game.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

namespace visual {
enum class EntityKind : std::uint8_t { Player, Infected, Animal, Vehicle, Item, Other, Count };
enum class ItemKind : std::uint8_t { Weapons, Ammo, Medical, Food, Tools, Clothing, Other, Drinks, Count };
enum class ItemBand : std::uint8_t { Near, Mid, Far, Hidden };
constexpr std::size_t EntityKinds=static_cast<std::size_t>(EntityKind::Count);
constexpr std::size_t ItemKinds=static_cast<std::size_t>(ItemKind::Count);
inline std::size_t Index(EntityKind kind) { return static_cast<std::size_t>(kind); }
inline std::size_t Index(ItemKind kind) { return static_cast<std::size_t>(kind); }

inline const char* CardinalDirection(game::Vec3 forward) {
    if (!std::isfinite(forward.x) || !std::isfinite(forward.z) ||
        forward.x*forward.x+forward.z*forward.z<0.0025f) return "--";
    // Live compass sample: +Z is north and +X is east for this DayZ build.
    constexpr float step=0.78539816339f;
    constexpr float halfStep=step*0.5f;
    constexpr const char* names[]={"N","NE","E","SE","S","SW","W","NW"};
    const float angle=std::atan2(forward.x,forward.z);
    const int sector=static_cast<int>(std::floor((angle+halfStep)/step));
    return names[(sector%8+8)%8];
}

inline bool FormatIzurviveCoordinates(char* out,std::size_t capacity,game::Vec3 position) {
    if (!out || capacity==0) return false;
    out[0]='\0';
    if (!std::isfinite(position.x) || !std::isfinite(position.z)) return false;
    // iZurvive's map X/Y correspond to DayZ world X/Z; world Y is altitude.
    const int written=std::snprintf(out,capacity,"%.0f/%.0f",
        static_cast<double>(position.x),static_cast<double>(position.z));
    if (written<0 || static_cast<std::size_t>(written)>=capacity) {
        out[0]='\0';return false;
    }
    return true;
}

inline bool Contains(std::string_view text,std::string_view part) {
    return text.find(part)!=std::string_view::npos;
}
inline EntityKind ClassifyEntity(std::string_view name) {
    if (name=="dayzplayer") return EntityKind::Player;
    if (name=="dayzinfected") return EntityKind::Infected;
    if (name=="dayzanimal") return EntityKind::Animal;
    if (name=="car" || name=="boat" || name=="helicopter" || name=="truck")
        return EntityKind::Vehicle;
    return EntityKind::Other;
}
inline ItemKind ClassifyItem(std::string_view type,std::string_view className,
                            std::string_view model) {
    if (type=="clothing" || Contains(model,"\\characters\\")) return ItemKind::Clothing;
    if (Contains(model,"\\ammunition\\") || Contains(className,"Ammo_") ||
        Contains(className,"Mag_")) return ItemKind::Ammo;
    if (Contains(model,"\\medical\\") || Contains(className,"Bandage") ||
        Contains(className,"Saline") || Contains(className,"Morphine")) return ItemKind::Medical;
    if (Contains(model,"\\drinks\\") || Contains(className,"SodaCan")) return ItemKind::Drinks;
    if (Contains(model,"\\food\\") || Contains(className,"FoodCan")) return ItemKind::Food;
    if (type=="Weapon" || Contains(model,"\\weapons\\")) return ItemKind::Weapons;
    if (Contains(model,"\\tools\\") || Contains(model,"\\navigation\\") ||
        Contains(model,"\\crafting\\")) return ItemKind::Tools;
    return ItemKind::Other;
}
inline bool WithinGate(float distance,float gate) {
    return std::isfinite(distance) && distance>=0 && std::isfinite(gate) &&
           gate>0 && distance<=gate;
}
inline bool RevealItemName(float distance,std::uint32_t nameMeters,bool useIcons) {
    return !useIcons || (std::isfinite(distance) && distance>=0 &&
                         distance<=static_cast<float>(nameMeters));
}
inline bool AttachItemLabel(float distance,std::uint32_t splitMeters,bool nameVisible) {
    return nameVisible && std::isfinite(distance) && distance>=0 &&
           distance<=static_cast<float>(splitMeters);
}
inline bool SameItemPile(game::Vec3 a,game::Vec3 b,std::uint32_t radiusCm) {
    const float radius=static_cast<float>(radiusCm)*0.01f;
    const float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
    return std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) &&
           dx*dx+dy*dy+dz*dz<=radius*radius;
}
inline void SortMembership(std::vector<std::uint64_t>& entries) {
    std::sort(entries.begin(),entries.end());
}
inline bool HasMember(const std::vector<std::uint64_t>& sortedEntries,std::uint64_t entity) {
    return std::binary_search(sortedEntries.begin(),sortedEntries.end(),entity);
}
inline bool SkeletonEnabled(EntityKind kind,const std::array<bool,4>& categories,
                            bool showSkeleton) {
    return showSkeleton &&
           (kind==EntityKind::Player || kind==EntityKind::Infected) &&
           categories[Index(kind)];
}
inline bool AimTargetEnabled(EntityKind kind,const std::array<bool,4>& categories,
                             bool targetInfected) {
    return (kind==EntityKind::Player && categories[0]) ||
           (kind==EntityKind::Infected && categories[1] && targetInfected);
}
inline std::uint32_t AdaptiveReadCap(std::size_t cachedObjects,std::uint32_t hardCap) {
    const auto estimate=1024ull+static_cast<unsigned long long>(cachedObjects)*8ull;
    return static_cast<std::uint32_t>(std::min<unsigned long long>(
        hardCap,std::max<unsigned long long>(1200,estimate)));
}
inline std::uint32_t ScanSampleCap(std::uint32_t frameCap) {
    // Leave enough reads for BeginRefresh and at least one StepRefresh pass.
    const auto reserve=std::min<std::uint32_t>(512,
        std::max<std::uint32_t>(96,frameCap/4));
    return frameCap>reserve ? frameCap-reserve : 1;
}
inline ItemBand Band(float distance,float nearMeters,float midMeters,float gateMeters,
                     bool farName) {
    if (!WithinGate(distance,gateMeters)) return ItemBand::Hidden;
    if (distance<=nearMeters) return ItemBand::Near;
    if (distance<=midMeters) return ItemBand::Mid;
    return farName?ItemBand::Far:ItemBand::Hidden;
}
inline const char* Quality(std::uint32_t packed) {
    constexpr const char* names[]={"Pristine","Worn","Damaged","Badly damaged","Ruined"};
    const auto level=packed&0xffu;
    return level<5?names[level]:"";
}
inline void EntityLabel(char* out,std::size_t capacity,const char* name,const char* held,
                        float distance) {
    if (!capacity) return;
    if (name && name[0] && held && held[0])
        std::snprintf(out,capacity,"%s | %s | %.0f m",name,held,double(distance));
    else if (name && name[0]) std::snprintf(out,capacity,"%s | %.0f m",name,double(distance));
    else std::snprintf(out,capacity,"%.0f m",double(distance));
}
inline void ItemLabel(char* out,std::size_t capacity,const char* name,float distance,
                      std::uint32_t packed,ItemBand band,bool compact,
                      bool showDistance,bool showQuality) {
    if (!capacity) return;
    const char* label=name&&name[0]?name:"Item";
    if (compact || band!=ItemBand::Near) std::snprintf(out,capacity,"%s",label);
    else if (showDistance && showQuality && Quality(packed)[0])
        std::snprintf(out,capacity,"%s | %.0f m | %s",label,double(distance),Quality(packed));
    else if (showDistance) std::snprintf(out,capacity,"%s | %.0f m",label,double(distance));
    else if (showQuality && Quality(packed)[0])
        std::snprintf(out,capacity,"%s | %s",label,Quality(packed));
    else std::snprintf(out,capacity,"%s",label);
}
struct Box { float left=0,top=0,right=0,bottom=0; };
inline bool ProjectBox(const game::CameraSample& camera,game::Vec3 position,float heightMeters,
                       float widthRatio,float viewWidth,float viewHeight,Box& box) {
    game::Vec2 bottom{},top{};
    game::Vec3 upper=position;upper.y+=heightMeters;
    if (!game::ProjectWorld(camera,position,viewWidth,viewHeight,bottom) ||
        !game::ProjectWorld(camera,upper,viewWidth,viewHeight,top)) return false;
    const float h=std::max(8.0f,std::abs(bottom.y-top.y));
    const float w=std::max(5.0f,h*widthRatio);
    const float center=(bottom.x+top.x)*0.5f;
    box={center-w*0.5f,top.y,center+w*0.5f,top.y+h};
    return std::isfinite(box.left) && std::isfinite(box.bottom);
}
inline std::uint32_t Cell(float x,float y,std::uint32_t cellSize,
                          std::uint32_t columns,std::uint32_t rows) {
    if (!cellSize || !columns || !rows || x<0 || y<0) return UINT32_MAX;
    const auto col=static_cast<std::uint32_t>(x)/cellSize;
    const auto row=static_cast<std::uint32_t>(y)/cellSize;
    return col<columns && row<rows ? row*columns+col : UINT32_MAX;
}
inline bool NewItemWins(float distance,int priority,float oldDistance,int oldPriority) {
    if (priority!=oldPriority) return priority>oldPriority;
    return distance<oldDistance;
}
inline bool SuppressNearbyItem(float dx,float dy,std::uint32_t spacing,
                               bool wasVisible) {
    const float radius=static_cast<float>(spacing)*(wasVisible?0.94f:1.12f);
    return dx*dx+dy*dy<radius*radius;
}
}
