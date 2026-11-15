#pragma once

struct TAdynmemStruct;
struct _OFFSCREEN;

namespace MainMapRadar
{
    bool Enabled();
    // Engine render thread, after fog and before the HUD. Retains no world pointers.
    void Draw(TAdynmemStruct* ta, _OFFSCREEN* surface);
}
