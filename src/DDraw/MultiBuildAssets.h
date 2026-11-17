#pragma once

#include <string>
#include <vector>

struct TAdynmemStruct;
struct UnitDefStruct;

namespace MultiBuildAssets
{
    struct Button
    {
        std::string name;
        int quickkey = 0;
    };
    struct Picture
    {
        int width = 0, height = 0;
        std::vector<unsigned char> pixels;
    };

    // Copies owned metadata/pixels; callers never retain engine menu pointers.
    std::vector<Button> Menu(const TAdynmemStruct& ta, const UnitDefStruct& constructor);
    Picture BuildPicture(const char* name);
}
