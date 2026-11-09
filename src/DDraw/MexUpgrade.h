#pragma once

struct TAdynmemStruct;
struct _MOUSEEVENT;

namespace MexUpgrade
{
    bool Enabled();
    bool CheckingSite();
    void UpdateCursor(TAdynmemStruct* ta);
    bool TryCommand(TAdynmemStruct* ta, const _MOUSEEVENT* mouse);
}
