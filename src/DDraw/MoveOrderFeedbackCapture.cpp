#include "config.h"
#include "MoveOrderFeedback.h"
#if PREPEND_ORDERS_ENABLE
#include "PrependOrders.h"
#endif

#if DRAWN_MOVE_FORMATIONS_ENABLE
#include "oddraw.h"
#include "tamem.h"
#include "tafunctions.h"
#include "iddrawsurface.h"
#include "hook/hook.h"
#include <cstdint>
#include <cstring>
#include <memory>

namespace
{
    using SendFn = void (__stdcall*)(unsigned, int, UnitStruct*, UnitStruct*, const Position_Dword*, int, int);
    const auto NativeSend = reinterpret_cast<SendFn>(0x0043AFC0u);
    const auto NativeAction = reinterpret_cast<unsigned char* (__stdcall*)(unsigned*, unsigned,
        UnitStruct*, UnitStruct*, const Position_Dword*)>(0x0043F0E0u);

    const unsigned kCall = 0x0048D1F4u;
    const unsigned char kOriginal[5] = {0xE8,0xC7,0xDD,0xFA,0xFF};
    unsigned char callPatch[5] = {};
    std::unique_ptr<SingleHook> callHook;
    bool installed = false;
    const unsigned kMaxOrders = 8192;

    struct Observation
    {
        TAdynmemStruct* world;
        UnitStruct* first;
        UnitStruct* last;
        UnitStruct* ownerFirst;
        UnitStruct* ownerLast;
        PlayerStruct* owner;
        UnitDefStruct* definition;
        int player;
        int tick;
        unsigned dpid;
        short type;
        unsigned unitIndex;
        unsigned action;
        unsigned before;
        bool prepend;
        Position_Dword origin;
        Position_Dword destination;
    };

    bool Playable(TAdynmemStruct* ta)
    {
        if (!ta || !DataShare || DataShare->TAProgress != TAInGame
            || DataShare->PlayingDemo || DataShare->ehaOff) return false;
        const int player = ta->LocalHumanPlayer_PlayerID;
        return player >= 0 && player < 10 && ta->Players[player].PlayerInfo
            && !(ta->Players[player].PlayerInfo->PropertyMask & WATCH)
            && ta->Players[player].My_PlayerType == Player_LocalHuman;
    }

    bool InBank(UnitStruct* unit, UnitStruct* first, UnitStruct* last)
    {
        const std::uintptr_t p = reinterpret_cast<std::uintptr_t>(unit);
        const std::uintptr_t a = reinterpret_cast<std::uintptr_t>(first);
        const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(last);
        return first && last && a <= b && p >= a && p <= b && (p-a)%sizeof(UnitStruct) == 0;
    }

    bool LiveMobile(TAdynmemStruct* ta, UnitStruct* unit)
    {
        if (!Playable(ta) || !unit) return false;
        PlayerStruct* owner = &ta->Players[ta->LocalHumanPlayer_PlayerID];
        return InBank(unit, owner->Units, owner->UnitsAry_End)
            && InBank(unit, ta->BeginUnitsArray_p, ta->EndOfUnitsArray_p)
            && unit->UnitID && unit->UnitType && unit->Owner_PlayerPtr0 == owner
            && (unit->UnitSelected & 0x10000000u) && !(unit->UnitSelected & 0x4000u)
            && unit->Nanoframe == 0.0f && (unit->UnitType->UnitTypeMask_0 & canmove)
            && unit->UnitType->bmcode != 0;
    }

    bool Nearby(const Position_Dword& a, const Position_Dword& b)
    {
        // Native Shift cancellation compares complete 16.16 X/Y coordinates.
        // Use unsigned arithmetic to match its wrap-safe +/-16 world-unit test.
        std::uint32_t ax, ay, bx, by;
        std::memcpy(&ax, &a, sizeof(ax));
        std::memcpy(&ay, reinterpret_cast<const unsigned char*>(&a)+8, sizeof(ay));
        std::memcpy(&bx, &b, sizeof(bx));
        std::memcpy(&by, reinterpret_cast<const unsigned char*>(&b)+8, sizeof(by));
        return ax-bx+0x100000u <= 0x200000u && ay-by+0x100000u <= 0x200000u;
    }

    bool CountMatches(UnitStruct& unit, unsigned action, UnitStruct* target,
        const Position_Dword& destination, unsigned& count)
    {
        count = 0;
        UnitOrdersStruct* checkpoint = unit.UnitOrders;
        unsigned power = 1, distance = 0, visited = 0;
        for (UnitOrdersStruct* order = unit.UnitOrders; order; )
        {
            if (++visited > kMaxOrders || order->Unit_ptr != &unit) return false;
            if (order->COBHandler_index == action && (!target || order->AttackTargat == target)
                && Nearby(order->Pos, destination)) ++count;
            order = order->NextOrder;
            ++distance;
            if (order && order == checkpoint) return false;
            if (distance == power) { checkpoint = order; power *= 2; distance = 0; }
        }
        return true;
    }

    bool Capture(unsigned action, UnitStruct* unit, UnitStruct* target,
        const Position_Dword* position, Observation& out)
    {
        // Observing presentation must never prevent native order delivery.
        __try
        {
            TAdynmemStruct* ta = *TAmainStruct_PtrPtr;
            if (!position || !LiveMobile(ta,unit) || !COBSciptHandler_Begin || !COBSciptHandler_End) return false;
            const COBHandle* begin = *COBSciptHandler_Begin;
            const COBHandle* end = *COBSciptHandler_End;
            action &= 0xffu; // Native GUI callers initialize only the low byte.
            if (!action || !begin || !end || end <= begin
                || action >= static_cast<unsigned>(end-begin)) return false;
            unsigned move = 0;
            Position_Dword probe = *position;
            const unsigned char* index = NativeAction(&move, ordertype::MOVE, unit, target, &probe);
            if (!index || *index != action) return false;
            out.world = ta;
            out.first = ta->BeginUnitsArray_p;
            out.last = ta->EndOfUnitsArray_p;
            out.player = ta->LocalHumanPlayer_PlayerID;
            out.owner = unit->Owner_PlayerPtr0;
            out.ownerFirst = out.owner->Units;
            out.ownerLast = out.owner->UnitsAry_End;
            out.dpid = out.owner->DirectPlayID;
            out.tick = ta->GameTime;
            out.definition = unit->UnitType;
            out.type = unit->UnitID;
            out.unitIndex = static_cast<unsigned>(unit-ta->BeginUnitsArray_p);
            out.action = action;
            out.origin = Position_Dword{};
            out.origin.x_ = unit->XPos__;
            out.origin.X = unit->XPos;
            out.origin.z_ = unit->ZPos__;
            out.origin.Z = unit->ZPos;
            out.origin.y_ = unit->YPos__;
            out.origin.Y = unit->YPos;
            out.destination = *position;
#if PREPEND_ORDERS_ENABLE
            out.prepend = PrependOrders::Active();
#else
            out.prepend = false;
#endif
            return CountMatches(*unit,action,target,out.destination,out.before);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool After(const Observation& observed, UnitStruct* unit, UnitStruct* target, unsigned& count)
    {
        __try
        {
            TAdynmemStruct* ta = *TAmainStruct_PtrPtr;
            return ta == observed.world && Playable(ta)
                && ta->LocalHumanPlayer_PlayerID == observed.player
                && ta->BeginUnitsArray_p == observed.first && ta->EndOfUnitsArray_p == observed.last
                && ta->GameTime >= observed.tick
                && LiveMobile(ta,unit) && unit->Owner_PlayerPtr0 == observed.owner
                && observed.owner->Units == observed.ownerFirst && observed.owner->UnitsAry_End == observed.ownerLast
                && observed.owner->DirectPlayID == observed.dpid
                && unit->UnitID == observed.type && unit->UnitType == observed.definition
                && CountMatches(*unit,observed.action,target,observed.destination,count);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void Present(const Observation& observed, bool cancel)
    {
        __try
        {
            if (cancel) MoveOrderFeedback::Cancel(observed.world,observed.unitIndex,observed.destination);
            else MoveOrderFeedback::Record(observed.world,observed.unitIndex,observed.origin,observed.destination);
        }
        // Let C++ exceptions unwind to the outer catch, but contain presentation
        // access violations independently of the native command that just ran.
        __except (GetExceptionCode() == 0xE06D7363u ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool CodeMatches(const unsigned char* bytes)
    {
        __try { return std::memcmp(reinterpret_cast<const void*>(kCall),bytes,5) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}

namespace MoveOrderFeedback
{
    void __stdcall Dispatch(unsigned action, int shift, UnitStruct* unit, UnitStruct* target,
        const Position_Dword* position, int param1, int param2)
    {
        Observation observed = {};
        const bool captured = Capture(action,unit,target,position,observed);
#if PREPEND_ORDERS_ENABLE
        PrependOrders::Dispatch(action,shift,unit,target,position,param1,param2);
#else
        NativeSend(action,shift,unit,target,position,param1,param2);
#endif
        unsigned after = 0;
        if (!captured || !After(observed,unit,target,after)) return;
        try
        {
            if (observed.prepend || shift)
            {
                if (after > observed.before) Present(observed,false);
                else if (after < observed.before) Present(observed,true);
            }
            else if (after) Present(observed,false);
        }
        catch (...) {} // Rendering allocation failure must not affect a sent order.
    }

    void Install()
    {
        if (installed) return;
        callPatch[0] = 0xE8;
        const std::int32_t relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&Dispatch)-kCall-5);
        std::memcpy(callPatch+1,&relative,sizeof(relative));
        // Prepending installs this same wrapper when enabled. Otherwise (also
        // QueuePrepend=FALSE), this module owns the stock CALL itself. No chain
        // and no second hook object ever overwrite an existing owner.
        if (!CodeMatches(callPatch))
        {
            if (!CodeMatches(kOriginal)) return;
            try
            {
                std::unique_ptr<SingleHook> candidate(new SingleHook(kCall,5,INLINE_UNPROTECTEVINMENT,callPatch));
                if (!CodeMatches(callPatch)) return;
                callHook.swap(candidate);
            }
            catch (...) { return; }
        }
        installed = true;
        IDDrawSurface::OutptTxt("[MoveOrderFeedback] build=destinations-v1");
    }
}
#else
namespace MoveOrderFeedback
{
    void Install() {}
    void __stdcall Dispatch(unsigned action, int shift, UnitStruct* unit, UnitStruct* target,
        const _Position_Dword* position, int param1, int param2)
    {
#if PREPEND_ORDERS_ENABLE
        PrependOrders::Dispatch(action,shift,unit,target,position,param1,param2);
#else
        using Send = void (__stdcall*)(unsigned, int, UnitStruct*, UnitStruct*, const _Position_Dword*, int, int);
        reinterpret_cast<Send>(0x0043AFC0u)(action,shift,unit,target,position,param1,param2);
#endif
    }
}
#endif
