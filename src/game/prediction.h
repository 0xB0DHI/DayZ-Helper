#pragma once
#include "game.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace game {
struct HeldWeapon {
    std::uint64_t entity=0,inventory=0,weapon=0,type=0;
    std::string weaponClass;
};
template<class Reader> bool ResolveHeldWeapon(Reader& reader,const Config& config,
                                               std::uint64_t entity,HeldWeapon& result) {
    HeldWeapon pending{};pending.entity=entity;
    if (!PtrAt(reader,entity,config.Offset("EntityInventory"),pending.inventory) ||
        !PtrAt(reader,pending.inventory,config.Offset("InventoryHandsCandidate"),pending.weapon) ||
        !PtrAt(reader,pending.weapon,config.Offset("EntityType"),pending.type) ||
        !ReadTypeText(reader,config,pending.type,"TypeClassCandidate",pending.weaponClass) ||
        pending.weaponClass.empty()) return false;
    result=std::move(pending);return true;
}
template<class Reader> bool ValidateHeldWeapon(Reader& reader,const Config& config,
                                                const HeldWeapon& held) {
    std::uint64_t inventory=0,weapon=0,type=0;
    return held.entity && held.weapon &&
        PtrAt(reader,held.entity,config.Offset("EntityInventory"),inventory) &&
        inventory==held.inventory &&
        PtrAt(reader,inventory,config.Offset("InventoryHandsCandidate"),weapon) &&
        weapon==held.weapon &&
        PtrAt(reader,weapon,config.Offset("EntityType"),type) && type==held.type;
}
struct MuzzleBinding {
    std::uint64_t entity=0,inventory=0,weapon=0,type=0,array=0;
    std::uint64_t ammoType=0,auxiliary=0;
    std::uint32_t index=0,count=0;
    std::string weaponClass;
};
struct MuzzleSpeed {
    float base=0,multiplier=0,effective=0;
};

// Resolve only at the cache refresh cadence. No values are published on a
// partial read; a later tick rechecks all mutable links before using them.
template<class Reader> bool ResolveMuzzleBinding(Reader& reader,const Config& config,
                                                  const HeldWeapon& held,MuzzleBinding& result) {
    MuzzleBinding pending{};pending.entity=held.entity;pending.inventory=held.inventory;
    pending.weapon=held.weapon;pending.type=held.type;pending.weaponClass=held.weaponClass;
    if (!held.weapon || !held.type) return false;
    const auto current=config.Offset("WeaponCurrentMuzzle");
    const auto arrayOffset=config.Offset("WeaponMuzzleArray");
    const auto countOffset=config.Offset("WeaponMuzzleCount");
    const auto stride=config.Offset("WeaponMuzzleEntryStride");
    if (!current || arrayOffset<current || countOffset<arrayOffset ||
        countOffset-current+sizeof(std::uint32_t)>32 || stride!=0x100) return false;
    std::array<std::uint8_t,32> header{};
    std::uint64_t address=0;
    if (!AddAddress(pending.weapon,current,address) ||
        !reader.ReadRaw(address,header.data(),
            static_cast<std::size_t>(countOffset-current+sizeof(std::uint32_t)))) return false;
    std::memcpy(&pending.index,header.data(),sizeof(pending.index));
    std::memcpy(&pending.array,header.data()+arrayOffset-current,sizeof(pending.array));
    std::memcpy(&pending.count,header.data()+countOffset-current,sizeof(pending.count));
    if (!pending.count || pending.count>16 || pending.index>=pending.count ||
        !UserRange(pending.array,static_cast<std::size_t>(pending.count)*stride)) return false;
    std::uint64_t entry=0;
    if (!AddAddress(pending.array,static_cast<std::uint64_t>(pending.index)*stride,entry) ||
        !PtrAt(reader,entry,config.Offset("ChamberAmmoType"),pending.ammoType) ||
        !PtrAt(reader,entry,config.Offset("MuzzleAuxiliary"),pending.auxiliary)) return false;
    std::uint64_t inventoryAgain=0,weaponAgain=0,typeAgain=0;
    if (!PtrAt(reader,held.entity,config.Offset("EntityInventory"),inventoryAgain) ||
        inventoryAgain!=pending.inventory ||
        !PtrAt(reader,pending.inventory,config.Offset("InventoryHandsCandidate"),weaponAgain) ||
        weaponAgain!=pending.weapon ||
        !PtrAt(reader,pending.weapon,config.Offset("EntityType"),typeAgain) ||
        typeAgain!=pending.type) return false;
    result=std::move(pending);return true;
}

template<class Reader> bool SampleMuzzleSpeed(Reader& reader,const Config& config,
                                               const MuzzleBinding& binding,
                                               MuzzleSpeed& result) {
    if (!binding.entity || !binding.weapon || !binding.ammoType || !binding.auxiliary ||
        config.Offset("WeaponMuzzleEntryStride")!=0x100) return false;
    std::uint64_t baseAt=0,multiplierAt=0;
    MuzzleSpeed pending{};
    if (!AddAddress(binding.ammoType,config.Offset("AmmoTypeInitSpeed"),baseAt) ||
        !AddAddress(binding.auxiliary,config.Offset("MuzzleAuxiliarySpeedMultiplier"),multiplierAt) ||
        !reader.Read(baseAt,pending.base) ||
        !reader.Read(multiplierAt,pending.multiplier)) return false;
    pending.effective=pending.base*pending.multiplier;
    if (!std::isfinite(pending.base) || !std::isfinite(pending.multiplier) ||
        !std::isfinite(pending.effective) || pending.base<1.0f ||
        pending.multiplier<0.1f || pending.multiplier>10.0f ||
        pending.effective<1.0f || pending.effective>2500.0f) return false;
    std::uint64_t inventory=0,weapon=0,type=0,array=0,entry=0,ammo=0,auxiliary=0;
    std::uint32_t index=0,count=0;
    std::uint64_t address=0;
    if (!PtrAt(reader,binding.entity,config.Offset("EntityInventory"),inventory) ||
        inventory!=binding.inventory ||
        !PtrAt(reader,inventory,config.Offset("InventoryHandsCandidate"),weapon) ||
        weapon!=binding.weapon ||
        !PtrAt(reader,weapon,config.Offset("EntityType"),type) || type!=binding.type ||
        !AddAddress(weapon,config.Offset("WeaponCurrentMuzzle"),address) ||
        !reader.Read(address,index) || index!=binding.index ||
        !PtrAt(reader,weapon,config.Offset("WeaponMuzzleArray"),array) ||
        array!=binding.array ||
        !AddAddress(weapon,config.Offset("WeaponMuzzleCount"),address) ||
        !reader.Read(address,count) || count!=binding.count || index>=count ||
        !AddAddress(array,static_cast<std::uint64_t>(index)*0x100,entry) ||
        !PtrAt(reader,entry,config.Offset("ChamberAmmoType"),ammo) ||
        ammo!=binding.ammoType ||
        !PtrAt(reader,entry,config.Offset("MuzzleAuxiliary"),auxiliary) ||
        auxiliary!=binding.auxiliary) return false;
    result=pending;return true;
}
}
