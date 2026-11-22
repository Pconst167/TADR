#include "config.h"
#include "oddraw.h"
#include "MoveOrderFeedback.h"

#if DRAWN_MOVE_FORMATIONS_ENABLE
#include "tamem.h"
#include "tafunctions.h"
#include "iddrawsurface.h"
#include "MoveOrderFeedbackGeometry.h"
#if USEMEGAMAP
#include "GUIExpand.h"
#include "fullscreenminimap.h"
#include "MegamapControl.h"
#endif
#include <array>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <vector>

namespace
{
    namespace Geometry = MoveOrderFeedbackGeometry;
    constexpr unsigned lifetime = 500;
    constexpr std::size_t capacity = 4096;
    struct FeedbackRecord
    {
        Position_Dword origin = {}, destination = {};
        unsigned unitIndex = 0, at = 0;
        bool active = false;
    };
    std::mutex feedbackMutex;
    std::array<FeedbackRecord, capacity> records;
    std::size_t nextRecord = 0;
    TAdynmemStruct* savedWorld = nullptr;
    UnitStruct* savedFirst = nullptr;
    UnitStruct* savedLast = nullptr;
    UnitStruct* savedOwnFirst = nullptr;
    UnitStruct* savedOwnLast = nullptr;
    int savedPlayer = -1, lastGameTime = -1;
    unsigned savedDpid = 0, contextSerial = 0;

    void Clear()
    {
        for (auto& item : records) item.active = false;
        nextRecord = 0;
        savedWorld = nullptr; savedFirst = savedLast = savedOwnFirst = savedOwnLast = nullptr;
        savedPlayer = -1; savedDpid = 0; lastGameTime = -1;
        ++contextSerial;
    }
    bool Playable(TAdynmemStruct* ta)
    {
        if (!ta || !DataShare || DataShare->TAProgress != TAInGame
            || DataShare->PlayingDemo || DataShare->ehaOff) return false;
        const int player = ta->LocalHumanPlayer_PlayerID;
        return player >= 0 && player < 10 && ta->Players[player].PlayerInfo
            && !(ta->Players[player].PlayerInfo->PropertyMask & WATCH)
            && ta->Players[player].My_PlayerType == Player_LocalHuman;
    }
    bool Context(TAdynmemStruct* ta)
    {
        __try
        {
            if (!Playable(ta) || !ta->BeginUnitsArray_p || !ta->EndOfUnitsArray_p
                || ta->EndOfUnitsArray_p < ta->BeginUnitsArray_p)
            {
                Clear();
                return false;
            }
            const int player = ta->LocalHumanPlayer_PlayerID;
            const auto& owner = ta->Players[player];
            if (!owner.Units || !owner.UnitsAry_End || owner.UnitsAry_End < owner.Units)
            {
                Clear();
                return false;
            }
            const unsigned dpid = static_cast<unsigned>(owner.DirectPlayID);
            if (ta != savedWorld || player != savedPlayer || dpid != savedDpid
                || ta->BeginUnitsArray_p != savedFirst || ta->EndOfUnitsArray_p != savedLast
                || owner.Units != savedOwnFirst || owner.UnitsAry_End != savedOwnLast
                || ta->GameTime < lastGameTime)
            {
                Clear();
                savedWorld = ta; savedPlayer = player; savedDpid = dpid;
                savedFirst = ta->BeginUnitsArray_p; savedLast = ta->EndOfUnitsArray_p;
                savedOwnFirst = owner.Units; savedOwnLast = owner.UnitsAry_End;
            }
            lastGameTime = ta->GameTime;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { Clear(); return false; }
    }
    bool InMap(const TAdynmemStruct& ta, const Position_Dword& p)
    {
        return ta.FeatureMapSizeX > 0 && ta.FeatureMapSizeY > 8
            && p.X < static_cast<std::int64_t>(ta.FeatureMapSizeX) * 16
            && p.Y < static_cast<std::int64_t>(ta.FeatureMapSizeY - 8) * 16;
    }
    TAdynmemStruct* World()
    {
        __try { return TAmainStruct_PtrPtr ? *TAmainStruct_PtrPtr : nullptr; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    bool CapturePositions(TAdynmemStruct* ta, unsigned unitIndex, const Position_Dword& origin,
        const Position_Dword& destination, Position_Dword& from, Position_Dword& to)
    {
        __try
        {
            if (unitIndex > static_cast<std::size_t>(ta->EndOfUnitsArray_p - ta->BeginUnitsArray_p)
                || !InMap(*ta, origin) || !InMap(*ta, destination)) return false;
            from = origin; to = destination;
            if (static_cast<short>(from.Z) < 0) from.Z = static_cast<unsigned short>(ta->SeaLevel);
            if (static_cast<short>(to.Z) < 0) to.Z = static_cast<unsigned short>(ta->SeaLevel);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool SameDestination(const Position_Dword& a, const Position_Dword& b)
    {
        // Match the native Shift toggle's sixteen-world-unit tolerance.
        return std::abs(static_cast<int>(a.X) - b.X) <= 16
            && std::abs(static_cast<int>(a.Y) - b.Y) <= 16;
    }
    Geometry::Rect HalfOpen(const RECT& r)
    {
        return {r.left, r.top,
            SmallMapViewport::Bounded(static_cast<std::int64_t>(r.right) + 1,
                (std::numeric_limits<int>::min)(), (std::numeric_limits<int>::max)()),
            SmallMapViewport::Bounded(static_cast<std::int64_t>(r.bottom) + 1,
                (std::numeric_limits<int>::min)(), (std::numeric_limits<int>::max)())};
    }
    int ScreenCoordinate(std::int64_t value)
    {
        return SmallMapViewport::Bounded(value, (std::numeric_limits<int>::min)(),
            (std::numeric_limits<int>::max)());
    }
    Geometry::Rect PopupRect(const TAdynmemStruct& ta)
    {
        const auto* gui = ta.desktopGUI.TheActive_GUIMEM;
        if (!gui || !gui->per_active || !gui->ControlsAry) return {};
        const auto& panel = *gui->ControlsAry;
        return {panel.xpos, panel.ypos, panel.xpos + panel.width, panel.ypos + panel.height};
    }
    struct ScreenRecord
    {
        POINT from, to;
        Geometry::Rect clip;
        unsigned at;
    };
    struct Scene
    {
        TAdynmemStruct* world = nullptr;
        RECT viewport = {};
        Geometry::Rect clip = {}, excluded = {};
        int cameraX = 0, cameraY = 0, mapWidth = 0, mapHeight = 0;
        bool megamap = false;
#if USEMEGAMAP
        MegaMapControl* map = nullptr;
        POINT projectionOrigin = {}, projectionFar = {};
        Position_Dword farPoint = {};
        RECT megaClip = {};
#endif
    };
    bool ReadScene(TAdynmemStruct* ta, Scene& out)
    {
        __try
        {
            if (!Playable(ta)) return false;
            out.world = ta; out.viewport = ta->GameSreen_Rect;
            out.cameraX = ta->EyeBallMapXPos; out.cameraY = ta->EyeBallMapYPos;
            out.mapWidth = ta->MapSizeX; out.mapHeight = ta->MapSizeY;
            out.clip = SmallMapViewport::MapClip(HalfOpen(out.viewport), out.cameraX, out.cameraY,
                out.mapWidth, out.mapHeight);
            out.excluded = PopupRect(*ta);
#if USEMEGAMAP
            out.map = GUIExpander && GUIExpander->myMinimap ? GUIExpander->myMinimap->Controler : nullptr;
            out.megamap = out.map && out.map->IsBliting();
            if (out.megamap)
            {
                const Position_Dword origin = {};
                out.farPoint.X = static_cast<unsigned short>(SmallMapViewport::Bounded(
                    static_cast<std::int64_t>(ta->FeatureMapSizeX) * 16 - 1, 0, 65535));
                out.farPoint.Y = static_cast<unsigned short>(SmallMapViewport::Bounded(
                    static_cast<std::int64_t>(ta->FeatureMapSizeY) * 16 - 129, 0, 65535));
                if (!out.map->MoveFeedbackScreenPoint(&out.projectionOrigin, &out.megaClip, origin)
                    || !out.map->MoveFeedbackScreenPoint(&out.projectionFar, &out.megaClip, out.farPoint)) return false;
                out.clip = HalfOpen(out.megaClip);
            }
#endif
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool Project(const Scene& scene, const FeedbackRecord& record, ScreenRecord& out)
    {
        __try
        {
            out.at = record.at; out.clip = scene.clip;
#if USEMEGAMAP
            if (scene.megamap)
            {
                RECT clip = {};
                return scene.map->MoveFeedbackScreenPoint(&out.from, &clip, record.origin)
                    && scene.map->MoveFeedbackScreenPoint(&out.to, &clip, record.destination);
            }
#endif
            out.from.x = ScreenCoordinate(static_cast<std::int64_t>(record.origin.X)
                - scene.cameraX + scene.viewport.left);
            out.from.y = ScreenCoordinate(static_cast<std::int64_t>(record.origin.Y)
                - scene.cameraY + scene.viewport.top - record.origin.Z / 2);
            out.to.x = ScreenCoordinate(static_cast<std::int64_t>(record.destination.X)
                - scene.cameraX + scene.viewport.left);
            out.to.y = ScreenCoordinate(static_cast<std::int64_t>(record.destination.Y)
                - scene.cameraY + scene.viewport.top - record.destination.Z / 2);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool SameRect(Geometry::Rect a, Geometry::Rect b)
    {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }
    bool CurrentScene(const Scene& before, unsigned serial)
    {
        {
            std::lock_guard<std::mutex> lock(feedbackMutex);
            if (World() != before.world) { Clear(); return false; }
            if (!Context(before.world) || serial != contextSerial) return false;
        }
        Scene after = {};
        if (!ReadScene(before.world, after)) return false;
        if (before.megamap != after.megamap || before.cameraX != after.cameraX
            || before.cameraY != after.cameraY || before.mapWidth != after.mapWidth
            || before.mapHeight != after.mapHeight || !SameRect(before.clip, after.clip)
            || !SameRect(before.excluded, after.excluded)) return false;
#if USEMEGAMAP
        if (before.megamap && (before.map != after.map
            || before.projectionOrigin.x != after.projectionOrigin.x
            || before.projectionOrigin.y != after.projectionOrigin.y
            || before.projectionFar.x != after.projectionFar.x
            || before.projectionFar.y != after.projectionFar.y)) return false;
#endif
        std::lock_guard<std::mutex> lock(feedbackMutex);
        return serial == contextSerial && before.world == savedWorld;
    }
    bool LockSurface(LPDIRECTDRAWSURFACE surface, DDSURFACEDESC& desc)
    {
        __try
        {
            return surface->Lock(nullptr, &desc, DDLOCK_WAIT | DDLOCK_SURFACEMEMORYPTR, nullptr) == DD_OK;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void PaintWithUnlock(LPDIRECTDRAWSURFACE surface, const DDSURFACEDESC& desc, const Scene& scene,
        const std::vector<ScreenRecord>& projected, unsigned serial)
    {
        __try
        {
            if (CurrentScene(scene, serial) && desc.ddpfPixelFormat.dwRGBBitCount == 8
                && desc.dwWidth <= 65536 && desc.dwHeight <= 65536)
            {
                const unsigned now = GetTickCount();
                for (std::size_t i = 0; i < projected.size(); ++i)
                {
                    const ScreenRecord& item = projected[i];
                    if (now - item.at >= lifetime) continue;
                    const Geometry::Surface target = {static_cast<unsigned char*>(desc.lpSurface),
                        static_cast<int>(desc.dwWidth), static_cast<int>(desc.dwHeight), static_cast<int>(desc.lPitch),
                        item.clip, scene.excluded};
                    Geometry::Draw(target, item.from.x, item.from.y, item.to.x, item.to.y, scene.megamap);
                }
            }
        }
        __finally { surface->Unlock(nullptr); }
    }
    void PaintProtected(LPDIRECTDRAWSURFACE surface, const DDSURFACEDESC& desc, const Scene& scene,
        const std::vector<ScreenRecord>& projected, unsigned serial)
    {
        __try { PaintWithUnlock(surface, desc, scene, projected, serial); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

namespace MoveOrderFeedback
{
    void Record(TAdynmemStruct* world, unsigned unitIndex,
        const Position_Dword& origin, const Position_Dword& destination)
    {
        std::lock_guard<std::mutex> lock(feedbackMutex);
        Position_Dword from = {}, to = {};
        if (!Context(world) || !CapturePositions(world, unitIndex, origin, destination, from, to)) return;
        // Fixed storage makes capture constant-time and allocation-free even
        // for a large selection. Oldest feedback is replaced at the hard cap.
        auto& item = records[nextRecord];
        nextRecord = (nextRecord + 1) % capacity;
        item.origin = from; item.destination = to;
        item.unitIndex = unitIndex; item.at = GetTickCount(); item.active = true;
    }
    void Cancel(TAdynmemStruct* world, unsigned unitIndex, const Position_Dword& destination)
    {
        std::lock_guard<std::mutex> lock(feedbackMutex);
        if (!Context(world)) return;
        for (auto& item : records)
            if (item.active && item.unitIndex == unitIndex && SameDestination(item.destination, destination))
                item.active = false;
    }
    void Draw(LPDIRECTDRAWSURFACE surface)
    {
        TAdynmemStruct* ta = World();
        std::vector<FeedbackRecord> snapshot;
        unsigned serial = 0;
        try
        {
            std::lock_guard<std::mutex> lock(feedbackMutex);
            if (!Context(ta)) return;
            const unsigned now = GetTickCount();
            for (auto& item : records)
            {
                if (!item.active) continue;
                if (now - item.at >= lifetime) { item.active = false; continue; }
                snapshot.push_back(item);
            }
            serial = contextSerial;
        }
        catch (...) { return; }
        if (!surface || snapshot.empty()) return;

        // All native reads and projections are guarded POD helpers. DirectDraw
        // Lock may yield: the epoch/view is validated again before painting.
        Scene scene = {};
        if (!ReadScene(ta, scene)) return;
        std::vector<ScreenRecord> projected;
        try
        {
            projected.reserve(snapshot.size());
            for (const auto& item : snapshot)
            {
                ScreenRecord out = {};
                if (Project(scene, item, out)) projected.push_back(out);
            }
        }
        catch (...) { return; }
        if (projected.empty()) return;
        DDSURFACEDESC desc = {};
        desc.dwSize = sizeof(desc);
        if (!LockSurface(surface, desc)) return;
        // Separate POD SEH envelopes avoid MSVC C2712 and always release a
        // successfully locked surface, including on a native rendering fault.
        PaintProtected(surface, desc, scene, projected, serial);
    }
}
#else
namespace MoveOrderFeedback
{
    void Record(TAdynmemStruct*, unsigned, const _Position_Dword&, const _Position_Dword&) {}
    void Cancel(TAdynmemStruct*, unsigned, const _Position_Dword&) {}
    void Draw(LPDIRECTDRAWSURFACE) {}
}
#endif
