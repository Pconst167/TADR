#pragma once

#include <vector>

struct TAdynmemStruct;
struct UnitStruct;
struct _MOUSEEVENT;

struct MultiBuildOption
{
    unsigned type = 0;
    char name[32] = {};
    char label[32] = {};
    char description[64] = {};
    int quickkey = 0;
};

namespace MultiBuild
{
    void Install();
    // Immediate-use live pointers only; never retain these across callbacks.
    bool Selection(TAdynmemStruct* ta, std::vector<UnitStruct*>& constructors);
    bool CanBuild(TAdynmemStruct* ta, const UnitStruct* constructor, unsigned type);
    std::vector<MultiBuildOption> Options(TAdynmemStruct* ta);
    // Enter normal native placement mode without changing the selection.
    bool Choose(TAdynmemStruct* ta, unsigned type);
    // Called from the existing native placement hook. A true result suppresses
    // its unfiltered multi-selection dispatch, including refused placements.
    bool TryIssue(TAdynmemStruct* ta, const _MOUSEEVENT* mouse);
}
