#!/usr/bin/env python3
"""Exercise production move feedback with passive native/surface mocks."""

from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile


DDRAW = Path(__file__).resolve().parent.parent
source = re.sub(r'^#include "[^"\n]+"\s*$', '',
                (DDRAW / 'MoveOrderFeedback.cpp').read_text(), flags=re.M)


def braces(text, opening):
    """Find a balanced production helper body (helpers contain no brace literals)."""
    depth = 0
    for closing in range(opening, len(text)):
        if text[closing] == '{':
            depth += 1
        elif text[closing] == '}':
            depth -= 1
            if depth == 0:
                return closing
    raise AssertionError('Unclosed helper body')


# Exercise the actual guarded production helpers portably. Windows SEH becomes
# ordinary exceptions; __finally uses a destructor to retain guaranteed cleanup.
paint = source.index('void PaintWithUnlock(')
protected = source.index('__try', paint)
opening = source.index('{', protected)
closing = braces(source, opening)
finally_start = source.index('__finally', closing)
finally_open = source.index('{', finally_start)
finally_close = braces(source, finally_open)
source = (source[:protected] + '{ Finally cleanup{[&](){'
          + source[finally_open + 1:finally_close] + '}};'
          + source[opening + 1:closing] + '}' + source[finally_close + 1:])
source = source.replace('__try', 'try').replace('__except (EXCEPTION_EXECUTE_HANDLER)', 'catch (...)')
mega = (DDRAW / 'MegamapControl.cpp').read_text()
signature = 'bool MegaMapControl::MoveFeedbackScreenPoint('
start = mega.index(signature)
opening = mega.index('{', start)
depth = 0
for end in range(opening, len(mega)):
    if mega[end] == '{':
        depth += 1
    elif mega[end] == '}':
        depth -= 1
        if depth == 0:
            projection = mega[start:end + 1]
            break
else:
    raise AssertionError('Unclosed passive projection')
assert 'CheckInControl' not in projection and 'CheckInMap' not in projection
assert 'PubCursor' not in projection
assert 'DrawnMovePreview' not in source

STUBS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>
#include "MoveOrderFeedbackGeometry.h"
struct Finally { std::function<void()> cleanup;~Finally(){cleanup();} };
struct POINT { int x=0,y=0; };
struct RECT { int left=0,top=0,right=0,bottom=0; };
struct _Position_Dword { unsigned short x_=0,X=0,z_=0,Z=0,y_=0,Y=0; };
using Position_Dword = _Position_Dword;
struct UnitStruct { unsigned unused=0; };
UnitStruct units[16],otherUnits[16];
struct PlayerInfoStruct { unsigned PropertyMask=0; } info[10];
struct PlayerStruct {
    PlayerInfoStruct* PlayerInfo=nullptr;
    int My_PlayerType=1; unsigned DirectPlayID=0;
    UnitStruct* Units=nullptr;UnitStruct* UnitsAry_End=nullptr;
};
struct Panel { int xpos=0,ypos=0,width=0,height=0; };
struct GUI { GUI* per_active=nullptr;Panel* ControlsAry=nullptr; };
struct TAdynmemStruct {
    int LocalHumanPlayer_PlayerID=0,GameTime=100;
    PlayerStruct Players[10];
    UnitStruct* BeginUnitsArray_p=units;UnitStruct* EndOfUnitsArray_p=units+15;
    int FeatureMapSizeX=64,FeatureMapSizeY=64,MapSizeX=1024,MapSizeY=896,SeaLevel=4;
    int EyeBallMapXPos=0,EyeBallMapYPos=0;
    RECT GameSreen_Rect={8,8,55,39};
    struct { GUI* TheActive_GUIMEM=nullptr; } desktopGUI;
};
TAdynmemStruct world,otherWorld;
TAdynmemStruct* worldPtr=&world;TAdynmemStruct** TAmainStruct_PtrPtr=&worldPtr;
constexpr int TAInGame=3,Player_LocalHuman=1,WATCH=4;
struct Share { int TAProgress=TAInGame;bool PlayingDemo=false,ehaOff=false; } shared;
Share* DataShare=&shared;
std::uint32_t clockNow=1000;
unsigned GetTickCount() { return clockNow; }
constexpr unsigned DDLOCK_WAIT=1,DDLOCK_SURFACEMEMORYPTR=2;
constexpr int DD_OK=0;
struct DDSURFACEDESC {
    unsigned dwSize=0,dwWidth=0,dwHeight=0;
    int lPitch=0;void* lpSurface=nullptr;
    struct { unsigned dwRGBBitCount=8; } ddpfPixelFormat;
};
struct TestSurface {
    static constexpr int width=64,height=48,pitch=80;
    std::vector<unsigned char> bytes=std::vector<unsigned char>(height*pitch,42);
    unsigned bits=8,reportedWidth=width,reportedHeight=height;
    int reportedPitch=pitch,lockResult=DD_OK,locks=0,unlocks=0;
    std::function<void()> duringLock;
    int Lock(void*,DDSURFACEDESC* desc,unsigned,void*) {
        ++locks;if(duringLock)duringLock();if(lockResult!=DD_OK)return lockResult;
        desc->dwWidth=reportedWidth;desc->dwHeight=reportedHeight;
        desc->lPitch=reportedPitch;desc->lpSurface=bytes.data();desc->ddpfPixelFormat.dwRGBBitCount=bits;
        return DD_OK;
    }
    int Unlock(void*) { ++unlocks;return DD_OK; }
    int Green() const { return static_cast<int>(std::count(bytes.begin(),bytes.end(),234)); }
    unsigned char At(int x,int y) const { return bytes[y*pitch+x]; }
    void Clean() { std::fill(bytes.begin(),bytes.end(),42); }
    void AssertOutside(MoveOrderFeedbackGeometry::Rect clip) const {
        for(int y=0;y<height;++y)for(int x=0;x<pitch;++x)
            if(x<clip.left||x>=clip.right||y<clip.top||y>=clip.bottom)assert(At(x,y)==42);
    }
};
using LPDIRECTDRAWSURFACE=TestSurface*;
bool megaVisible=false,throwProjection=false;
struct MegaMapControl {
    TAdynmemStruct* TAmainStruct_Ptr=&world;
    RECT MegaMapScreen={8,8,56,40};
    int PubCursorX=77,PubCursorY=88;
    bool IsBliting() { return megaVisible; }
    POINT* TAPos2ScreenPos(POINT* p,unsigned x,unsigned y,unsigned z) {
        if(throwProjection)throw std::runtime_error("native projection fault");
        p->x=static_cast<int>(x/2)-16;
        p->y=static_cast<int>(y/2)-static_cast<int>(z/4);
        return p;
    }
    bool MoveFeedbackScreenPoint(POINT*,RECT*,const Position_Dword&);
} control;
struct Minimap { MegaMapControl* Controler=&control; } minimap;
struct Expander { Minimap* myMinimap=&minimap; } expander;
Expander* GUIExpander=&expander;
'''

TESTS = r'''
static Position_Dword Pos(unsigned x,unsigned y,unsigned z=0) {
    Position_Dword p;p.X=x;p.Y=y;p.Z=z;return p;
}
static void Reset() {
    shared.TAProgress=0;MoveOrderFeedback::Draw(nullptr);
    shared={};world={};worldPtr=&world;clockNow=1000;megaVisible=false;throwProjection=false;
    for(int i=0;i<10;++i) {
        info[i]={};world.Players[i].PlayerInfo=&info[i];world.Players[i].My_PlayerType=Player_LocalHuman;
        world.Players[i].DirectPlayID=100+i;world.Players[i].Units=units+1;world.Players[i].UnitsAry_End=units+7;
    }
    control.TAmainStruct_Ptr=&world;control.PubCursorX=77;control.PubCursorY=88;
}
static void Add(unsigned unit=1) { MoveOrderFeedback::Record(&world,unit,Pos(12,12),Pos(40,28)); }
static void EmptyAfter(const std::function<void()>& change) {
    Reset();Add();change();TestSurface surface;MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);
}
int main() {
    Reset();TestSurface surface;
#if !DRAWN_MOVE_FORMATIONS_ENABLE
    Add();MoveOrderFeedback::Cancel(&world,1,Pos(40,28));MoveOrderFeedback::Draw(&surface);
    assert(surface.Green()==0 && surface.locks==0);
#else
    Add();MoveOrderFeedback::Draw(&surface);assert(surface.At(48,36)==234);surface.AssertOutside({8,8,56,40});
    clockNow=1499;surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()>0);
    clockNow=1500;surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);

    Reset();clockNow=0xffffff00u;Add();clockNow=200;surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.Green()>0);clockNow=244;surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);

    EmptyAfter([]{shared.TAProgress=0;});
    EmptyAfter([]{shared.PlayingDemo=true;});
    EmptyAfter([]{shared.ehaOff=true;});
    EmptyAfter([]{info[0].PropertyMask=WATCH;});
    EmptyAfter([]{world.Players[0].My_PlayerType=2;});
    EmptyAfter([]{world.GameTime=99;});
    EmptyAfter([]{world.Players[0].DirectPlayID=999;});
    EmptyAfter([]{world.LocalHumanPlayer_PlayerID=1;});
    EmptyAfter([]{world.BeginUnitsArray_p=otherUnits;world.EndOfUnitsArray_p=otherUnits+15;});
    EmptyAfter([]{world.Players[0].Units=units+8;world.Players[0].UnitsAry_End=units+15;});
    EmptyAfter([]{otherWorld=world;worldPtr=&otherWorld;});
    EmptyAfter([]{worldPtr=nullptr;});
    Reset();Add();shared.TAProgress=0;MoveOrderFeedback::Draw(nullptr);shared.TAProgress=TAInGame;
    world.GameTime=0;surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);

    Reset();Add();MoveOrderFeedback::Cancel(&world,2,Pos(40,28));surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.Green()>0);MoveOrderFeedback::Cancel(&world,1,Pos(56,44));surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.Green()==0);Add();MoveOrderFeedback::Cancel(&world,1,Pos(57,28));surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.Green()>0);

    Reset();Add(16);MoveOrderFeedback::Record(&world,1,Pos(12,12),Pos(65535,28));
    surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);
    Add(15);surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()>0); // inclusive end slot
    Reset();Add(0);surface.Clean();MoveOrderFeedback::Draw(&surface);assert(surface.Green()>0); // first valid slot

    Reset();world.EyeBallMapXPos=-16;Add();surface.Clean();MoveOrderFeedback::Draw(&surface);
    surface.AssertOutside({24,8,56,40});assert(surface.Green()>0);
    Reset();world.EyeBallMapXPos=10;world.EyeBallMapYPos=6;
    MoveOrderFeedback::Record(&world,1,Pos(12,12,4),Pos(40,28,8));surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.At(38,26)==234); // live camera offsets and height projection

    Reset();Panel panel;panel.xpos=20;panel.ypos=20;panel.width=24;panel.height=16;
    GUI gui;gui.per_active=&gui;gui.ControlsAry=&panel;world.desktopGUI.TheActive_GUIMEM=&gui;
    MoveOrderFeedback::Record(&world,1,Pos(0,20),Pos(44,20));surface.Clean();MoveOrderFeedback::Draw(&surface);
    for(int y=20;y<36;++y)for(int x=20;x<44;++x)assert(surface.At(x,y)==42);
    assert(surface.Green()>0);

    Reset();Add();surface.Clean();surface.bits=24;MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);
    surface.bits=8;surface.reportedPitch=-1;MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);
    surface.reportedPitch=surface.pitch;surface.reportedWidth=65537;MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0);
    surface.reportedWidth=surface.width;surface.lockResult=1;
    const int unlocksBefore=surface.unlocks;MoveOrderFeedback::Draw(&surface);assert(surface.unlocks==unlocksBefore);
    surface.lockResult=DD_OK;

    Reset();Add();surface.Clean();surface.duringLock=[] {
        assert(feedbackMutex.try_lock());feedbackMutex.unlock(); // no cache lock crosses DirectDraw Lock
        shared.TAProgress=0;
    };
    int beforeLock=surface.unlocks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.unlocks==beforeLock+1);
    surface.duringLock={};
    Reset();Add();surface.Clean();surface.duringLock=[] {worldPtr=nullptr;};
    beforeLock=surface.unlocks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.unlocks==beforeLock+1);surface.duringLock={};
    Reset();Add();surface.Clean();surface.duringLock=[] {world.EyeBallMapXPos=8;};
    beforeLock=surface.unlocks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.unlocks==beforeLock+1);surface.duringLock={};
    MoveOrderFeedback::Draw(&surface);assert(surface.At(40,36)==234); // fresh camera next frame

#if USEMEGAMAP
    Reset();Add();surface.Clean();surface.duringLock=[] {megaVisible=true;};
    beforeLock=surface.unlocks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.unlocks==beforeLock+1);surface.duringLock={};
    MoveOrderFeedback::Draw(&surface);assert(surface.At(12,22)==234); // fresh active view next frame
    Reset();Add();megaVisible=true;throwProjection=true;surface.Clean();beforeLock=surface.locks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.locks==beforeLock);
    throwProjection=false;surface.duringLock=[] {throwProjection=true;};beforeLock=surface.unlocks;
    MoveOrderFeedback::Draw(&surface);assert(surface.Green()==0 && surface.unlocks==beforeLock+1);surface.duringLock={};
    Reset();Add();megaVisible=true;surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.At(12,22)==234 && control.PubCursorX==77 && control.PubCursorY==88);
    Reset();megaVisible=true;
    MoveOrderFeedback::Record(&world,1,Pos(0,16),Pos(112,16));surface.Clean();MoveOrderFeedback::Draw(&surface);
    assert(surface.At(8,16)==234 && surface.At(48,16)==234);surface.AssertOutside({8,8,56,40});
    assert(control.PubCursorX==77 && control.PubCursorY==88);
#endif
    Reset();for(unsigned i=0;i<capacity+20;++i)Add();
    unsigned active=0;for(const auto& entry:records)if(entry.active)++active;assert(active==capacity);
#endif
    std::cout << "Move feedback expiry, lifecycle and projection passed\n";
}
'''

with tempfile.TemporaryDirectory(prefix='move-feedback-controller-') as directory:
    directory = Path(directory)
    cpp = directory / 'controller.cpp'
    cpp.write_text(STUBS + '\n' + projection + '\n' + source + '\n' + TESTS)
    for enabled, megamap in ((0, 0), (0, 1), (1, 0), (1, 1)):
        binary = directory / f'controller-{enabled}-{megamap}'
        command = shlex.split(os.environ.get('CXX', 'g++')) + [
            '-std=c++14', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
            '-Wno-unused-function', f'-DDRAWN_MOVE_FORMATIONS_ENABLE={enabled}',
            f'-DUSEMEGAMAP={megamap}', '-I', str(DDRAW), str(cpp), '-o', str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
print('Move feedback enabled/disabled and megamap combinations passed')
