#!/usr/bin/env python3
"""Run the actual cooperative-build controller with mocked native order APIs.

The native linked lists, allocator reuse, destruction callbacks, queue toggles,
rotation tags and unit identities are exercised without modifying a game.
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
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#define __stdcall
#define __thiscall
using DWORD=std::uintptr_t;
using BYTE=unsigned char;
using LPVOID=void*;
constexpr int TAInGame=1, Player_LocalHuman=1;
constexpr unsigned UnitSelected_State=0x10, builder=0x40, canfly=0x800;
constexpr int INLINE_5BYTESLAGGERJMP=0x1002;
constexpr int X86STRACKBUFFERCHANGE=0x7798FFAA;
struct _Position_Dword { unsigned short x_=0,X=0,z_=0,Z=0,y_=0,Y=0; };
using Position_Dword=_Position_Dword;
struct UnitStruct;
struct UnitDefStruct { unsigned UnitTypeMask_0=builder; int FootX=2,FootY=2,bmcode=1; };
struct PlayerStruct {
    int PlayerActive=1,My_PlayerType=Player_LocalHuman;
    UnitStruct* Units=nullptr;UnitStruct* UnitsAry_End=nullptr;
};
struct UnitOrdersStruct {
    unsigned char COBHandler_index=0,State=0;
    UnitStruct* Unit_ptr=nullptr;UnitStruct* AttackTargat=nullptr;
    unsigned StartTime=0,BuildUnitID=0,unknow_0=0,unknow_1=0;
    Position_Dword Pos;
    UnitOrdersStruct* NextOrder=nullptr;
};
struct UnitStruct {
    int IsUnit=1;short UnitID=1,UnitInGameIndex=0;
    UnitDefStruct* UnitType=nullptr;
    PlayerStruct* Owner_PlayerPtr0=nullptr;
    void* Object3do=nullptr;
    unsigned UnitSelected=0x10000010;float Nanoframe=0;
    UnitOrdersStruct* UnitOrders=nullptr;
};
struct TAdynmemStruct {
    PlayerStruct Players[10];int LocalHumanPlayer_PlayerID=0,GameTime=100;
    UnitStruct* BeginUnitsArray_p=nullptr;UnitStruct* EndOfUnitsArray_p=nullptr;
    UnitDefStruct* UnitDef=nullptr;unsigned UNITINFOCount=16;
    unsigned CircleSelect_Pos1TAz=37;
    Position_Dword MouseMapPos;
};
struct Shared { int TAProgress=TAInGame;bool PlayingDemo=false; } shared;
Shared* DataShare=&shared;
TAdynmemStruct* activeWorld=nullptr;
TAdynmemStruct** TAmainStruct_PtrPtr=&activeWorld;
struct InlineX86StackBuffer { DWORD Esp=0,Eax=0,Ecx=0;LPVOID rtnAddr_Pvoid=nullptr; };
using PInlineX86StackBuffer=InlineX86StackBuffer*;
using Callback=int(*)(PInlineX86StackBuffer);
std::map<unsigned,Callback> hooks;
struct InlineSingleHook {
    InlineSingleHook(unsigned addr,unsigned length,int mode,Callback callback) {
        assert(mode==INLINE_5BYTESLAGGERJMP);
        assert((addr==0x406300 && length==8)||(addr==0x40fbe0 && length==8)||(addr==0x43a1f0 && length==5));
        hooks[addr]=callback;
    }
};
struct IDDrawSurface { static void OutptTxt(const char*) {} };
std::map<const UnitStruct*,std::set<unsigned>> capabilities;
namespace MultiBuild {
    bool CanBuild(TAdynmemStruct*,const UnitStruct* unit,unsigned type) { return capabilities[unit].count(type)!=0; }
}
struct CUnitRotate {
    int rotation=1;
    std::map<void*,int> tags;
    static CUnitRotate* GetInstance() { static CUnitRotate instance;return &instance; }
    int GetRotation() const {return rotation;}
    bool IsRotationAllowed(unsigned type,int) const {return type!=9;}
    void TagOrderRotation(void* order,int value) {tags[order]=value;}
};
bool byteGuard=true;
bool NativeCheckBytes(unsigned,const unsigned char*,size_t) {return byteGuard;}
void NativeClick(unsigned,unsigned,UnitStruct*,UnitStruct*,Position_Dword*,unsigned,unsigned);
void NativeQueued(unsigned,unsigned,UnitStruct*,UnitStruct*,Position_Dword*,unsigned,unsigned);
char* NativeFind(unsigned char*,const char*);
void NativeDelay(UnitOrdersStruct* order,unsigned ticks) {
    assert(ticks==1);order->unknow_1|=1;order->unknow_0=activeWorld->GameTime+ticks;
}

// PRODUCTION_SOURCE

constexpr unsigned GroundBuild=10,AirBuild=11,GroundGuard=12,AirGuard=13,OtherOrder=20,HelpOrder=21;
unsigned serial=1;
struct NativeEvent {UnitStruct* unit;unsigned action;bool shift;};
std::vector<NativeEvent> events;
char* NativeFind(unsigned char* result,const char* name) {
    const std::string key(name);
    *result=key=="MobileBuild"?GroundBuild:key=="VTOL_MobileBuild"?AirBuild:
        key=="Follow_Ground"?GroundGuard:key=="VTOL_Follow"?AirGuard:0;
    return reinterpret_cast<char*>(result);
}
void NotifyDestroy(UnitOrdersStruct* order) {
    if(hooks.count(0x43a1f0)) {InlineX86StackBuffer frame;frame.Ecx=reinterpret_cast<DWORD>(order);hooks[0x43a1f0](&frame);}
    CUnitRotate::GetInstance()->tags.erase(order);
}
void RemoveNode(UnitStruct& unit,UnitOrdersStruct* order) {
    auto** link=&unit.UnitOrders;
    while(*link && *link!=order)link=&(*link)->NextOrder;
    assert(*link==order);*link=order->NextOrder;
    NotifyDestroy(order);delete order;
}
void ClearOrders(UnitStruct& unit) {while(unit.UnitOrders)RemoveNode(unit,unit.UnitOrders);}
void Add(unsigned action,unsigned shift,UnitStruct* unit,UnitStruct* target,
         Position_Dword* position,unsigned type,bool toggle) {
    events.push_back({unit,action,shift!=0});
    if(toggle && shift)for(auto* order=unit->UnitOrders;order;order=order->NextOrder) {
        if(order->COBHandler_index==action && (!target || order->AttackTargat==target)
            && std::abs(int(order->Pos.X)-position->X)<=16 && std::abs(int(order->Pos.Y)-position->Y)<=16) {
            RemoveNode(*unit,order);return;
        }
    }
    // Native allocates the replacement BEFORE clearing old nodes.
    auto* order=new UnitOrdersStruct;
    order->COBHandler_index=action;order->Unit_ptr=unit;order->AttackTargat=target;
    order->Pos=*position;order->BuildUnitID=type;order->StartTime=serial++;
    if(!shift)ClearOrders(*unit);
    auto** link=&unit->UnitOrders;while(*link)link=&(*link)->NextOrder;*link=order;
}
void NativeClick(unsigned action,unsigned shift,UnitStruct* unit,UnitStruct* target,
                 Position_Dword* position,unsigned type,unsigned) {Add(action,shift,unit,target,position,type,true);}
void NativeQueued(unsigned action,unsigned shift,UnitStruct* unit,UnitStruct* target,
                  Position_Dword* position,unsigned type,unsigned) {Add(action,shift,unit,target,position,type,false);}
struct World {
    TAdynmemStruct ta;UnitStruct units[8];UnitDefStruct definitions[16];
    World() {
        activeWorld=&ta;shared=Shared{};DataShare=&shared;capabilities.clear();events.clear();
        ta.BeginUnitsArray_p=units;ta.EndOfUnitsArray_p=units+7;ta.UnitDef=definitions;
        ta.Players[0].Units=units;ta.Players[0].UnitsAry_End=units+7;
        for(int i=0;i<8;++i) {
            units[i].UnitType=&definitions[i+1];units[i].UnitID=i+1;units[i].UnitInGameIndex=i;
            units[i].Owner_PlayerPtr0=&ta.Players[0];units[i].Object3do=&units[i];units[i].IsUnit=i+100;
            capabilities[units+i].insert(8);capabilities[units+i].insert(9);
        }
        definitions[8].bmcode=definitions[9].bmcode=0;
        ta.MouseMapPos.X=120;ta.MouseMapPos.Y=248;ta.MouseMapPos.Z=99;
#if MULTI_BUILD_ENABLE
        // The real context observes a game transition, not test-private resets.
        shared.TAProgress=0;MultiBuildOrders::Issue(&ta,{units,units+1},8,false);
        shared.TAProgress=TAInGame;
#endif
    }
    ~World() {for(auto& unit:units)ClearOrders(unit);activeWorld=nullptr;}
    bool Issue(bool shift=false,unsigned type=8,std::vector<UnitStruct*> selection={}) {
        if(selection.empty())selection={units,units+1};
        return MultiBuildOrders::Issue(&ta,selection,type,shift);
    }
    int Result(int slot,UnitOrdersStruct* order=nullptr,unsigned flags=0) {
#if MULTI_BUILD_ENABLE
        return HelperResult(&ta,units+slot,order?order:units[slot].UnitOrders,flags);
#else
        (void)slot;(void)order;(void)flags;return -1;
#endif
    }
};
unsigned Count(const UnitStruct& unit) {unsigned count=0;for(auto* o=unit.UnitOrders;o;o=o->NextOrder)++count;return count;}
UnitOrdersStruct* Last(UnitStruct& unit) {auto* order=unit.UnitOrders;while(order&&order->NextOrder)order=order->NextOrder;return order;}
void Extra(UnitStruct& unit,unsigned action=OtherOrder) {
    Position_Dword position;NativeQueued(action,1,&unit,nullptr,&position,0,0);
}
#if MULTI_BUILD_ENABLE
void TestInstall() {
    byteGuard=false;MultiBuildOrders::Install();assert(!MultiBuildOrders::Ready()&&hooks.empty());
    byteGuard=true;MultiBuildOrders::Install();MultiBuildOrders::Install();assert(MultiBuildOrders::Ready()&&hooks.size()==3);
}
void TestMixedOneLeaderAndFacing() {
    World world;capabilities[world.units].clear();world.definitions[2].UnitTypeMask_0|=canfly;
    assert(world.Issue());
    assert(world.units[1].UnitOrders->COBHandler_index==AirBuild);
    assert(world.units[0].UnitOrders->COBHandler_index==GroundGuard);
    assert(world.units[0].UnitOrders->AttackTargat==world.units+1);
    assert(Count(world.units[0])==1 && Count(world.units[1])==1);
    for(const auto& event:events)if(event.action==GroundBuild||event.action==AirBuild)assert(event.unit==world.units+1);
    const auto* build=world.units[1].UnitOrders;
    assert(build->BuildUnitID==8 && build->Pos.X==128 && build->Pos.Y==256 && build->Pos.Z==37);
    assert(CUnitRotate::GetInstance()->tags[const_cast<UnitOrdersStruct*>(build)]==1);
    assert(world.Result(0)==-1);
}
void TestGroundPreferenceAndAirHelper() {
    World world;world.definitions[1].UnitTypeMask_0|=canfly;
    assert(world.Issue(false,8,{world.units+1,world.units,world.units}));
    assert(world.units[1].UnitOrders->COBHandler_index==GroundBuild);
    assert(world.units[0].UnitOrders->COBHandler_index==AirGuard);
    // Some constructors/carriers can build mobile targets; the menu union
    // must not silently discard these legitimate native MobileBuild choices.
    world.definitions[8].bmcode=1;
    assert(world.Issue(false));
    assert(world.units[1].UnitOrders->COBHandler_index==GroundBuild);
    assert(world.units[1].UnitOrders->BuildUnitID==8 && world.Result(0)==-1);
}
void TestInclusiveLastUnitAndSelectionGuards() {
    World world;capabilities[world.units+1].clear();
    assert(world.Issue(false,9,{world.units+1,world.units+7}));
    assert(world.units[7].UnitOrders->COBHandler_index==GroundBuild);
    assert(world.units[1].UnitOrders->AttackTargat==world.units+7);
    assert(world.Result(1)==-1);
    ClearOrders(world.units[1]);ClearOrders(world.units[7]);events.clear();
    auto* outside=reinterpret_cast<UnitStruct*>(reinterpret_cast<std::uintptr_t>(world.units)+sizeof(world.units));
    assert(world.Issue(false,9,{world.units+1,outside})&&events.empty());
    world.units[7].UnitSelected&=~UnitSelected_State;
    assert(world.Issue(false,9,{world.units+1,world.units+7})&&events.empty());
}
void TestShiftQueueAndTemporaryCompletion() {
    World world;Extra(world.units[0]);
    auto* earlier=world.units[0].UnitOrders;
    assert(world.Issue(true));auto* build=Last(world.units[0]);auto* guard=world.units[1].UnitOrders;
    assert(world.units[0].UnitOrders==earlier && world.Result(1)==2);
    assert((guard->unknow_1&1) && guard->unknow_0==unsigned(world.ta.GameTime+1));
    Extra(world.units[1]);auto* later=Last(world.units[1]);
    RemoveNode(world.units[0],earlier);assert(world.Result(1)==-1);
    // Vanilla guard temporarily pushes HelpBuild ahead, retaining the guard.
    auto* help=new UnitOrdersStruct;help->COBHandler_index=HelpOrder;help->Unit_ptr=world.units+1;
    help->NextOrder=guard;world.units[1].UnitOrders=help;
    RemoveNode(world.units[0],build);
    RemoveNode(world.units[1],help);
    assert(world.Result(1)==5);RemoveNode(world.units[1],guard);
    assert(world.units[1].UnitOrders==later && Count(world.units[1])==1);
}
void TestGridDoesNotToggleHelpers() {
    World world;
    for(int i=0;i<5;++i) {world.ta.MouseMapPos.X=120+i*16;assert(world.Issue(true));}
    // Native building clicks use a 16-unit proximity toggle; use spaced sites
    // to isolate guard-target coalescing from ordinary native build toggles.
    // Every surviving build has its own helper, even with the SAME leader.
    assert(Count(world.units[1])>=Count(world.units[0]));
    while(world.units[0].UnitOrders)RemoveNode(world.units[0],world.units[0].UnitOrders);
    while(world.units[1].UnitOrders) {assert(world.Result(1)==5);RemoveNode(world.units[1],world.units[1].UnitOrders);}
    for(int i=0;i<5;++i) {world.ta.MouseMapPos.X=120+i*48;assert(world.Issue(true));}
    assert(Count(world.units[0])==5 && Count(world.units[1])==5);
    auto* guard=world.units[1].UnitOrders;auto* build=world.units[0].UnitOrders;
    while(build) {assert(guard&&guard->AttackTargat==world.units);guard=guard->NextOrder;build=build->NextOrder;}
}
void TestNativeToggleAndReplacement() {
    World world;assert(world.Issue(true));auto* guard=world.units[1].UnitOrders;
    assert(world.Issue(true));assert(!world.units[0].UnitOrders && world.Result(1,guard)==5);
    assert(world.Issue(false));assert(Count(world.units[0])==1&&Count(world.units[1])==1);
    assert(world.Result(1)==-1);
    assert(world.Issue(false,9));assert(world.units[0].UnitOrders->BuildUnitID==9);
    assert(CUnitRotate::GetInstance()->tags[world.units[0].UnitOrders]==0);
    assert(Count(world.units[1])==1&&world.Result(1)==-1);
}
void TestCancellationCaptureDeathAndReuse() {
    for(int reason=0;reason<6;++reason) {
        World world;assert(world.Issue());auto* guard=world.units[1].UnitOrders;
        if(reason==0)ClearOrders(world.units[0]);
        if(reason==1)world.units[0].UnitSelected|=0x4000;
        if(reason==2)world.units[0].Owner_PlayerPtr0=&world.ta.Players[1];
        if(reason==3)world.units[0].IsUnit+=100;
        if(reason==4)world.units[0].Object3do=&world.ta;
        if(reason==5)world.units[0].UnitType=&world.definitions[5];
        assert(world.Result(1,guard)==5);
    }
    World world;assert(world.Issue());auto* guard=world.units[1].UnitOrders;
    world.units[1].Owner_PlayerPtr0=&world.ta.Players[1];assert(world.Result(1,guard)==5);
    // Destroy notification removes identities BEFORE allocator pointer reuse.
    auto* build=world.units[0].UnitOrders;NotifyDestroy(build);
    build->StartTime=serial++;assert(world.Result(1,guard)==-1); // Removed invalid helper is no longer tracked.
}
void TestResetAndEligibility() {
    World world;assert(world.Issue(false,8,{world.units,world.units+1,world.units+2}));
    auto* guard=world.units[1].UnitOrders;
    world.ta.GameTime=0;assert(world.Result(1,guard)==5 && world.Result(2)==5);
    assert(world.Issue(false));
    shared.TAProgress=0;assert(world.Result(1)==5);
    shared.TAProgress=TAInGame;ClearOrders(world.units[0]);ClearOrders(world.units[1]);
    capabilities[world.units].clear();capabilities[world.units+1].clear();events.clear();
    assert(world.Issue()&&events.empty());
    capabilities[world.units].insert(8);world.units[1].Owner_PlayerPtr0=&world.ta.Players[1];
    assert(world.Issue()&&events.empty());
}
void TestHookCallingConvention() {
    World world;Extra(world.units[0]);world.Issue(true);
    DWORD frame[4]={0x12345678,reinterpret_cast<DWORD>(world.units+1),reinterpret_cast<DWORD>(world.units[1].UnitOrders),0};
    InlineX86StackBuffer regs;regs.Esp=reinterpret_cast<DWORD>(frame);
    const auto originalEsp=regs.Esp;
    assert(hooks[0x406300](&regs)==X86STRACKBUFFERCHANGE && regs.Eax==2);
    assert(regs.Esp==originalEsp+16 && regs.rtnAddr_Pvoid==reinterpret_cast<void*>(frame[0]));
    ClearOrders(world.units[0]);regs.Esp=originalEsp;
    assert(hooks[0x40fbe0](&regs)==X86STRACKBUFFERCHANGE && regs.Eax==5);
    regs.Esp=originalEsp;frame[3]=2;
    assert(hooks[0x406300](&regs)==0); // Never intercept native teardown callbacks.
}
#endif
int main() {
#if MULTI_BUILD_ENABLE
    TestInstall();TestMixedOneLeaderAndFacing();TestGroundPreferenceAndAirHelper();
    TestInclusiveLastUnitAndSelectionGuards();
    TestShiftQueueAndTemporaryCompletion();TestGridDoesNotToggleHelpers();
    TestNativeToggleAndReplacement();TestCancellationCaptureDeathAndReuse();
    TestResetAndEligibility();TestHookCallingConvention();
    std::cout<<"Multi-build production order controller tests passed\n";
#else
    MultiBuildOrders::Install();World world;
    assert(!MultiBuildOrders::Ready()&&!world.Issue()&&events.empty()&&hooks.empty());
    std::cout<<"Multi-build compiled-out order controller tests passed\n";
#endif
}
'''


def main():
    source = (DDRAW / "MultiBuildOrders.cpp").read_text()
    source = re.sub(r'^\s*#include[^\n]*$', '', source, flags=re.M)
    for address, mock in (("0x0043AFC0u", "NativeClick"), ("0x0043ADC0u", "NativeQueued"),
                          ("0x00438760u", "NativeFind"), ("0x00439E80u", "NativeDelay")):
        source, count = re.subn(r'reinterpret_cast<\w+>\(' + address + r'\)', mock, source)
        assert count == 1, (address, count)
    source = source.replace('return std::memcmp(reinterpret_cast<const void*>(address), expected, length) == 0;',
                            'return NativeCheckBytes(address, expected, length);')
    assert 'return NativeCheckBytes(address, expected, length);' in source
    harness = HARNESS.replace('// PRODUCTION_SOURCE', source)
    env = os.environ.copy()
    env['ASAN_OPTIONS'] = env.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    with tempfile.TemporaryDirectory(prefix='multi-build-controller-') as temp:
        cpp = Path(temp) / 'controller.cpp'
        cpp.write_text(harness)
        for enabled in (1, 0):
            binary = Path(temp) / ('controller' + str(enabled))
            subprocess.run(shlex.split(os.environ.get('CXX', 'g++')) + [
                '-std=c++11', '-Wall', '-Wextra', '-Werror', '-g', '-pthread',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                '-DMULTI_BUILD_ENABLE=' + str(enabled), str(cpp), '-o', str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, env=env, timeout=15)


if __name__ == '__main__':
    main()
