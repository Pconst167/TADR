#!/usr/bin/env python3
"""Exercise the production menu/input controller with engine calls mocked."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

DDRAW = Path(__file__).resolve().parent.parent
source = re.sub(r'^#include "[^"\n]+"\s*$', '', (DDRAW / 'MultiBuildMenu.cpp').read_text(), flags=re.M)
STUBS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <iostream>
#include <vector>
#include "MultiBuildMenuGeometry.h"
#include "MultiBuild.h"
#include "MultiBuildAssets.h"
class Button; class Label;
using HWND=void*; using UINT=unsigned; using WPARAM=std::uintptr_t; using LPARAM=std::intptr_t;
constexpr UINT WM_KILLFOCUS=8,WM_CANCELMODE=31,WM_CAPTURECHANGED=533,WM_LBUTTONDOWN=513,WM_LBUTTONUP=514,
 WM_LBUTTONDBLCLK=515,WM_MOUSEWHEEL=522,WM_MOUSEMOVE=512,WM_KEYDOWN=256;
constexpr int WHEEL_DELTA=120;
#define LOWORD(v) (static_cast<unsigned>(v)&65535u)
#define HIWORD(v) ((static_cast<unsigned>(v)>>16)&65535u)
struct POINT{int x,y;};struct RECT{int left,top,right,bottom;};
struct Control {char name[16]={};int xpos=0,ypos=0,width=0,height=0,totalgadgets=0;};
struct GUI {GUI* per_active=nullptr;Control* ControlsAry=nullptr;int Active_b=0;};
struct Frame{};struct Entry{Frame* PtrFrame=nullptr;};struct Sequence{unsigned Frames=0;Entry PtrFrameAry[1];};
constexpr int cursornormal=0;namespace ordertype{constexpr int BUILD=14;}
struct TAdynmemStruct {
 struct Desktop{GUI* TheActive_GUIMEM=nullptr;}desktopGUI;
 RECT GameSreen_Rect={128,32,1023,767};unsigned ScreenWidth=1024,ScreenHeight=768;
 unsigned GameTime=100;int LocalHumanPlayer_PlayerID=0;void* smalFontHandle=nullptr;
 int PrepareOrder_Type=0;short BuildUnitID=-1;Sequence* cursor_ary[1]={};
};
struct UnitStruct{short UnitInGameIndex=0,UnitID=0;};
UnitStruct conA={1,10},conB={2,20};
TAdynmemStruct ta;TAdynmemStruct* taPtr=&ta;TAdynmemStruct** TAmainStruct_PtrPtr=&taPtr;
constexpr int TAInGame=3;struct Share{int TAProgress=TAInGame;}share;Share* DataShare=&share;
RECT dialogBounds={};bool dialogVisible=false;
struct Dialog{bool IsShow(RECT* r){*r=dialogBounds;return dialogVisible;}}dialog;
struct DDSURFACEDESC{unsigned dwSize,dwWidth,dwHeight;};constexpr int DD_OK=0;
struct DialogSurface{int GetSurfaceDesc(DDSURFACEDESC* d){d->dwWidth=80;d->dwHeight=100;return DD_OK;}}voteSurface;
struct VoteDialog{bool visible=false;bool IsVisible(){return visible;}DialogSurface* lpDialogSurf=&voteSurface;int posX=0,posY=160;}vote;
VoteDialog* g_VoteDialog=&vote;
struct CIncome{bool IsPointOver(int,int){return false;}}income;
struct Local{void* Dialog=&dialog;void* Income=&income;}local;Local* LocalShare=&local;
struct Program{HWND TAClass_Hwnd=reinterpret_cast<void*>(1);}program;
Program* programPtr=&program;Program** TAProgramStruct_PtrPtr=&programPtr;
POINT pointer={400,400};bool foreground=true;
HWND GetForegroundWindow(){return foreground?program.TAClass_Hwnd:nullptr;}
bool GetCursorPos(POINT* p){*p=pointer;return true;}bool ScreenToClient(HWND,POINT*){return true;}
struct OFFSCREEN{void* lpSurface;int Width,Height,lPitch;unsigned field_10,field_2C;RECT ScreenRect;};
int cursorDraws=0;void GafFrame2OFFScreen(OFFSCREEN*,Frame*,int,int){++cursorDraws;}
char ThinFont[2048]={};bool validSelection=true;int chooses=0;unsigned chosen=0;int pictureReads=0;
namespace MultiBuild {
bool Selection(TAdynmemStruct*,std::vector<UnitStruct*>& cons){if(!validSelection)return false;cons={&conA,&conB};return true;}
std::vector<MultiBuildOption> Options(TAdynmemStruct*){
 std::vector<MultiBuildOption> result(12);for(int i=0;i<12;++i){result[i].type=i+1;std::strcpy(result[i].name,"armsolar");std::strcpy(result[i].label,"Solar collector");}return result;
}
bool Choose(TAdynmemStruct* t,unsigned type){++chooses;chosen=type;t->BuildUnitID=type;t->PrepareOrder_Type=ordertype::BUILD;return true;}
}
namespace MultiBuildAssets {Picture BuildPicture(const char*){++pictureReads;Picture p;p.width=4;p.height=4;p.pixels.assign(16,208);return p;}}
'''
TEST = r'''
LPARAM At(int x,int y){return static_cast<unsigned short>(x)|(static_cast<std::uintptr_t>(static_cast<unsigned short>(y))<<16);}
bool Event(UINT message,int x,int y,WPARAM keys=0){return MultiBuildMenu::Message(program.TAClass_Hwnd,message,keys,At(x,y));}
void Click(int x,int y){assert(Event(WM_LBUTTONDOWN,x,y));assert(Event(WM_LBUTTONUP,x,y));}
int main(){
 Control controls[3];controls[0].xpos=0;controls[0].ypos=128;controls[0].width=128;controls[0].height=640;controls[0].totalgadgets=2;
 std::strcpy(controls[1].name,"ARMONOFF");controls[1].ypos=386;controls[1].width=113;controls[1].height=20;
 std::strcpy(controls[2].name,"ARMSTOP");controls[2].ypos=543;controls[2].width=55;controls[2].height=31;
 GUI gen;gen.ControlsAry=controls;ta.desktopGUI.TheActive_GUIMEM=&gen;
 constexpr int width=1024,height=768,pitch=1040;std::vector<char> pixels(pitch*height,64);
#if !MULTI_BUILD_ENABLE
 assert(!Event(WM_LBUTTONDOWN,20,180));MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);
 assert(pictureReads==0&&chooses==0);for(char c:pixels)assert(c==64);
#else
 MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);assert(pictureReads==10);
 assert(static_cast<unsigned char>(pixels[180*pitch+20])==208);
 for(int y=0;y<height;++y)for(int x=0;x<pitch;++x)if(x>=128||y<128||y>=514)assert(pixels[y*pitch+x]==64);
 MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);assert(pictureReads==10);
 assert(!Event(WM_KEYDOWN,20,180,'S'));assert(!Event(WM_LBUTTONDOWN,200,180));
 assert(!Event(WM_LBUTTONDOWN,20,600));assert(!Event(WM_MOUSEMOVE,20,180));
 Click(20,180);assert(chooses==1&&chosen==1);
 assert(Event(WM_LBUTTONDOWN,20,180));assert(Event(WM_LBUTTONUP,200,180));assert(chooses==1);
 // Orders reveal the untouched cached native body and return its input.
 gen.Active_b=0;Click(90,137);assert(gen.Active_b==1);assert(!Event(WM_LBUTTONDOWN,20,180));
 std::fill(pixels.begin(),pixels.end(),64);MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);
 assert(pixels[180*pitch+20]==64);assert(pictureReads==10);Click(20,137);
 // Page two paints only two pictures and clears all other cells.
 Click(112,481);MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);assert(pictureReads==12);
 assert(static_cast<unsigned char>(pixels[220*pitch+20])==95);Click(20,220);assert(chooses==1);
 Click(20,180);assert(chosen==11&&chooses==2);
 const WPARAM wheelUp=static_cast<WPARAM>(120)<<16;
 assert(Event(WM_MOUSEWHEEL,20,180,wheelUp));Click(20,180);assert(chosen==1&&chooses==3);
 assert(!Event(WM_MOUSEWHEEL,200,180,wheelUp));
 // Dialog/native popup pixels and input always stay above the new menu.
 dialogBounds={0,160,80,260};dialogVisible=true;assert(!Event(WM_LBUTTONDOWN,20,180));dialogVisible=false;
 vote.visible=true;assert(!Event(WM_LBUTTONDOWN,20,180));vote.visible=false;
 Control popupControl;popupControl.xpos=0;popupControl.ypos=160;popupControl.width=80;popupControl.height=100;
 GUI popup;popup.ControlsAry=&popupControl;popup.per_active=&gen;ta.desktopGUI.TheActive_GUIMEM=&popup;
 std::fill(pixels.begin(),pixels.end(),64);MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);
 assert(pixels[180*pitch+20]==64);assert(!Event(WM_LBUTTONDOWN,20,180));ta.desktopGUI.TheActive_GUIMEM=&gen;
 // A new selection opens BUILD again, and an invalid selection dirties native panels.
 Click(90,137);conB.UnitInGameIndex=3;MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);Click(20,180);assert(chooses==4);
 gen.Active_b=0;validSelection=false;MultiBuildMenu::Draw(pixels.data(),width,height,pitch,&ta);assert(gen.Active_b==1);
 assert(!Event(WM_LBUTTONDOWN,20,180));validSelection=true;
 // Focus loss cancels an owned press instead of choosing its icon later.
 assert(Event(WM_LBUTTONDOWN,20,180));assert(!Event(WM_KILLFOCUS,0,0));assert(!Event(WM_LBUTTONUP,20,180));assert(chooses==4);
#endif
 std::cout<<"Production multi-build menu input/render test passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='multi-build-menu-') as directory:
    directory=Path(directory)
    cpp=directory/'test.cpp';cpp.write_text(STUBS+'\n'+source+'\n'+TEST)
    for enabled in (0,1):
        output=directory/f'menu-{enabled}'
        subprocess.run(shlex.split(os.environ.get('CXX','g++'))+[
            '-std=c++14','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined','-g',
            '-fno-omit-frame-pointer',f'-DMULTI_BUILD_ENABLE={enabled}','-I',str(DDRAW),str(cpp),'-o',str(output)],check=True)
        subprocess.run([str(output)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
print('Verified production menu enabled/disabled')
