#!/usr/bin/env python3
"""Exercise the real normal-map radar overlay with native rendering mocked.

The production filtering, projection and clipping code is compiled unchanged.
Only TA's structures/native calls and platform headers are replaced; guarded
pixel buffers and ASan/UBSan check the resulting writes. Win32 ABI validation
is performed by the separate MSVC build.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


DDRAW = Path(__file__).resolve().parent.parent

HARNESS = r'''
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
#include "SmallMapViewport.h"
#define __stdcall
#define __cdecl
using BOOL = int;
using BYTE = unsigned char;
using DWORD = unsigned;
using WORD = unsigned short;
using LPBYTE = BYTE*;
using LPVOID = void*;
constexpr int TRUE = 1, FALSE = 0, TAInGame = 1, WATCH = 4, Player_LocalHuman = 1;
struct RECT { int left, top, right, bottom; };
struct POINT { int x, y; };
struct _OFFSCREEN {
    int Width = 0, Height = 0, lPitch = 0;
    void* lpSurface = nullptr;
    unsigned field_10 = 10000, field_14 = 0;
    unsigned short field_18 = 0, field_1A = 0;
    RECT ScreenRect = {};
    unsigned field_2C = 1;
};
using OFFSCREEN = _OFFSCREEN;
struct GAFFrame {
    unsigned short Width, Height, xPosition, yPosition;
    unsigned char Background, Compressed;
    unsigned short FramePointers;
    unsigned Unknown0;
    unsigned char* PtrFrameBits;
    unsigned FPS;
};
struct GAFENTRY { GAFFrame* PtrFrame; int Animated; };
struct GAFSequence {
    unsigned short Frames = 0;
    unsigned Signature = 1;
    unsigned short Padding = 0;
    char Name[32] = {};
    GAFENTRY PtrFrameAry[10] = {};
};
using _GAFFrame = GAFFrame;
using _GAFSequence = GAFSequence;
using PGAFFrame = GAFFrame*;
using PGAFSequence = GAFSequence*;
struct UnitDefStruct { int FootX = 1, FootY = 1; };
struct PlayerInfoStruct { unsigned PropertyMask = 0; int PlayerLogoColor = 0; };
struct PlayerStruct {
    PlayerInfoStruct* PlayerInfo = nullptr;
    int PlayerAryIndex = 0, My_PlayerType = Player_LocalHuman;
};
struct UnitStruct {
    short UnitID = 0;
    UnitDefStruct* UnitType = nullptr;
    PlayerStruct* Owner_PlayerPtr0 = nullptr;
    unsigned UnitSelected = 0;
    short UnitInGameIndex = 0;
    unsigned short XPos = 0, YPos = 0, ZPos = 0;
    unsigned char cOwnerID = 0;
    unsigned short cIsCloaked = 0;
};
struct RadarUnit_ { short ID = 0; unsigned x = 0, y = 0; };
struct TAdynmemStruct {
    UnitStruct* BeginUnitsArray_p = nullptr;
    UnitStruct* EndOfUnitsArray_p = nullptr;
    RadarUnit_* RadarUnits = nullptr;
    int NumHotRadarUnits = 0;
    PlayerStruct Players[10];
    int LOS_Sight_PlayerID = 0, LocalHumanPlayer_PlayerID = 0;
    int EyeBallMapXPos = 0, EyeBallMapYPos = 0;
    int MapSizeX = 1024, MapSizeY = 1024;
    int FeatureMapSizeX = 64, FeatureMapSizeY = 72;
    RECT GameSreen_Rect = {128,32,511,383};
    GAFSequence* radlogo = nullptr;
    GAFSequence* radlogohigh = nullptr;
};
struct SharedData { int TAProgress = TAInGame; bool PlayingDemo = false; } shared;
SharedData* DataShare = &shared;
struct Config {
    bool active = true;
    int reads = 0;
    BOOL GetIniBool(const char* key, BOOL fallback) {
        assert(std::string(key) == "MainMapRadarContacts" && fallback == TRUE);
        ++reads; return active;
    }
} config;
Config* MyConfig = nullptr;
struct IDDrawSurface {
    static void OutptTxt(const char*) {}
    template<class... Args> static void OutptFmtTxt(const char*, Args...) {}
};
struct Control { bool active = false; BOOL IsBliting() { return active; } } control;
struct Minimap { Control* Controler = nullptr; } minimap;
struct Expand { Minimap* myMinimap = nullptr; } expand;
Expand* GUIExpander = nullptr;
TAdynmemStruct* currentWorld = nullptr;
int losCalls = 0, drawCalls = 0, frameCalls = 0;
int lastViewedPlayer = -1, lastFrame = -1;
int lastX = 0, lastY = 0;
RECT lastClip = {};
int los[16] = {};
int CheckUnitInPlayerLOS(PlayerStruct* player, UnitStruct* unit) {
    assert(currentWorld && player >= currentWorld->Players && player < currentWorld->Players+10);
    assert(unit >= currentWorld->BeginUnitsArray_p && unit <= currentWorld->EndOfUnitsArray_p);
    ++losCalls; lastViewedPlayer = static_cast<int>(player-currentWorld->Players);
    return los[unit-currentWorld->BeginUnitsArray_p];
}
GAFFrame* Index2Frame_InSequence(GAFSequence* sequence, int frame) {
    ++frameCalls; lastFrame = frame;
    assert(sequence && frame >= 0 && frame < sequence->Frames);
    return sequence->PtrFrameAry[frame].PtrFrame;
}
void MockDrawContactFrame(_OFFSCREEN* surface, GAFFrame* frame, int x, int y) {
    assert(surface && frame);
    ++drawCalls; lastX = x; lastY = y; lastClip = surface->ScreenRect;
    for (int iy=0; iy<frame->Height; ++iy) for (int ix=0; ix<frame->Width; ++ix) {
        const int dx = x + ix - static_cast<short>(frame->xPosition);
        const int dy = y + iy - static_cast<short>(frame->yPosition);
        const BYTE value = frame->PtrFrameBits[iy*frame->Width+ix];
        if (value == frame->Background || dx < surface->ScreenRect.left || dx > surface->ScreenRect.right
            || dy < surface->ScreenRect.top || dy > surface->ScreenRect.bottom) continue;
        assert(dx >= 0 && dx < surface->Width && dy >= 0 && dy < surface->Height);
        static_cast<BYTE*>(surface->lpSurface)[dy*surface->lPitch+dx] = value;
    }
}
BOOL IsPlayerAllyUnitStrict(int slot, int player) {
    return currentWorld && currentWorld->BeginUnitsArray_p[slot].Owner_PlayerPtr0 == &currentWorld->Players[player];
}
BOOL IsPlayerAllyUnit(int slot, int player) { return IsPlayerAllyUnitStrict(slot,player); }
namespace MainMapRadar { bool Enabled(); void Draw(TAdynmemStruct*, _OFFSCREEN*); }

// PRODUCTION_SOURCE

struct World {
    TAdynmemStruct ta;
    UnitStruct units[16];
    UnitDefStruct def;
    RadarUnit_ contacts[16];
    PlayerInfoStruct info[10];
    GAFSequence logo;
    BYTE icon[16] = {9,211,211,9,211,211,211,211,211,211,211,211,9,211,211,9};
    GAFFrame frame = {4,4,2,2,9,0,0,0,icon,0};
    static constexpr int width = 640, height = 480, pitch = 648, guard = 32;
    std::vector<BYTE> pixels = std::vector<BYTE>(guard+pitch*height+guard,0xcc);
    _OFFSCREEN surface;
    World() {
        shared = SharedData{}; DataShare = &shared;
        losCalls = drawCalls = frameCalls = 0; lastViewedPlayer = lastFrame = -1;
        std::fill(los,los+16,0);
        currentWorld = &ta;
        ta.BeginUnitsArray_p = units; ta.EndOfUnitsArray_p = units+15;
        ta.RadarUnits = contacts; ta.NumHotRadarUnits = 1; contacts[0].ID = 2;
        ta.EyeBallMapXPos = 20; ta.EyeBallMapYPos = 30;
        for (int i=0; i<10; ++i) {
            ta.Players[i].PlayerAryIndex = i; ta.Players[i].PlayerInfo = &info[i];
            info[i].PlayerLogoColor = i;
        }
        units[2].UnitID = 1; units[2].UnitInGameIndex = 2; units[2].UnitType = &def;
        units[2].Owner_PlayerPtr0 = &ta.Players[1]; units[2].cOwnerID = 1;
        units[2].UnitSelected = 0x10000000 | 0x100;
        units[2].XPos = 160; units[2].YPos = 180; units[2].ZPos = 32;
        logo.Frames = 10; for (auto& entry : logo.PtrFrameAry) entry = {&frame,10};
        ta.radlogo = ta.radlogohigh = &logo;
        surface.Width = width; surface.Height = height; surface.lPitch = pitch;
        surface.lpSurface = pixels.data()+guard; surface.ScreenRect = {0,0,width-1,height-1};
        control.active = false; minimap.Controler = &control; expand.myMinimap = &minimap; GUIExpander = &expand;
    }
    void Draw() { MainMapRadar::Draw(&ta,&surface); }
    bool Unchanged() const { return std::all_of(pixels.begin(),pixels.end(),[](BYTE c){return c==0xcc;}); }
    void CheckGuards() const {
        for (int i=0; i<guard; ++i) assert(pixels[i]==0xcc && pixels[pixels.size()-1-i]==0xcc);
        for (int y=0; y<height; ++y) for (int x=width; x<pitch; ++x) assert(pixels[guard+y*pitch+x]==0xcc);
    }
};

void ResetPaint(World& world) {
    std::fill(world.pixels.begin(),world.pixels.end(),0xcc);
    losCalls = drawCalls = frameCalls = 0;
}
bool SameRect(RECT a, RECT b) {
    return a.left==b.left && a.top==b.top && a.right==b.right && a.bottom==b.bottom;
}
void OnlyInside(const World& world, RECT clip) {
    world.CheckGuards();
    for (int y=0; y<World::height; ++y) for (int x=0; x<World::width; ++x)
        if (world.pixels[World::guard+y*World::pitch+x] != 0xcc)
            assert(x>=clip.left && x<=clip.right && y>=clip.top && y<=clip.bottom);
}
void ScreenCenter(World& world, int x, int y) {
    world.units[2].XPos = static_cast<unsigned short>(x-world.ta.GameSreen_Rect.left+world.ta.EyeBallMapXPos);
    world.units[2].YPos = static_cast<unsigned short>(y-world.ta.GameSreen_Rect.top+world.ta.EyeBallMapYPos+world.units[2].ZPos/2);
}
void TestDetectedContactsAndProjection() {
    World world;
    const RECT originalClip = world.surface.ScreenRect;
    world.units[3] = world.units[2]; world.units[3].UnitInGameIndex = 3;
    world.units[3].XPos += 100; // A live unit outside the native contact list must not leak.
    UnitStruct snapshot[16]; std::memcpy(snapshot,world.units,sizeof(snapshot));
    world.Draw();
    assert(drawCalls==1 && frameCalls==1 && lastViewedPlayer==0 && lastFrame==1);
    assert(lastX==268 && lastY==166);
    assert(!world.Unchanged() && SameRect(originalClip,world.surface.ScreenRect));
    assert(std::memcmp(snapshot,world.units,sizeof(snapshot))==0);
    OnlyInside(world,world.ta.GameSreen_Rect);
    for (unsigned sensor : {0x100u,0x200u,0x300u}) {
        ResetPaint(world); world.units[2].UnitSelected = 0x10000000 | sensor;
        world.Draw(); assert(drawCalls==1 && !world.Unchanged());
    }
    for (const auto& altitude : std::vector<std::pair<int,int>>{{-20,192},{-1,183},{-3,184},{1,182},{3,181}}) {
        ResetPaint(world); world.units[2].ZPos = static_cast<unsigned short>(altitude.first);
        world.Draw(); assert(drawCalls==1 && lastY==altitude.second);
        OnlyInside(world,world.ta.GameSreen_Rect); // Native signed SAR handles odd submerged heights.
    }

    World lastSlot; lastSlot.units[15] = lastSlot.units[2]; lastSlot.units[15].UnitInGameIndex = 15;
    lastSlot.contacts[0].ID = 15;
    lastSlot.Draw(); assert(drawCalls==1 && !lastSlot.Unchanged()); // EndOfUnitsArray_p is inclusive.
}
void TestContactLossJammingAndSight() {
    World world; world.Draw(); assert(drawCalls==1);
    ResetPaint(world); world.units[2].UnitSelected &= ~0x300u;
    world.Draw(); assert(drawCalls==0 && world.Unchanged()); // Jamming clears the native sensor flags.
    world.units[2].UnitSelected |= 0x100;
    world.ta.NumHotRadarUnits = 0;
    world.Draw(); assert(drawCalls==0 && world.Unchanged()); // List membership is required too.
    world.ta.NumHotRadarUnits = 1; los[2] = 1;
    world.Draw(); assert(drawCalls==0 && world.Unchanged()); // Visible models need no extra marker.
    los[2] = 0; world.Draw(); assert(drawCalls==1);
    ResetPaint(world); shared.TAProgress = 0;
    world.Draw(); assert(drawCalls==0 && world.Unchanged());
    shared.TAProgress = TAInGame;
    world.Draw(); assert(drawCalls==1); // Returning to a new view has no remembered contacts.
}
void TestInvalidContactAndWorldState() {
    for (int reason=0; reason<36; ++reason) {
        World world;
        switch (reason) {
        case 0: world.units[2].UnitID = 0; break;
        case 1: world.units[2].UnitType = nullptr; break;
        case 2: world.units[2].Owner_PlayerPtr0 = nullptr; break;
        case 3: world.units[2].cOwnerID = 10; break;
        case 4: world.units[2].Owner_PlayerPtr0 = &world.ta.Players[3]; break;
        case 5: world.units[2].UnitSelected &= ~0x10000000u; break;
        case 6: world.units[2].UnitSelected |= 0x4000; break;
        case 7: world.units[2].UnitSelected &= ~0x300u; break;
        case 8: world.contacts[0].ID = 0; break;
        case 9: world.contacts[0].ID = 16; break;
        case 10: world.contacts[0].ID = -1; break;
        case 11: world.units[2].UnitInGameIndex = 3; break;
        case 12: los[2] = 1; break;
        case 13: world.ta.LOS_Sight_PlayerID = -1; break;
        case 14: world.ta.LOS_Sight_PlayerID = 10; break;
        case 15: world.ta.Players[0].PlayerInfo = nullptr; break;
        case 16: world.ta.NumHotRadarUnits = 0; break;
        case 17: world.ta.NumHotRadarUnits = -1; break;
        case 18: world.ta.NumHotRadarUnits = 17; break;
        case 19: world.ta.RadarUnits = nullptr; break;
        case 20: world.ta.BeginUnitsArray_p = nullptr; break;
        case 21: world.ta.EndOfUnitsArray_p = nullptr; break;
        case 22: world.ta.EndOfUnitsArray_p = world.ta.BeginUnitsArray_p; break;
        case 23: world.ta.Players[1].PlayerInfo = nullptr; break;
        case 24: world.info[1].PlayerLogoColor = -1; break;
        case 25: world.info[1].PlayerLogoColor = 10; break;
        case 26: world.ta.radlogo = nullptr; break;
        case 27: world.logo.PtrFrameAry[1].PtrFrame = nullptr; break;
        case 28: DataShare = nullptr; break;
        case 29: shared.TAProgress = 0; break;
        case 30: MyConfig = nullptr; break;
        case 31: world.frame.PtrFrameBits = nullptr; break;
        case 32: world.frame.Width = 0; break;
        case 33: world.frame.Height = 0; break;
        case 34: world.frame.Width = 65; break;
        case 35: world.frame.Height = 65; break;
        }
        world.Draw();
        assert(drawCalls==0 && world.Unchanged()); world.CheckGuards();
        MyConfig = &config;
    }
    World world; MainMapRadar::Draw(nullptr,&world.surface);
    MainMapRadar::Draw(&world.ta,nullptr);
    assert(drawCalls==0 && world.Unchanged());
}
void TestViewPlayerAndMegamap() {
    World world;
    world.ta.LOS_Sight_PlayerID = 3; world.ta.LocalHumanPlayer_PlayerID = 0;
    shared.PlayingDemo = true;
    world.Draw(); assert(drawCalls==1 && lastViewedPlayer==3);
    ResetPaint(world); world.info[0].PropertyMask |= WATCH;
    world.Draw(); assert(drawCalls==1 && lastViewedPlayer==3);
    ResetPaint(world); los[2] = 1;
    world.Draw(); assert(drawCalls==0 && world.Unchanged());
    los[2] = 0; control.active = true;
    ResetPaint(world); world.Draw();
#if USEMEGAMAP
    assert(drawCalls==0 && world.Unchanged());
#else
    assert(drawCalls==1);
#endif
    ResetPaint(world); control.active = false; GUIExpander = nullptr;
    world.Draw(); assert(drawCalls==1); // The normal map does not depend on a megamap instance.
}
void TestSurfaceClippingAndMargins() {
    for (int reason=0; reason<8; ++reason) {
        World world;
        switch (reason) {
        case 0: world.surface.lpSurface = nullptr; break;
        case 1: world.surface.Width = 0; break;
        case 2: world.surface.Height = 0; break;
        case 3: world.surface.lPitch = -1; break;
        case 4: world.surface.lPitch = world.surface.Width-1; break;
        case 5: world.surface.ScreenRect = {200,200,100,100}; break;
        case 6: world.ta.GameSreen_Rect = {200,200,100,100}; break;
        case 7: world.ta.MapSizeX = 0; break;
        }
        world.Draw(); assert(world.Unchanged()); world.CheckGuards();
    }
    for (const POINT center : std::vector<POINT>{{128,32},{511,32},{128,383},{511,383},
        {127,100},{512,100},{200,31},{200,384}}) {
        World world; ScreenCenter(world,center.x,center.y); const RECT before = world.surface.ScreenRect;
        world.Draw(); OnlyInside(world,world.ta.GameSreen_Rect);
        assert(SameRect(world.surface.ScreenRect,before));
    }
    World inset; inset.surface.ScreenRect = {260,160,269,169};
    const RECT before = inset.surface.ScreenRect;
    inset.Draw(); assert(!inset.Unchanged()); OnlyInside(inset,before);
    assert(SameRect(inset.surface.ScreenRect,before));

    World margin; margin.ta.EyeBallMapXPos = -100; margin.ta.EyeBallMapYPos = -50;
    margin.ta.MapSizeX = 100; margin.ta.MapSizeY = 80;
    margin.units[2].XPos = 5; margin.units[2].YPos = 15; margin.units[2].ZPos = 0;
    margin.Draw(); assert(!margin.Unchanged()); OnlyInside(margin,{228,82,327,161});
}

int main(int argc, char** argv) {
    assert(argc == 2); config.active = std::string(argv[1]) == "1";
    assert(!MainMapRadar::Enabled()); MyConfig = &config;
    assert(MainMapRadar::Enabled() == config.active);
    if (!config.active) {
        World world; world.Draw(); assert(world.Unchanged() && drawCalls == 0);
    } else {
        TestDetectedContactsAndProjection();
        TestContactLossJammingAndSight();
        TestInvalidContactAndWorldState();
        TestViewPlayerAndMegamap();
        TestSurfaceClippingAndMargins();
    }
    std::cout << "Normal-map radar regression tests passed (enabled=" << config.active << ")\n";
}
'''


def main():
    # This is an engine-thread overlay, not a second presentation-thread scan.
    hook_source = (DDRAW / "TABugFix.cpp").read_text()
    wrapper = re.search(
        r'void __stdcall DrawStripWithSmallMapMargins\(OFFSCREEN\* surface\)\s*\{(.*?)\n    \}',
        hook_source, re.S,
    )
    if not wrapper:
        raise RuntimeError("Missing native post-fog/pre-HUD wrapper")
    body = wrapper.group(1)
    assert body.count("MainMapRadar::Draw(") == 1
    assert body.index("MainMapRadar::Draw(") < body.index("0x004689C0u")
    assert "0x00469F9Fu" in hook_source and "&DrawStripWithSmallMapMargins" in hook_source
    assert '<ClCompile Include="MainMapRadar.cpp" />' in (DDRAW / "ddraw.vcxproj").read_text()

    source = (DDRAW / "MainMapRadar.cpp").read_text()
    source = re.sub(r'^#include\s+"[^\n]*"\s*$', "", source, flags=re.M)
    bridge = "reinterpret_cast<DrawFrame>(0x004B7F90u)"
    if source.count(bridge) != 1:
        raise RuntimeError("Expected one native contact-frame bridge to mock")
    source = source.replace(bridge, "static_cast<DrawFrame>(MockDrawContactFrame)")
    harness = HARNESS.replace("// PRODUCTION_SOURCE", source)
    with tempfile.TemporaryDirectory(prefix="main-map-radar-test-") as temp:
        cpp = Path(temp) / "main_map_radar_test.cpp"
        cpp.write_text(harness)
        env = os.environ.copy()
        env["ASAN_OPTIONS"] = env.get("ASAN_OPTIONS", "") + ":detect_leaks=0"
        for mod, megamap in (("ESCALATION",1),("PROTA",1),("OTA",1),("ESCALATION",0)):
            binary = Path(temp) / ("main_map_radar_" + mod.lower()+str(megamap))
            subprocess.run(shlex.split(os.environ.get("CXX", "g++")) + [
                "-std=c++11", "-Wall", "-Wextra", "-Werror", "-g",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-DTDRAW_CONFIG_" + mod, "-DUSEMEGAMAP="+str(megamap), "-I", str(DDRAW), str(cpp), "-o", str(binary),
            ], check=True)
            for configured in ("1", "0"):
                subprocess.run([str(binary), configured], check=True, env=env)


if __name__ == "__main__":
    main()
