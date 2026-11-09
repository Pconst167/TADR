#pragma once

struct TAdynmemStruct;
struct _MOUSEEVENT;

namespace MexUpgrade
{
    bool Enabled();
    bool TryCommand(TAdynmemStruct* ta, const _MOUSEEVENT* mouse);
}
