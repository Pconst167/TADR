#pragma once

#include <vector>

struct TAdynmemStruct;
struct UnitStruct;

namespace MultiBuildOrders
{
    void Install();
    bool Ready();

    // Called at the native mobile-build entry after placement validation. The
    // chosen type and position are the ordinary native placement cursor's.
    // True means handled, including the native Shift-click cancellation case.
    bool Issue(TAdynmemStruct* ta, const std::vector<UnitStruct*>& constructors,
        unsigned type, bool shift);
}
