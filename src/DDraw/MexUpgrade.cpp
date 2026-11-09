#include "config.h"
#include "oddraw.h"
#include "iddrawsurface.h"
#include "tamem.h"
#include "tafunctions.h"
#include "TAConfig.h"
#include "Gaf.h"
#include "MexUpgrade.h"

#include <cstdlib>
#include <cstring>

namespace
{
    constexpr unsigned kDefinitionEnabled = 0x00800000;
    constexpr unsigned kMaxOrders = 8192;
    constexpr int kMohoFootprint = 5;
    thread_local bool checkingSite = false;

    struct SiteProbe
    {
        // TestGridSpot uses two shared scratch values. An upgrade probe runs
        // in the input handler and should leave the native build preview alone.
        DWORD* const scratch = reinterpret_cast<DWORD*>(0x0051E684u);
        const DWORD height = scratch[0];
        const DWORD flags = scratch[1];
        const bool previous = checkingSite;

        SiteProbe() { checkingSite = true; }
        ~SiteProbe()
        {
            scratch[0] = height;
            scratch[1] = flags;
            checkingSite = previous;
        }
    };

    struct UpgradeCursor
    {
        unsigned char pixels[13 * 14];
        GAFFrame frame = {};
        GAFSequence sequence = {};

        UpgradeCursor()
        {
            static constexpr char art[] =
                "......#......"
                ".....#+#....."
                "....#+++#...."
                "...#+++++#..."
                "..#+++++++#.."
                ".#+++++++++#."
                "##+++++++++##"
                "..###+++###.."
                "....#+++#...."
                "....#+++#...."
                "....#+++#...."
                "....#+++#...."
                "....#+++#...."
                "....#####....";
            static_assert(sizeof(art) == sizeof(pixels) + 1, "Upgrade cursor dimensions");
            for (size_t i = 0; i < sizeof(pixels); ++i)
                pixels[i] = art[i] == '+' ? 232 : art[i] == '#' ? 142 : 9;
            // The tip is the click hotspot. Index 9 is TA cursor transparency.
            frame = {13, 14, 6, 0, 9, 0, 0, 0, pixels, 0};
            sequence.Frames = 1;
            sequence.Signature = 1;
            sequence.PtrFrameAry[0] = {&frame, 10};
        }
    };

    struct Upgrade
    {
        UnitStruct* constructor;
        UnitStruct* target;
        int type;
        Position_Dword buildPos;
    };

    struct UpgradePair
    {
        const char* from;
        const char* to;
    };
    constexpr UpgradePair kUpgrades[] = {
        {"ARMMEX", "ARMMOHO"}, {"CORMEX", "CORMOHO"},
        {"ARMUWMEX", "ARMUWMOHO"}, {"CORUWMEX", "CORUWMOHO"},
    };

    bool LiveOwned(const UnitStruct& unit, const PlayerStruct& owner)
    {
        // IsUnit is the movement object pointer. Buildings can be live without it.
        return unit.UnitID && unit.UnitType && unit.Owner_PlayerPtr0 == &owner
            && (unit.UnitSelected & 0x10000000) && !(unit.UnitSelected & 0x4000);
    }

    UnitStruct* SingleSelectedUnit(PlayerStruct& owner)
    {
        UnitStruct* selected = nullptr;
        for (UnitStruct* unit = owner.Units; unit <= owner.UnitsAry_End; ++unit)
        {
            if (!(unit->UnitSelected & UnitSelected_State) || !LiveOwned(*unit, owner)) continue;
            if (selected) return nullptr;
            selected = unit;
        }
        return selected;
    }

    int FindReplacement(const TAdynmemStruct& ta, const UnitDefStruct& oldDef,
        const UnitDefStruct& constructor)
    {
        const char* name = nullptr;
        for (const auto& pair : kUpgrades)
            if (_strnicmp(oldDef.UnitName, pair.from, sizeof(oldDef.UnitName)) == 0)
                name = pair.to;
        if (!name || !constructor.CANBUILD_ptr || constructor.canbuildCount <= 0
            || static_cast<unsigned>(constructor.canbuildCount) > ta.UNITINFOCount) return 0;

        for (int i = 0; i < constructor.canbuildCount; ++i)
        {
            const unsigned id = static_cast<unsigned short>(constructor.CANBUILD_ptr[i]);
            if (!id || id >= ta.UNITINFOCount) continue;
            const UnitDefStruct& def = ta.UnitDef[id];
            if (_strnicmp(def.UnitName, name, sizeof(def.UnitName)) == 0
                && def.bmcode == 0 && def.extractsmetal > oldDef.extractsmetal
                && (def.UnitTypeMask_0 & kDefinitionEnabled)) return id;
        }
        return 0;
    }

    bool UnderBuildLimit(const PlayerStruct& owner, int type, int limit)
    {
        if (limit == -1) return true;
        if (limit <= 0) return false;
        // Native unit creation counts all occupied slots of this type, including nanoframes.
        for (const UnitStruct* unit = owner.Units; unit <= owner.UnitsAry_End; ++unit)
            if (unit->UnitID == type && --limit == 0) return false;
        return true;
    }

    bool CheckReplacementSite(const TAdynmemStruct& ta, PlayerStruct& owner,
        const UnitStruct& old, unsigned slot, const UnitDefStruct& def, Position_Dword& position)
    {
        if (!def.YardMap || def.FootX != kMohoFootprint || def.FootY != kMohoFootprint)
            return false;
        // Same grid rounding as GUI_IssueMobileBuildOrder, starting from the old center.
        const int x = static_cast<int>(old.XPos) + 8 - def.FootX * 8;
        const int y = static_cast<int>(old.YPos) + 8 - def.FootY * 8;
        if (x < 0 || y < 0) return false;
        const int left = x / 16, top = y / 16;
        if (left + def.FootX > ta.FeatureMapSizeX || top + def.FootY > ta.FeatureMapSizeY)
            return false;

        // TestGridSpot can ignore one occupant by slot. Its earlier yard check also
        // sees the old building's yard reservation, so mask that bit on a private
        // copy only where the clicked building occupies the proposed footprint.
        char yard[kMohoFootprint * kMohoFootprint];
        std::memcpy(yard, def.YardMap, sizeof(yard));
        for (int row = 0; row < def.FootY; ++row)
        {
            for (int column = 0; column < def.FootX; ++column)
            {
                const int tileX = left + column, tileY = top + row;
                const auto occupant = ta.FeatureMap[tileY * ta.FeatureMapSizeX + tileX].occupyingUnitNumber;
                // The native preview can omit occupancies in fog. Never reclaim
                // the extractor if a different ground occupant already blocks it.
                if (occupant && occupant != slot) return false;
                if (occupant == slot && tileX >= old.XGridPos && tileX < old.XGridPos + old.SizeFootX
                    && tileY >= old.YGridPos && tileY < old.YGridPos + old.SizeFootZ
                    && (yard[row * def.FootX + column] & 0x01))
                    yard[row * def.FootX + column] &= ~0x01;
            }
        }
        UnitDefStruct preview = def;
        preview.YardMap = yard;
        const unsigned packed = (static_cast<unsigned>(top) << 16) | static_cast<unsigned>(left);
        const SiteProbe probe;
        if (!TestGridSpot(&preview, packed, slot, &owner)) return false;

        // TestGridSpot publishes the validated terrain height used by native build orders.
        const auto gridHeight = reinterpret_cast<int (__cdecl*)()>(0x0047C780u);
        position.X = static_cast<unsigned short>(left * 16 + def.FootX * 8);
        position.Y = static_cast<unsigned short>(top * 16 + def.FootY * 8);
        position.Z = static_cast<unsigned short>(gridHeight());
        return true;
    }

    bool CanReclaim(UnitStruct* constructor, UnitStruct* target)
    {
        const auto canReclaim = reinterpret_cast<int (__thiscall*)(UnitStruct*, UnitStruct*)>(0x00489960u);
        return canReclaim(constructor, target) != 0;
    }

    bool FindUpgrade(TAdynmemStruct* ta, Upgrade& upgrade, bool reportBlocked)
    {
        if (!ta || !DataShare || DataShare->TAProgress != TAInGame || DataShare->PlayingDemo
            || ta->PrepareOrder_Type != ordertype::STOP || (ta->BuildSpotState & 10) != 2) return false;
        if (ta->InterfaceType != 0 && (ta->InterfaceType != 1 || !(ta->BuildSpotState & 4))) return false;

        const int player = ta->LocalHumanPlayer_PlayerID;
        if (player < 0 || player >= 10 || !ta->UnitDef || !ta->FeatureMap
            || !ta->BeginUnitsArray_p || !ta->EndOfUnitsArray_p) return false;
        auto& owner = ta->Players[player];
        if (!owner.PlayerInfo || (owner.PlayerInfo->PropertyMask & WATCH) || owner.My_PlayerType != Player_LocalHuman
            || !owner.Units || !owner.UnitsAry_End || owner.Units < ta->BeginUnitsArray_p
            || owner.UnitsAry_End >= ta->EndOfUnitsArray_p || owner.UnitsAry_End < owner.Units) return false;

        const unsigned slot = ta->MouseOverUnit;
        if (!slot || slot >= static_cast<size_t>(ta->EndOfUnitsArray_p - ta->BeginUnitsArray_p)) return false;
        auto& target = ta->BeginUnitsArray_p[slot];
        if (!LiveOwned(target, owner) || static_cast<unsigned short>(target.UnitInGameIndex) != slot
            || target.Nanoframe != 0.0f || target.UnitType->bmcode != 0 || target.UnitType->extractsmetal <= 0)
            return false;
        auto* constructor = SingleSelectedUnit(owner);
        if (!constructor || constructor == &target || !constructor->IsUnit || constructor->Nanoframe != 0.0f
            || !(constructor->UnitType->UnitTypeMask_0 & builder) || constructor->UnitType->bmcode == 0)
            return false;

        const int type = FindReplacement(*ta, *target.UnitType, *constructor->UnitType);
        if (!type || !CanReclaim(constructor, &target)) return false;
        const auto& def = ta->UnitDef[type];
        if (!UnderBuildLimit(owner, type, def.buildLimit)) return false;
        Position_Dword buildPos = {};
        if (!CheckReplacementSite(*ta, owner, target, slot, def, buildPos))
        {
            if (reportBlocked)
                IDDrawSurface::OutptFmtTxt("[MexUpgrade] replacement site blocked: extractor=%u build=%d", slot, type);
            return false;
        }
        upgrade = {constructor, &target, type, buildPos};
        return true;
    }

    unsigned char ActionIndex(const char* name)
    {
        const auto lookup = reinterpret_cast<unsigned char* (__thiscall*)(unsigned char*, const char*)>(0x00438760u);
        unsigned char index = 0;
        lookup(&index, name);
        return index;
    }

    // ORDERS_NewMainOrder2Unit, also used by native GUI_IssueMobileBuildOrder.
    using Dispatch = void (__stdcall*)(unsigned, int, UnitStruct*, UnitStruct*, const Position_Dword*, int, int);
    const auto Send = reinterpret_cast<Dispatch>(0x0043AFC0u);

    UnitOrdersStruct* FindReclaim(const UnitStruct& constructor, unsigned action, const UnitStruct& target)
    {
        unsigned scanned = 0;
        for (auto* order = constructor.UnitOrders; order && scanned++ < kMaxOrders; order = order->NextOrder)
            if (order->COBHandler_index == action && order->AttackTargat == &target) return order;
        return nullptr;
    }

    bool IsBuildAt(const UnitOrdersStruct* order, unsigned action, int type, const Position_Dword& position)
    {
        return order && order->COBHandler_index == action && order->BuildUnitID == static_cast<unsigned>(type)
            && order->Pos.X == position.X && order->Pos.Y == position.Y && order->Pos.Z == position.Z;
    }

    bool QueueUpgrade(UnitStruct& constructor, UnitStruct& target, int type,
        const Position_Dword& buildPos, bool shift)
    {
        const bool air = (constructor.UnitType->UnitTypeMask_0 & canfly) != 0;
        const unsigned reclaim = ActionIndex(air ? "VTOL_RECLAIMUNIT" : "RECLAIMUNIT");
        const unsigned build = ActionIndex(air ? "VTOL_MOBILEBUILD" : "MOBILEBUILD");
        if (!reclaim || !build) return false;

        // Shift dispatch toggles a matching order. Don't turn repeated clicks into
        // cancellation or issue a reclaim when a nearby build would be toggled off.
        unsigned scanned = 0;
        for (auto* order = constructor.UnitOrders; order; order = order->NextOrder)
        {
            if (++scanned >= kMaxOrders) return false;
            if ((order->COBHandler_index == reclaim && order->AttackTargat == &target)
                || (order->COBHandler_index == build
                    && std::abs(static_cast<int>(order->Pos.X) - buildPos.X) <= 16
                    && std::abs(static_cast<int>(order->Pos.Y) - buildPos.Y) <= 16)) return true;
        }

        Position_Dword reclaimPos = {};
        reclaimPos.X = target.XPos;
        reclaimPos.Y = target.YPos;
        reclaimPos.Z = target.ZPos;
        Send(reclaim, shift, &constructor, &target, &reclaimPos, 0, 0);
        auto* order = FindReclaim(constructor, reclaim, target);
        if (!order) return true;

        Send(build, 1, &constructor, nullptr, &buildPos, type, 0);
        order = FindReclaim(constructor, reclaim, target);
        if (order && IsBuildAt(order->NextOrder, build, type, buildPos))
        {
            IDDrawSurface::OutptFmtTxt("[MexUpgrade] queued builder=%u extractor=%u build=%d shift=%d",
                static_cast<unsigned short>(constructor.UnitInGameIndex),
                static_cast<unsigned short>(target.UnitInGameIndex), type, shift);
            return true;
        }

        // Both commands run on the UI thread before the engine executes an order.
        // If the build didn't follow reclaim, remove the orders this click added.
        const auto remove = reinterpret_cast<void (__stdcall*)(UnitStruct*, UnitOrdersStruct*)>(0x00439F80u);
        scanned = 0;
        for (auto* queued = constructor.UnitOrders; queued && scanned++ < kMaxOrders; queued = queued->NextOrder)
        {
            if (!IsBuildAt(queued, build, type, buildPos)) continue;
            remove(&constructor, queued);
            break;
        }
        if (order) remove(&constructor, order);
        IDDrawSurface::OutptTxt("[MexUpgrade] rejected incomplete order pair");
        return true;
    }
}

bool MexUpgrade::Enabled()
{
#if defined(TDRAW_CONFIG_ESCALATION) || defined(TDRAW_CONFIG_PROTA)
    if (!MyConfig) return false;
    static const bool enabled = [] {
        const bool active = MyConfig->GetIniBool("MexUpgrade", TRUE) != FALSE;
        if (active) IDDrawSurface::OutptTxt("[MexUpgrade] upgrade cursor and Shift queue enabled");
        return active;
    }();
    return enabled;
#else
    return false;
#endif
}

bool MexUpgrade::CheckingSite()
{
    return checkingSite;
}

void MexUpgrade::UpdateCursor(TAdynmemStruct* ta)
{
    if (!ta) return;
    const unsigned index = ta->CurrentCursora_Index;
    if (index >= sizeof(ta->cursor_ary) / sizeof(ta->cursor_ary[0])) return;
    auto* stock = ta->cursor_ary[index];
    auto* displayed = reinterpret_cast<GAFSequence*>(ta->desktopGUI.Cursor_3);
    static UpgradeCursor icon;
    // Respect custom cursors owned by other patches, such as wreck snapping.
    if (!stock || (displayed != stock && displayed != &icon.sequence)) return;
    Upgrade upgrade = {};
    const bool ready = !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_MENU) & 0x8000)
        && FindUpgrade(ta, upgrade, false);
    auto* desired = ready ? &icon.sequence : stock;
    if (displayed != desired) SetUICursor(&ta->desktopGUI, desired);
}

bool MexUpgrade::TryCommand(TAdynmemStruct* ta, const _MOUSEEVENT* mouse)
{
    if (!Enabled() || !ta || !mouse || (mouse->fwKeys & MK_CONTROL)
        || (GetAsyncKeyState(VK_MENU) & 0x8000)) return false;
    const bool shift = (mouse->fwKeys & MK_SHIFT) != 0;
    // An eligible extractor takes precedence over additive selection. All
    // other clicks fall through to the stock selection or order handler.
    const bool leftCommand = ta->InterfaceType == 0 && mouse->Msg == WM_LBUTTONUP && !(mouse->fwKeys & MK_RBUTTON);
    const bool rightCommand = ta->InterfaceType == 1 && mouse->Msg == WM_RBUTTONDOWN
        && !(mouse->fwKeys & MK_LBUTTON);
    if (!leftCommand && !rightCommand) return false;

    Upgrade upgrade = {};
    return FindUpgrade(ta, upgrade, true)
        && QueueUpgrade(*upgrade.constructor, *upgrade.target, upgrade.type, upgrade.buildPos, shift);
}
