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

#include <cstring>

namespace
{
    constexpr int kContactScale = 2;

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

    void DrawScaledContact(OFFSCREEN* surface, GAFFrame* frame, int x, int y)
    {
        // Decode through TA so compressed mod sprites keep their original colors
        // and transparency. The buffer belongs to this call, not a loaded world.
        BYTE icon[64 * 64];
        std::memset(icon, frame->Background, static_cast<size_t>(frame->Width) * frame->Height);
        OFFSCREEN decoded = *surface;
        decoded.Width = decoded.lPitch = frame->Width;
        decoded.Height = frame->Height;
        decoded.lpSurface = icon;
        decoded.ScreenRect = {0, 0, frame->Width - 1, frame->Height - 1};
        DrawContactFrame(&decoded, frame, static_cast<short>(frame->xPosition),
            static_cast<short>(frame->yPosition));

        const int left = x - static_cast<short>(frame->xPosition) * kContactScale;
        const int top = y - static_cast<short>(frame->yPosition) * kContactScale;
        auto* pixels = static_cast<BYTE*>(surface->lpSurface);
        const RECT& clip = surface->ScreenRect;
        for (int row = 0; row < frame->Height; ++row)
        {
            for (int column = 0; column < frame->Width; ++column)
            {
                const BYTE color = icon[row * frame->Width + column];
                if (color == frame->Background) continue;
                for (int dy = 0; dy < kContactScale; ++dy)
                {
                    const int screenY = top + row * kContactScale + dy;
                    if (screenY < clip.top || screenY > clip.bottom) continue;
                    for (int dx = 0; dx < kContactScale; ++dx)
                    {
                        const int screenX = left + column * kContactScale + dx;
                        if (screenX >= clip.left && screenX <= clip.right)
                            pixels[static_cast<size_t>(screenY) * surface->lPitch + screenX] = color;
                    }
                }
            }
        }
    }
}

bool MainMapRadar::Enabled()
{
    if (!MyConfig) return false;
    static const bool enabled = [] {
        const bool active = MyConfig->GetIniBool("MainMapRadarContacts", TRUE) != FALSE;
        if (active) IDDrawSurface::OutptTxt("[MainMapRadar] build=contacts-v2-large");
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
        const int left = x - static_cast<short>(frame->xPosition) * kContactScale;
        const int top = y - static_cast<short>(frame->yPosition) * kContactScale;
        if (left >= clip.right || top >= clip.bottom
            || left + frame->Width * kContactScale <= clip.left
            || top + frame->Height * kContactScale <= clip.top) continue;
        DrawScaledContact(&destination, frame, x, y);
    }
}
