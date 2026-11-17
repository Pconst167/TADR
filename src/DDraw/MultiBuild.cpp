#include "config.h"
#include "MultiBuild.h"

#if MULTI_BUILD_ENABLE
#include "oddraw.h"
#include "tamem.h"
#include "tafunctions.h"
#include "iddrawsurface.h"
#include "MultiBuildAssets.h"
#include "MultiBuildOrders.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace
{
    constexpr unsigned kEnabled = 0x00800000u;
    bool g_ready = false;
    std::recursive_mutex g_mutex;
    const TAdynmemStruct* g_world = nullptr;
    const UnitDefStruct* g_definitions = nullptr;
    unsigned g_count = 0;
    int g_tick = -1;
    std::map<std::string, unsigned> g_names;
    std::map<unsigned, std::vector<MultiBuildOption>> g_menus;
    const TAdynmemStruct* g_choiceWorld = nullptr;
    unsigned g_choiceType = 0;

    void Disarm(TAdynmemStruct* ta)
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (ta && DataShare && DataShare->TAProgress == TAInGame && ta == g_choiceWorld
            && ta->PrepareOrder_Type == ordertype::BUILD
            && static_cast<unsigned short>(ta->BuildUnitID) == g_choiceType)
            ta->PrepareOrder_Type = ordertype::STOP;
        g_choiceWorld = nullptr; g_choiceType = 0;
    }

    std::string Name(const char* value, std::size_t length)
    {
        std::string result;
        for (std::size_t i = 0; i < length && value[i]; ++i)
        {
            char c = value[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            result += c;
        }
        return result;
    }
    template<std::size_t N>
    void Text(char (&destination)[N], const char* source, std::size_t length)
    {
        const std::size_t size = (std::min)(N - 1, length);
        std::memcpy(destination, source, size);
        destination[size] = 0;
    }
    void Context(const TAdynmemStruct& ta)
    {
        if (g_world != &ta || g_definitions != ta.UnitDef || g_count != ta.UNITINFOCount
            || ta.GameTime < g_tick)
        {
            g_world = &ta; g_definitions = ta.UnitDef; g_count = ta.UNITINFOCount;
            g_choiceWorld = nullptr; g_choiceType = 0;
            g_names.clear(); g_menus.clear();
            for (unsigned type = 1; type < ta.UNITINFOCount && type <= 65535; ++type)
            {
                const auto& definition = ta.UnitDef[type];
                g_names.emplace(Name(definition.UnitName, sizeof(definition.UnitName)), type);
            }
        }
        g_tick = ta.GameTime;
    }
    const std::vector<MultiBuildOption>& Menu(const TAdynmemStruct& ta, const UnitStruct& constructor)
    {
        const unsigned type = static_cast<unsigned short>(constructor.UnitID);
        const auto found = g_menus.find(type);
        if (found != g_menus.end()) return found->second;
        std::vector<MultiBuildOption> result;
        std::set<unsigned> seen;
        for (const auto& button : MultiBuildAssets::Menu(ta, *constructor.UnitType))
        {
            const auto unit = g_names.find(Name(button.name.data(), button.name.size()));
            if (unit == g_names.end() || !seen.insert(unit->second).second) continue;
            const auto& definition = ta.UnitDef[unit->second];
            MultiBuildOption option;
            option.type = unit->second; option.quickkey = button.quickkey;
            Text(option.name, definition.UnitName, sizeof(definition.UnitName));
            Text(option.label, definition.Name, sizeof(definition.Name));
            Text(option.description, definition.UnitDescription, sizeof(definition.UnitDescription));
            result.push_back(option);
        }
        return g_menus.emplace(type, std::move(result)).first->second;
    }
    bool Buildable(const TAdynmemStruct& ta, unsigned type)
    {
        return type > 0 && type < ta.UNITINFOCount && type <= 65535
            && (ta.UnitDef[type].UnitTypeMask_0 & kEnabled)
            && ta.UnitDef[type].FootX > 0 && ta.UnitDef[type].FootY > 0;
    }
}

namespace MultiBuild
{
    void Install()
    {
        MultiBuildOrders::Install();
        g_ready = MultiBuildOrders::Ready();
        if (g_ready) IDDrawSurface::OutptTxt("[MultiBuild] build=combined-v1");
    }
    bool Selection(TAdynmemStruct* ta, std::vector<UnitStruct*>& constructors)
    {
        constructors.clear();
        if (!g_ready || !ta || !DataShare || DataShare->TAProgress != TAInGame || DataShare->PlayingDemo
            || !ta->UnitDef || !ta->UNITINFOCount || !ta->BeginUnitsArray_p || !ta->EndOfUnitsArray_p)
            return false;
        const int me = ta->LocalHumanPlayer_PlayerID;
        if (me < 0 || me >= 10) return false;
        const auto& owner = ta->Players[me];
        if (!owner.PlayerActive || !owner.PlayerInfo || (owner.PlayerInfo->PropertyMask & WATCH)
            || owner.My_PlayerType != Player_LocalHuman || !owner.Units || !owner.UnitsAry_End) return false;
        const auto begin = reinterpret_cast<std::uintptr_t>(ta->BeginUnitsArray_p);
        const auto end = reinterpret_cast<std::uintptr_t>(ta->EndOfUnitsArray_p);
        const auto first = reinterpret_cast<std::uintptr_t>(owner.Units);
        const auto last = reinterpret_cast<std::uintptr_t>(owner.UnitsAry_End);
        if (end < begin || first < begin || last > end || last < first
            || (first - begin) % sizeof(UnitStruct) || (last - begin) % sizeof(UnitStruct)) return false;
        for (auto* unit = owner.Units; unit <= owner.UnitsAry_End; ++unit)
        {
            if (!(unit->UnitSelected & UnitSelected_State) || !unit->UnitID
                || !(unit->UnitSelected & 0x10000000u) || (unit->UnitSelected & 0x4000u)) continue;
            const unsigned type = static_cast<unsigned short>(unit->UnitID);
            if (type >= ta->UNITINFOCount || !unit->UnitType || unit->UnitType != &ta->UnitDef[type]
                || unit->Owner_PlayerPtr0 != &owner || unit->Nanoframe != 0.0f || !unit->IsUnit
                || !unit->UnitType->bmcode || !(unit->UnitType->UnitTypeMask_0 & builder))
            { constructors.clear(); return false; }
            constructors.push_back(unit);
        }
        if (constructors.size() < 2) { constructors.clear(); return false; }
        return true;
    }
    bool CanBuild(TAdynmemStruct* ta, const UnitStruct* constructor, unsigned type)
    {
        if (!ta || !ta->UnitDef || !constructor || !constructor->UnitType || !Buildable(*ta, type)) return false;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        Context(*ta);
        const auto& menu = Menu(*ta, *constructor);
        return std::any_of(menu.begin(), menu.end(), [type](const MultiBuildOption& option) { return option.type == type; });
    }
    std::vector<MultiBuildOption> Options(TAdynmemStruct* ta)
    {
        std::vector<UnitStruct*> constructors;
        if (!Selection(ta, constructors)) { Disarm(ta); return {}; }
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        Context(*ta);
        std::set<unsigned> types, seen;
        std::vector<MultiBuildOption> result;
        for (auto* constructor : constructors)
        {
            if (!types.insert(static_cast<unsigned short>(constructor->UnitID)).second) continue;
            for (const auto& option : Menu(*ta, *constructor))
                if (Buildable(*ta, option.type) && seen.insert(option.type).second) result.push_back(option);
        }
        return result;
    }
    bool Choose(TAdynmemStruct* ta, unsigned type)
    {
        std::vector<UnitStruct*> constructors;
        if (!Selection(ta, constructors)) return false;
        bool allowed = false;
        for (auto* constructor : constructors) allowed = allowed || CanBuild(ta, constructor, type);
        if (!allowed) return false;
        ta->BuildUnitID = static_cast<short>(type);
        ta->PrepareOrder_Type = ordertype::BUILD;
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        g_choiceWorld = ta; g_choiceType = type;
        return true;
    }
    bool TryIssue(TAdynmemStruct* ta, const _MOUSEEVENT* mouse)
    {
        std::vector<UnitStruct*> constructors;
        if (!mouse || !ta || ta->PrepareOrder_Type != ordertype::BUILD) return false;
        if (!Selection(ta, constructors))
        {
            // A group-only icon may no longer be valid after adding a combat
            // unit or selecting a different constructor. Cancel that pending
            // placement before the generic native dispatcher can use it.
            bool owned = false;
            {
                std::lock_guard<std::recursive_mutex> lock(g_mutex);
                owned = ta == g_choiceWorld
                    && static_cast<unsigned short>(ta->BuildUnitID) == g_choiceType;
            }
            Disarm(ta);
            return owned;
        }
        const unsigned type = static_cast<unsigned short>(ta->BuildUnitID);
        // Never let a refused union choice fall through to the engine's
        // unfiltered "BUILD on every selected unit" implementation.
        MultiBuildOrders::Issue(ta, constructors, type, (mouse->fwKeys & MK_SHIFT) != 0);
        return true;
    }
}
#else
namespace MultiBuild
{
    void Install() {}
    bool Selection(TAdynmemStruct*, std::vector<UnitStruct*>& constructors) { constructors.clear(); return false; }
    bool CanBuild(TAdynmemStruct*, const UnitStruct*, unsigned) { return false; }
    std::vector<MultiBuildOption> Options(TAdynmemStruct*) { return {}; }
    bool Choose(TAdynmemStruct*, unsigned) { return false; }
    bool TryIssue(TAdynmemStruct*, const _MOUSEEVENT*) { return false; }
}
#endif
