#include "config.h"
#include "MultiBuildOrders.h"

#if MULTI_BUILD_ENABLE
#include "MultiBuild.h"
#include "oddraw.h"
#include "iddrawsurface.h"
#include "tamem.h"
#include "tafunctions.h"
#include "unitrotate.h"
#include "hook/hook.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace
{
    // These are owner-local UI orders, not a shared simulation rule. Native
    // Units_Update (0x48AEB5..0x48AECD) calls MainOrderStateController only for
    // owners whose My_PlayerType is LocalHuman(1) or LocalAI(2); RemoteHuman
    // skips the main/background order calls at 0x48AF9C/0x48AFA2 entirely.
    // Unit creation, nanolathe and movement remain the ordinary native paths.
    constexpr unsigned kGroundFollow = 0x00406300u;
    constexpr unsigned kAirFollow = 0x0040FBE0u;
    constexpr unsigned kOrderDestroy = 0x0043A1F0u;
    constexpr unsigned kMaxOrders = 8192;
    constexpr unsigned kMaxHelpers = 131072;
    constexpr unsigned kAlive = 0x10000000u;
    constexpr unsigned kDying = 0x4000u;

    using Dispatch = void (__stdcall*)(unsigned, unsigned, UnitStruct*, UnitStruct*,
        Position_Dword*, unsigned, unsigned);
    // The ordinary click function toggles matching queued builds. Helpers use
    // its underlying allocator/queue operation so several guards of one leader
    // cannot toggle one another off merely because they have the same target.
    const auto SendClick = reinterpret_cast<Dispatch>(0x0043AFC0u);
    const auto SendQueued = reinterpret_cast<Dispatch>(0x0043ADC0u);
    using FindMission = void (__thiscall*)(unsigned char*, const char*);
    const auto FindByName = reinterpret_cast<FindMission>(0x00438760u);
    using Delay = void (__thiscall*)(UnitOrdersStruct*, unsigned);
    const auto DelayCallback = reinterpret_cast<Delay>(0x00439E80u);

    struct UnitRef
    {
        UnitStruct* unit = nullptr;
        UnitDefStruct* definition = nullptr;
        PlayerStruct* owner = nullptr;
        void* object = nullptr;
        int movement = 0;
        short type = 0;
        short slot = 0;
    };

    struct BuildTask
    {
        UnitRef leader;
        UnitOrdersStruct* order = nullptr;
        bool finished = false;
    };

    struct HelperTask
    {
        UnitRef helper;
        std::shared_ptr<BuildTask> build;
        unsigned action = 0;
        unsigned start = 0;
    };

    std::recursive_mutex g_mutex;
    std::map<UnitOrdersStruct*, std::shared_ptr<BuildTask>> g_builds;
    std::map<UnitOrdersStruct*, HelperTask> g_helpers;
    std::unique_ptr<InlineSingleHook> g_groundHook, g_airHook, g_destroyHook;
    TAdynmemStruct* g_world = nullptr;
    UnitStruct* g_units = nullptr;
    UnitDefStruct* g_definitions = nullptr;
    int g_localPlayer = -1;
    int g_lastTick = -1;
    bool g_installed = false;

    void Clear()
    {
        g_helpers.clear();
        g_builds.clear();
        g_world = nullptr;
        g_units = nullptr;
        g_definitions = nullptr;
        g_localPlayer = g_lastTick = -1;
    }

    bool Context(TAdynmemStruct* ta)
    {
        if (!g_installed || !ta || !DataShare || DataShare->TAProgress != TAInGame
            || DataShare->PlayingDemo || !ta->UnitDef || !ta->BeginUnitsArray_p
            || !ta->EndOfUnitsArray_p || ta->LocalHumanPlayer_PlayerID < 0
            || ta->LocalHumanPlayer_PlayerID >= 10)
        {
            Clear();
            return false;
        }
        const int me = ta->LocalHumanPlayer_PlayerID;
        if (!ta->Players[me].PlayerActive || ta->Players[me].My_PlayerType != Player_LocalHuman)
        {
            Clear();
            return false;
        }
        if (g_world == ta && g_units == ta->BeginUnitsArray_p && g_definitions == ta->UnitDef
            && g_localPlayer == me && ta->GameTime < g_lastTick)
        {
            // A clock rollback can leave more than one managed guard live.
            // Keep their identities until native completion removes each one,
            // rather than abandoning the other guards after the first tick.
            for (auto& build : g_builds)
            {
                build.second->finished = true;
                build.second->order = nullptr;
            }
            g_builds.clear();
        }
        else if (g_world != ta || g_units != ta->BeginUnitsArray_p || g_definitions != ta->UnitDef
            || g_localPlayer != me)
        {
            Clear();
            g_world = ta;
            g_units = ta->BeginUnitsArray_p;
            g_definitions = ta->UnitDef;
            g_localPlayer = me;
        }
        g_lastTick = ta->GameTime;
        return true;
    }

    bool LiveLocal(const TAdynmemStruct& ta, const UnitStruct* unit)
    {
        if (!unit) return false;
        const auto address = reinterpret_cast<std::uintptr_t>(unit);
        const auto begin = reinterpret_cast<std::uintptr_t>(ta.BeginUnitsArray_p);
        const auto end = reinterpret_cast<std::uintptr_t>(ta.EndOfUnitsArray_p);
        if (end < begin || address < begin || address > end
            || (address - begin) % sizeof(UnitStruct)) return false;
        const PlayerStruct* owner = &ta.Players[ta.LocalHumanPlayer_PlayerID];
        const auto ownerBegin = reinterpret_cast<std::uintptr_t>(owner->Units);
        const auto ownerEnd = reinterpret_cast<std::uintptr_t>(owner->UnitsAry_End);
        return owner->Units && owner->UnitsAry_End && ownerBegin <= address && address <= ownerEnd
            && unit->UnitID > 0 && unit->UnitType && unit->Owner_PlayerPtr0 == owner
            && (unit->UnitSelected & kAlive) && !(unit->UnitSelected & kDying)
            && unit->IsUnit && unit->Nanoframe == 0.0f;
    }

    UnitRef Remember(UnitStruct* unit)
    {
        UnitRef ref;
        ref.unit = unit; ref.definition = unit->UnitType; ref.owner = unit->Owner_PlayerPtr0;
        ref.object = unit->Object3do; ref.movement = unit->IsUnit;
        ref.type = unit->UnitID; ref.slot = unit->UnitInGameIndex;
        return ref;
    }

    bool SameUnit(const TAdynmemStruct& ta, const UnitRef& ref)
    {
        return LiveLocal(ta, ref.unit) && ref.unit->UnitType == ref.definition
            && ref.unit->Owner_PlayerPtr0 == ref.owner && ref.unit->Object3do == ref.object
            && ref.unit->IsUnit == ref.movement && ref.unit->UnitID == ref.type
            && ref.unit->UnitInGameIndex == ref.slot;
    }

    bool Orders(const UnitStruct& unit, std::set<UnitOrdersStruct*>& result)
    {
        result.clear();
        for (auto* order = unit.UnitOrders; order; order = order->NextOrder)
            if (result.size() >= kMaxOrders || !result.insert(order).second) return false;
        return true;
    }

    unsigned Mission(const char* name)
    {
        unsigned char id = 0;
        FindByName(&id, name);
        return id;
    }

    Position_Dword Placement(const TAdynmemStruct& ta, const UnitDefStruct& def)
    {
        // GUI_IssueMobileBuildOrder (0x4196C4..0x419727): snap the same way
        // for odd/even footprints, retaining the native validated ground Z.
        Position_Dword position = {};
        const int x = ta.MouseMapPos.X, y = ta.MouseMapPos.Y;
        position.X = static_cast<unsigned short>(((x - def.FootX * 8 + 8) >> 4) * 16 + def.FootX * 8);
        position.Y = static_cast<unsigned short>(((y - def.FootY * 8 + 8) >> 4) * 16 + def.FootY * 8);
        position.Z = static_cast<unsigned short>(ta.CircleSelect_Pos1TAz);
        return position;
    }

    UnitOrdersStruct* NewOrder(UnitStruct& unit, const std::set<UnitOrdersStruct*>& before,
        unsigned action, UnitStruct* target)
    {
        std::set<UnitOrdersStruct*> after;
        if (!Orders(unit, after)) return nullptr;
        for (auto* order : after)
            if (!before.count(order) && order->Unit_ptr == &unit
                && order->COBHandler_index == action && order->AttackTargat == target) return order;
        return nullptr;
    }

    void Destroyed(UnitOrdersStruct* order)
    {
        // Called BEFORE the native destructor or free. This prevents a new
        // allocation at the same address inheriting another task's identity.
        const auto build = g_builds.find(order);
        if (build != g_builds.end())
        {
            build->second->finished = true;
            build->second->order = nullptr;
            g_builds.erase(build);
        }
        g_helpers.erase(order);
    }

    // -1 = ordinary native guard; 2 = delayed wait; 5 = complete this one native
    // order. The engine owns cancellation, unlinking, cleanup and successors.
    int HelperResult(TAdynmemStruct* ta, UnitStruct* unit, UnitOrdersStruct* order, unsigned flags)
    {
        const auto found = g_helpers.find(order);
        if (found == g_helpers.end()) return -1;
        if (!Context(ta)) return (flags & 2u) ? -1 : 5;
        // Context can clear the maps, so never retain iterators across it.
        const auto current = g_helpers.find(order);
        if (current == g_helpers.end()) return (flags & 2u) ? -1 : 5;
        const HelperTask task = current->second;
        if (flags & 2u) return -1; // The destructor must run vanilla teardown.
        if (unit != task.helper.unit || !SameUnit(*ta, task.helper)
            || order->Unit_ptr != unit || order->COBHandler_index != task.action
            || order->StartTime != task.start || order->AttackTargat != task.build->leader.unit)
        {
            g_helpers.erase(order);
            return 5;
        }
        if (task.build->finished || !task.build->order || !SameUnit(*ta, task.build->leader)) return 5;
        // A queued group may follow earlier independent orders. Do not mirror
        // the leader's earlier job merely because the helper arrived first.
        if (task.build->leader.unit->UnitOrders != task.build->order)
        {
            // Returning zero with no event mask would immediately redispatch
            // the same node inside MainOrderStateController's loop. Use the
            // native timer/event bit and return 2 without advancing its State.
            DelayCallback(order, 1);
            return 2;
        }
        return -1;
    }

    int __stdcall FollowProc(PInlineX86StackBuffer buf)
    {
        const auto* args = reinterpret_cast<const DWORD*>(buf->Esp);
        auto* unit = reinterpret_cast<UnitStruct*>(args[1]);
        auto* order = reinterpret_cast<UnitOrdersStruct*>(args[2]);
        int result;
        {
            std::lock_guard<std::recursive_mutex> lock(g_mutex);
            result = HelperResult(*TAmainStruct_PtrPtr, unit, order, static_cast<unsigned>(args[3]));
        }
        if (result < 0) return 0;
        buf->Eax = static_cast<DWORD>(result);
        buf->rtnAddr_Pvoid = reinterpret_cast<LPVOID>(args[0]);
        buf->Esp += 4 + 12; // Emulate the untouched __stdcall RET 0x0C.
        return X86STRACKBUFFERCHANGE;
    }

    int __stdcall DestroyProc(PInlineX86StackBuffer buf)
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        Destroyed(reinterpret_cast<UnitOrdersStruct*>(buf->Ecx)); // __thiscall ECX
        return 0;
    }

    bool CheckBytes(unsigned address, const unsigned char* expected, size_t length)
    {
        return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;
    }
}

namespace MultiBuildOrders
{
    bool Ready() { return g_installed; }

    void Install()
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (g_installed) return;
        const unsigned char ground[] = {0x83,0xEC,0x18,0x53,0x8B,0x5C,0x24,0x24};
        const unsigned char air[] = {0x83,0xEC,0x54,0x53,0x8B,0x5C,0x24,0x60};
        const unsigned char destroy[] = {0x53,0x56,0x8B,0xF1,0x57};
        if (!CheckBytes(kGroundFollow, ground, sizeof(ground))
            || !CheckBytes(kAirFollow, air, sizeof(air))
            || !CheckBytes(kOrderDestroy, destroy, sizeof(destroy)))
        {
            IDDrawSurface::OutptTxt("[MultiBuild] native order byte guard failed; cooperative orders disabled");
            return;
        }
        g_groundHook.reset(new InlineSingleHook(kGroundFollow, sizeof(ground), INLINE_5BYTESLAGGERJMP, FollowProc));
        g_airHook.reset(new InlineSingleHook(kAirFollow, sizeof(air), INLINE_5BYTESLAGGERJMP, FollowProc));
        g_destroyHook.reset(new InlineSingleHook(kOrderDestroy, sizeof(destroy), INLINE_5BYTESLAGGERJMP, DestroyProc));
        g_installed = true;
        Clear();
        IDDrawSurface::OutptTxt("[MultiBuild] cooperative construction orders installed");
    }

    bool Issue(TAdynmemStruct* ta, const std::vector<UnitStruct*>& constructors, unsigned type, bool shift)
    {
        std::lock_guard<std::recursive_mutex> lock(g_mutex);
        if (!Context(ta) || constructors.size() < 2 || !type || type >= ta->UNITINFOCount) return false;

        std::vector<UnitStruct*> builders = constructors;
        std::sort(builders.begin(), builders.end());
        builders.erase(std::unique(builders.begin(), builders.end()), builders.end());
        UnitStruct* leader = nullptr;
        std::map<UnitStruct*, std::set<UnitOrdersStruct*>> before;
        for (auto* unit : builders)
        {
            if (!LiveLocal(*ta, unit) || !(unit->UnitSelected & UnitSelected_State)
                || !(unit->UnitType->UnitTypeMask_0 & builder) || !Orders(*unit, before[unit])) return true;
            if (!MultiBuild::CanBuild(ta, unit, type)) continue;
            // Keep selection-order-independent leadership stable for queue
            // toggles; prefer ground when it can start the chosen structure.
            if (!leader || ((leader->UnitType->UnitTypeMask_0 & canfly)
                && !(unit->UnitType->UnitTypeMask_0 & canfly))) leader = unit;
        }
        if (!leader || builders.size() < 2 || g_helpers.size() + builders.size() > kMaxHelpers) return true;

        const bool airLeader = (leader->UnitType->UnitTypeMask_0 & canfly) != 0;
        const unsigned buildAction = Mission(airLeader ? "VTOL_MobileBuild" : "MobileBuild");
        const unsigned groundGuard = Mission("Follow_Ground"), airGuard = Mission("VTOL_Follow");
        if (!buildAction || !groundGuard || !airGuard || buildAction == groundGuard || groundGuard == airGuard) return true;
        Position_Dword position = Placement(*ta, ta->UnitDef[type]);
        SendClick(buildAction, shift ? 1u : 0u, leader, nullptr, &position, type, 0);
        auto* build = NewOrder(*leader, before[leader], buildAction, nullptr);
        if (!build) return true; // Native Shift toggle removed an existing task.

        if (CUnitRotate* rotation = CUnitRotate::GetInstance())
        {
            const int facing = rotation->GetRotation();
            rotation->TagOrderRotation(build, rotation->IsRotationAllowed(type, facing) ? facing : 0);
        }
        auto task = std::make_shared<BuildTask>();
        task->leader = Remember(leader); task->order = build;
        g_builds[build] = task;
        for (auto* unit : builders)
        {
            if (unit == leader) continue;
            const unsigned action = (unit->UnitType->UnitTypeMask_0 & canfly) ? airGuard : groundGuard;
            SendQueued(action, shift ? 1u : 0u, unit, leader, &position, 0, 0);
            auto* guard = NewOrder(*unit, before[unit], action, leader);
            if (!guard) continue;
            HelperTask helper;
            helper.helper = Remember(unit); helper.build = task;
            helper.action = action; helper.start = guard->StartTime;
            g_helpers[guard] = helper;
        }
        return true;
    }
}

#else
namespace MultiBuildOrders
{
    void Install() {}
    bool Ready() { return false; }
    bool Issue(TAdynmemStruct*, const std::vector<UnitStruct*>&, unsigned, bool) { return false; }
}
#endif
