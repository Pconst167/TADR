#include "config.h"
#include "oddraw.h"
#include "iddrawsurface.h"
#include "tamem.h"
#include "tafunctions.h"
#include "TAConfig.h"
#include "Gaf.h"
#include "GUIExpand.h"
#include "fullscreenminimap.h"
#include "MegamapControl.h"
#include "SmallMapViewport.h"
#include "MainMapRadar.h"

namespace
{
    bool MegamapActive()
    {
#if USEMEGAMAP
        return GUIExpander && GUIExpander->myMinimap && GUIExpander->myMinimap->Controler
            && GUIExpander->myMinimap->Controler->IsBliting();
#else
        return false;
#endif
    }

    void DrawContactFrame(OFFSCREEN* surface, GAFFrame* frame, int x, int y)
    {
        // Native DrawFrame supports both compressed and uncompressed mod assets,
        // subtracts the frame hotspot and clips to this private OFFSCREEN context.
        using DrawFrame = void (__stdcall*)(OFFSCREEN*, GAFFrame*, int, int);
        reinterpret_cast<DrawFrame>(0x004B7F90u)(surface, frame, x, y);
    }
}

bool MainMapRadar::Enabled()
{
    if (!MyConfig) return false;
    static const bool enabled = [] {
        const bool active = MyConfig->GetIniBool("MainMapRadarContacts", TRUE) != FALSE;
        if (active) IDDrawSurface::OutptTxt("[MainMapRadar] build=contacts-v1");
        return active;
    }();
    return enabled;
}

void MainMapRadar::Draw(TAdynmemStruct* ta, _OFFSCREEN* surface)
{
    if (!ta || !DataShare || DataShare->TAProgress != TAInGame || !Enabled() || MegamapActive()
        || !surface || !surface->lpSurface || surface->Width <= 0 || surface->Height <= 0
        || surface->lPitch < surface->Width || !ta->BeginUnitsArray_p || !ta->EndOfUnitsArray_p
        || !ta->RadarUnits || !ta->radlogo
        || ta->NumHotRadarUnits <= 0 || ta->MapSizeX <= 0 || ta->MapSizeY <= 0) return;

    // The native scan/list belongs to the viewed player's sensors, including
    // shared radar, sonar and enemy jamming. Do not scan all enemy unit slots.
    const int viewPlayer = ta->LOS_Sight_PlayerID;
    if (viewPlayer < 0 || viewPlayer >= 10 || !ta->Players[viewPlayer].PlayerInfo) return;
    // DrawRadarFinal's unit walk includes EndOfUnitsArray_p. Validate the byte
    // span before deriving capacity, so the last legal slot is not dropped.
    const auto begin = reinterpret_cast<std::uintptr_t>(ta->BeginUnitsArray_p);
    const auto end = reinterpret_cast<std::uintptr_t>(ta->EndOfUnitsArray_p);
    if (end < begin || (end - begin) % sizeof(UnitStruct)) return;
    const auto slots = (end - begin) / sizeof(UnitStruct) + 1;
    const int count = ta->NumHotRadarUnits;
    if (slots > 65536 || static_cast<std::uintptr_t>(count) > slots) return;

    const RECT& r = ta->GameSreen_Rect;
    if (r.left < 0 || r.top < 0 || r.right < r.left || r.bottom < r.top
        || r.right >= surface->Width || r.bottom >= surface->Height) return;
    const SmallMapViewport::Rect view = {r.left, r.top, r.right + 1, r.bottom + 1};
    const SmallMapViewport::Surface pixels = {static_cast<BYTE*>(surface->lpSurface),
        surface->Width, surface->Height, surface->lPitch,
        {surface->ScreenRect.left, surface->ScreenRect.top,
            surface->ScreenRect.right + 1, surface->ScreenRect.bottom + 1}};
    const auto clip = SmallMapViewport::Intersect(SmallMapViewport::Clip(pixels, view),
        SmallMapViewport::MapClip(view, ta->EyeBallMapXPos, ta->EyeBallMapYPos, ta->MapSizeX, ta->MapSizeY));
    if (SmallMapViewport::Empty(clip)) return;
    OFFSCREEN destination = *surface;
    destination.ScreenRect = {clip.left, clip.top, clip.right - 1, clip.bottom - 1};

    for (int i = 0; i < count; ++i)
    {
        const unsigned slot = static_cast<unsigned short>(ta->RadarUnits[i].ID);
        if (!slot || slot >= static_cast<unsigned>(slots)) continue;
        auto& unit = ta->BeginUnitsArray_p[slot];
        if (!unit.UnitID || !unit.UnitType || static_cast<unsigned short>(unit.UnitInGameIndex) != slot
            || !(unit.UnitSelected & 0x10000000) || (unit.UnitSelected & 0x4000)
            || !(unit.UnitSelected & 0x300)) continue;
        const unsigned owner = unit.cOwnerID;
        if (owner >= 10 || unit.Owner_PlayerPtr0 != &ta->Players[owner]
            || !ta->Players[owner].PlayerInfo) continue;
        // Recheck current sensor bits above so an old list entry cannot survive
        // signal loss. Visual LOS uses normal models instead of extra markers.
        if (CheckUnitInPlayerLOS(&ta->Players[viewPlayer], &unit) == 1) continue;

        const int color = static_cast<unsigned char>(ta->Players[owner].PlayerInfo->PlayerLogoColor);
        if (color >= ta->radlogo->Frames) continue;
        auto* frame = Index2Frame_InSequence(ta->radlogo, color);
        if (!frame || !frame->PtrFrameBits || !frame->Width || !frame->Height
            || frame->Width > 64 || frame->Height > 64) continue;
        // RadarUnits.x/y are minimap pixels, not world positions. Project the
        // live unit as TA does for the native minimap, including altitude.
        const int x = static_cast<short>(unit.XPos) - ta->EyeBallMapXPos + r.left;
        const int altitude = static_cast<short>(unit.ZPos);
        // Match the native arithmetic shift for odd, negative underwater heights.
        const int height = altitude / 2 - (altitude < 0 && altitude % 2 != 0 ? 1 : 0);
        const int y = static_cast<short>(unit.YPos) - height
            - ta->EyeBallMapYPos + r.top;
        const int left = x - static_cast<short>(frame->xPosition);
        const int top = y - static_cast<short>(frame->yPosition);
        if (left >= clip.right || top >= clip.bottom
            || left + frame->Width <= clip.left || top + frame->Height <= clip.top) continue;
        DrawContactFrame(&destination, frame, x, y);
    }
}
