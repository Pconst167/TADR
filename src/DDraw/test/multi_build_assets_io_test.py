#!/usr/bin/env python3
"""Exercise the production asset bridge against an owned, in-memory HPI VFS."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'MultiBuildAssets.cpp').read_text()
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include "'))
native_directory = 'reinterpret_cast<const char*>(0x505fe4)'
assert source.count(native_directory) == 1
source = source.replace(native_directory, 'testPictureDirectory')

bridge = r'''
#include "MultiBuildAssets.h"
#include "MultiBuildData.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>
struct UnitDefStruct { char UnitName[32] = {}; unsigned char GUINum = 0; unsigned UnitTypeMask_0 = 0; };
struct TAdynmemStruct { char desktopGUI[0x9b6+256] = {}; UnitDefStruct* UnitDef = nullptr; int GameTime = 0; };
const char* testPictureDirectory = "unitpicE";
std::map<std::string,std::vector<unsigned char>> files;
std::vector<std::string> stats, reads;
unsigned allocated = 0, freed = 0;
std::string Canonical(const char* name) {
    std::string result;
    for (; *name; ++name) {
        char c = *name == '/' ? '\\' : *name;
        if (c >= 'a' && c <= 'z') c -= 'a'-'A';
        result += c;
    }
    return result;
}
unsigned filelength_HPI(char* name) {
    const std::string key = Canonical(name); stats.push_back(key);
    const auto it = files.find(key); return it == files.end() ? 0 : static_cast<unsigned>(it->second.size());
}
char* readfile_HPI(char* name,unsigned* size) {
    const std::string key = Canonical(name); reads.push_back(key);
    const auto it = files.find(key);
    if (it == files.end()) { *size = 0; return nullptr; }
    *size = static_cast<unsigned>(it->second.size());
    char* bytes = static_cast<char*>(std::malloc(*size)); assert(bytes); ++allocated;
    std::memcpy(bytes,it->second.data(),*size); return bytes;
}
void freeTAMem(void* data) { assert(data); ++freed; std::free(data); }
void Put(const std::string& name,const std::string& bytes) {
    files[Canonical(name.c_str())] = std::vector<unsigned char>(bytes.begin(),bytes.end());
}
std::string MenuText(const char* name,int key = 0) {
    return std::string("[GADGET0]{[COMMON]{id=0;name=HEADER;active=1;}totalgadgets=1;}")
        + "[GADGET1]{[COMMON]{id=1;name=" + name + ";attribs=32;active=1;}quickkey=" + std::to_string(key) + ";text=;}";
}
void Reset(TAdynmemStruct& ta,UnitDefStruct& def) {
    std::strcpy(ta.desktopGUI+0x9b6,"guiE\\"); ta.UnitDef=&def; ta.GameTime=10;
    std::strcpy(def.UnitName,"ARMCON"); def.GUINum=3; def.UnitTypeMask_0=0;
}
'''

tests = r'''
int main() {
    TAdynmemStruct ta; UnitDefStruct def; Reset(ta,def);
    Put("guiE/ARMCON0.GUI",MenuText("PAGEZERO",3));
    Put("guiE/ARMCON1.GUI",MenuText("ARMFUS",121));
    Put("guiE/ARMCON2.GUI",MenuText("ARMMOHO",113)+MenuText("ARMFUS",999));
#if MULTI_BUILD_ENABLE
    auto menu=MultiBuildAssets::Menu(ta,def);
    assert(menu.size()==2 && menu[0].name=="ARMFUS" && menu[0].quickkey==121 && menu[1].name=="ARMMOHO");
    assert(reads.size()==2 && reads[0]=="GUIE\\ARMCON1.GUI" && reads[1]=="GUIE\\ARMCON2.GUI");
    const auto readCount=reads.size(); menu[0].name="MUTATED";
    assert(MultiBuildAssets::Menu(ta,def)[0].name=="ARMFUS" && reads.size()==readCount);
    def.UnitTypeMask_0=0x80000000u;
    menu=MultiBuildAssets::Menu(ta,def); assert(menu.size()==3 && menu[0].name=="PAGEZERO");
    def.GUINum=1; menu=MultiBuildAssets::Menu(ta,def); assert(menu.size()==1 && menu[0].name=="PAGEZERO");
    def.UnitTypeMask_0=0; assert(MultiBuildAssets::Menu(ta,def).empty());
    def.GUINum=2; Put("guiE/ARMCON1.GUI",MenuText("CHANGED"));
    ta.GameTime=1; menu=MultiBuildAssets::Menu(ta,def); assert(menu.size()==1 && menu[0].name=="CHANGED");
    Put("guiE/ARMCON1.GUI",MenuText("NEXTWORLD"));
    TAdynmemStruct next; Reset(next,def);
    menu=MultiBuildAssets::Menu(next,def); assert(menu[0].name=="NEXTWORLD");
    Put("guiE/ARMCON1.GUI",MenuText("NEXTDEF"));
    UnitDefStruct another=def; next.UnitDef=&another; menu=MultiBuildAssets::Menu(next,another);
    assert(menu[0].name=="NEXTDEF");
    std::strcpy(next.desktopGUI+0x9b6,"othergui");
    Put("othergui/ARMCON1.GUI",MenuText("DIRECTORY"));
    menu=MultiBuildAssets::Menu(next,another); assert(menu.size()==1 && menu[0].name=="DIRECTORY");
    std::memset(next.desktopGUI+0x9b6,'a',256); assert(MultiBuildAssets::Menu(next,another).empty());
    std::strcpy(next.desktopGUI+0x9b6,"hugegui");
    Put("hugegui/ARMCON1.GUI",std::string(1024*1024+1,' '));
    const auto beforeHuge=reads.size(); assert(MultiBuildAssets::Menu(next,another).empty());
    assert(reads.size()==beforeHuge);
    std::vector<unsigned char> pcx(128,0);
    pcx[0]=10;pcx[1]=5;pcx[2]=1;pcx[3]=8;pcx[65]=1;pcx[66]=1;pcx.push_back(77);
    files["UNITPICE\\ARMFUS.PCX"]=pcx;
    auto picture=MultiBuildAssets::BuildPicture("armfus");
    assert(picture.width==1 && picture.height==1 && picture.pixels.size()==1 && picture.pixels[0]==77);
    const auto picturesRead=reads.size(); picture.pixels[0]=12;
    assert(MultiBuildAssets::BuildPicture("ARMFUS").pixels[0]==77 && reads.size()==picturesRead);
    const auto beforeInvalid=stats.size();
    assert(MultiBuildAssets::BuildPicture("../armsolar").pixels.empty());
    assert(MultiBuildAssets::BuildPicture("abcdefghijklmnopqrstuvwxyz1234567").pixels.empty());
    assert(MultiBuildAssets::BuildPicture(nullptr).pixels.empty()); assert(stats.size()==beforeInvalid);
    assert(allocated==freed);
#else
    assert(MultiBuildAssets::Menu(ta,def).empty());
    assert(MultiBuildAssets::BuildPicture("ARMFUS").pixels.empty());
    assert(stats.empty() && reads.empty() && allocated==0 && freed==0);
#endif
    std::cout<<"Multi-build asset bridge tests passed enable="<<MULTI_BUILD_ENABLE<<"\n";
}
'''

with tempfile.TemporaryDirectory(prefix='tadr-multi-build-assets-') as temporary:
    temporary = Path(temporary)
    path = temporary / 'test.cpp'
    path.write_text(bridge + '\n' + source + '\n' + tests)
    for enabled in (0, 1):
        binary = temporary / ('assets-' + str(enabled))
        subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                        '-DMULTI_BUILD_ENABLE=' + str(enabled), '-I', str(ROOT), str(path),
                        '-o', str(binary)], check=True)
        environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
        subprocess.run([str(binary)], env=environment, check=True)
