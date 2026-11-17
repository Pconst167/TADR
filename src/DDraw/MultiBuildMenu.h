#pragma once

#include <windows.h>

struct TAdynmemStruct;

namespace MultiBuildMenu
{
    bool Message(HWND window, UINT message, WPARAM keys, LPARAM position);
    // Presentation only; the final indexed-color composite must be locked.
    void Draw(char* pixels, int width, int height, int pitch, TAdynmemStruct* ta);
}
