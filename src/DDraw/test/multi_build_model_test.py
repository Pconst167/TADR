#!/usr/bin/env python3
"""Exercise the actual selection/union/placement controller with native data mocked."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

DDRAW = Path(__file__).resolve().parent.parent
source = re.sub(r'^#include "[^"\n]+"\s*$', '', (DDRAW / 'MultiBuild.cpp').read_text(), flags=re.M)
STUBS = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "MultiBuild.h"
#include "MultiBuildAssets.h"
constexpr unsigned builder=0x40, WATCH=4, MK_SHIFT=4, UnitSelected_State=0x10;
constexpr int Player_LocalHuman=1, TAInGame=3;
namespace ordertype { constexpr unsigned char STOP=0, BUILD=14; }
struct UnitDefStruct {
 char Name[32]={},UnitName[32]={},UnitDescription[64]={};
 unsigned UnitTypeMask_0=0; unsigned char bmcode=0; short FootX=2,FootY=2;
};
struct PlayerInfo { unsigned PropertyMask=0; };
struct PlayerStruct;
struct UnitStruct {
 unsigned UnitSelected=0; short UnitID=0; UnitDefStruct* UnitType=nullptr;
 PlayerStruct* Owner_PlayerPtr0=nullptr; float Nanoframe=0; int IsUnit=1;
};
struct PlayerStruct {
 int PlayerActive=1, My_PlayerType=Player_LocalHuman; ::PlayerInfo* PlayerInfo=nullptr;
 UnitStruct* Units=nullptr;UnitStruct* UnitsAry_End=nullptr;
};
struct TAdynmemStruct {
 UnitDefStruct* UnitDef=nullptr;unsigned UNITINFOCount=0;
 UnitStruct* BeginUnitsArray_p=nullptr;UnitStruct* EndOfUnitsArray_p=nullptr;
 int LocalHumanPlayer_PlayerID=0,GameTime=100;PlayerStruct Players[10];
 short BuildUnitID=0;unsigned char PrepareOrder_Type=ordertype::STOP;
};
struct Shared { int TAProgress=TAInGame,PlayingDemo=0; } shared;
Shared* DataShare=&shared;
struct _MOUSEEVENT { unsigned fwKeys=0; };
namespace IDDrawSurface { void OutptTxt(const char*) {} }
std::map<std::string,std::vector<MultiBuildAssets::Button>> menus;
unsigned assetCalls=0;
namespace MultiBuildAssets {
 std::vector<Button> Menu(const TAdynmemStruct&,const UnitDefStruct& def) {++assetCalls;return menus[def.UnitName];}
}
namespace MultiBuildOrders {
 bool ready=true, issueResult=true, lastShift=false;
 unsigned issued=0,lastType=0,lastCount=0;
 void Install() {} bool Ready() {return ready;}
 bool Issue(TAdynmemStruct*,const std::vector<UnitStruct*>& units,unsigned type,bool shift) {
  ++issued;lastCount=units.size();lastType=type;lastShift=shift;return issueResult;
 }
}
'''
TESTS = r'''
struct Fixture {
 TAdynmemStruct ta;UnitDefStruct defs[10];UnitStruct units[8];PlayerInfo info;
 Fixture() {
  shared={};menus.clear();assetCalls=0;
  MultiBuildOrders::ready=true;MultiBuildOrders::issueResult=true;MultiBuildOrders::issued=0;
  ta.UnitDef=defs;ta.UNITINFOCount=10;ta.BeginUnitsArray_p=units;ta.EndOfUnitsArray_p=units+7;
  ta.Players[0].Units=units+1;ta.Players[0].UnitsAry_End=units+7;ta.Players[0].PlayerInfo=&info;
  const char* names[]={"","ARMCON","ARMACON","ARMSOLAR","ARMMEX","ARMMOHO","CORCON","CORSOLAR","ARMPLANE","DISABLED"};
  for(unsigned i=1;i<10;++i){std::strcpy(defs[i].UnitName,names[i]);std::strcpy(defs[i].Name,names[i]);defs[i].UnitTypeMask_0=0x00800000u;}
  for(unsigned i:{1u,2u,6u}){defs[i].bmcode=1;defs[i].UnitTypeMask_0|=builder;}
  defs[8].bmcode=1;defs[9].UnitTypeMask_0=0;
  menus["ARMCON"]={{"ARMSOLAR",65},{"ARMMEX",66},{"ARMSOLAR",67},{"DISABLED",0}};
  menus["ARMACON"]={{"ARMSOLAR",65},{"ARMMOHO",68},{"ARMPLANE",69}};
  menus["CORCON"]={{"CORSOLAR",65}};
  Select(1,1);Select(7,2);MultiBuild::Install();
 }
 void Select(unsigned slot,unsigned type){units[slot].UnitSelected=0x10000010u;units[slot].UnitID=type;units[slot].UnitType=&defs[type];units[slot].Owner_PlayerPtr0=&ta.Players[0];}
 std::vector<MultiBuildOption> Options(){return MultiBuild::Options(&ta);}
};
bool Has(const std::vector<MultiBuildOption>& options,unsigned type){for(const auto&o:options)if(o.type==type)return true;return false;}
int main(){
#if MULTI_BUILD_ENABLE
 Fixture f;std::vector<UnitStruct*> selected;
 assert(MultiBuild::Selection(&f.ta,selected)&&selected.size()==2&&selected.back()==f.units+7);
 auto options=f.Options();assert(options.size()==4&&Has(options,3)&&Has(options,4)&&Has(options,5)&&Has(options,8));
 assert(!Has(options,9)&&assetCalls==2);f.Options();assert(assetCalls==2);
 assert(!MultiBuild::CanBuild(&f.ta,f.units+1,5)&&MultiBuild::CanBuild(&f.ta,f.units+7,5));
 assert(MultiBuild::Choose(&f.ta,5)&&f.ta.BuildUnitID==5&&f.ta.PrepareOrder_Type==ordertype::BUILD);
 _MOUSEEVENT mouse;mouse.fwKeys=MK_SHIFT;
 assert(MultiBuild::TryIssue(&f.ta,&mouse)&&MultiBuildOrders::issued==1&&MultiBuildOrders::lastShift&&MultiBuildOrders::lastType==5&&MultiBuildOrders::lastCount==2);
 // A refused cooperative placement must not fall through to unfiltered native BUILD.
 MultiBuildOrders::issueResult=false;assert(MultiBuild::TryIssue(&f.ta,&mouse));
 assert(MultiBuild::Choose(&f.ta,8)); // Human menu mobile targets remain available.
 assert(!MultiBuild::Choose(&f.ta,9));
 // A stale selected free slot is ignored; a selected combat unit disables the union.
 f.units[4].UnitSelected=0x10000010u;assert(MultiBuild::Selection(&f.ta,selected));
 f.Select(4,3);assert(!MultiBuild::Selection(&f.ta,selected)&&selected.empty());
 f.Options();assert(f.ta.PrepareOrder_Type==ordertype::STOP);
 f.units[4].UnitSelected=0;f.units[7].UnitSelected=0;
 assert(!MultiBuild::Selection(&f.ta,selected));
 // Ordinary native single-constructor placement remains untouched after cancellation.
 f.ta.PrepareOrder_Type=ordertype::BUILD;assert(!MultiBuild::TryIssue(&f.ta,&mouse));
 f.Select(7,2);assert(MultiBuild::Choose(&f.ta,5));f.Select(4,3);
 assert(MultiBuild::TryIssue(&f.ta,&mouse)&&f.ta.PrepareOrder_Type==ordertype::STOP);
 f.units[4].UnitSelected=0;
 f.ta.Players[0].UnitsAry_End=f.units+8;assert(!MultiBuild::Selection(&f.ta,selected));f.ta.Players[0].UnitsAry_End=f.units+7;
 f.info.PropertyMask=WATCH;assert(!MultiBuild::Selection(&f.ta,selected));f.info.PropertyMask=0;
 shared.PlayingDemo=1;assert(!MultiBuild::Selection(&f.ta,selected));shared.PlayingDemo=0;
 f.units[7].Nanoframe=.5f;assert(!MultiBuild::Selection(&f.ta,selected));f.units[7].Nanoframe=0;
 f.units[7].Owner_PlayerPtr0=&f.ta.Players[1];assert(!MultiBuild::Selection(&f.ta,selected));f.units[7].Owner_PlayerPtr0=&f.ta.Players[0];
 // A second faction's visually similar building is a distinct real unit type.
 std::strcpy(f.defs[7].Name,"Solar Collector");std::strcpy(f.defs[3].Name,"Solar Collector");
 f.Select(7,6);options=f.Options();assert(Has(options,3)&&Has(options,7));
 // Definition/clock changes invalidate only owned menu metadata.
 menus["ARMCON"]={{"ARMMOHO",0}};f.ta.GameTime=1;
 options=f.Options();assert(Has(options,5)&&!Has(options,4));
 MultiBuildOrders::ready=false;MultiBuild::Install();assert(!MultiBuild::Selection(&f.ta,selected));
 std::cout<<"Combined construction model/selection/union/placement tests passed\n";
#else
 TAdynmemStruct ta;std::vector<UnitStruct*> units;_MOUSEEVENT mouse;
 MultiBuild::Install();assert(!MultiBuild::Selection(&ta,units)&&MultiBuild::Options(&ta).empty());
 assert(!MultiBuild::Choose(&ta,1)&&!MultiBuild::TryIssue(&ta,&mouse));
 assert(!MultiBuildOrders::issued);std::cout<<"Combined construction compiled-out model passed\n";
#endif
}
'''
with tempfile.TemporaryDirectory(prefix='multi-build-model-') as directory:
    directory=Path(directory)
    cpp=directory/'model.cpp';cpp.write_text(STUBS+'\n'+source+'\n'+TESTS)
    for enabled in (1,0):
        executable=directory/f'model-{enabled}'
        subprocess.run(shlex.split(os.environ.get('CXX','g++'))+[
            '-std=c++14','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-g',f'-DMULTI_BUILD_ENABLE={enabled}',
            '-I',str(DDRAW),str(cpp),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0'})
