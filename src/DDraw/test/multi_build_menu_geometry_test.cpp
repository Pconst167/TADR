#include "../MultiBuildMenuGeometry.h"
#include <cassert>
#include <iostream>
#include <vector>

using namespace MultiBuildMenuGeometry;

int main()
{
    const auto layout = Arrange(128, 768, 128, 514);
    assert(layout.columns == 2 && layout.rows == 5 && layout.tile == 64 && layout.capacity == 10);
    assert(layout.bounds.bottom == 514 && layout.caption.bottom < 514);
    assert(Cell(layout, 0).left == 0 && Cell(layout, 1).left == 64);
    assert(Cell(layout, 9).bottom == 470);
    assert(Pages(0, 10) == 1 && Pages(10, 10) == 1 && Pages(11, 10) == 2);
    assert(HitTest(layout, true, 1, 11, 20, 180).index == 10);
    assert(HitTest(layout, true, 1, 11, 80, 180).kind == Kind::Body);
    assert(HitTest(layout, false, 0, 20, 20, 180).kind == Kind::None);
    assert(HitTest(layout, false, 0, 20, 20, 137).kind == Kind::Build);
    assert(HitTest(layout, true, 0, 20, 80, 137).kind == Kind::Orders);
    assert(HitTest(layout, true, 0, 20, 128, 180).kind == Kind::None);
    assert(HitTest(layout, true, 0, 20, 20, 514).kind == Kind::None);
    const auto compact = Arrange(128, 480, 128, 226);
    assert(compact.capacity == 2 && compact.tile == 36);
    assert(compact.caption.bottom < 226);
    assert(Arrange(30, 768, 128, 514).capacity == 0);
    assert(Arrange(128, 768, 128, 160).capacity == 0);

    constexpr int width = 160, height = 768, pitch = 176, guard = 32;
    std::vector<unsigned char> bytes(guard + pitch * height + guard, 64);
    const Rect excluded = {8, 170, 36, 210};
    Surface surface = {bytes.data() + guard, width, height, pitch, layout.bounds, &excluded, 1};
    Fill(surface, {-50, -50, 800, 900}, 93);
    std::vector<unsigned char> picture(96 * 96, 208);
    Image(surface, {1, 151, 63, 213}, picture.data(), 96, 96, picture.size());
    const unsigned char glyph[] = {0xA0, 0xA0};
    Glyph(surface, 1, 130, glyph, 3, 3, 255, true);
    Glyph(surface, 1, 130, glyph, 3, 3, 255, false);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < pitch; ++x)
            if (x >= width || !Contains(layout.bounds, x, y) || Contains(excluded, x, y))
                assert(surface.pixels[y * pitch + x] == 64);
    assert(surface.pixels[160 * pitch + 5] == 208);
    assert(surface.pixels[220 * pitch + 5] == 93);
    for (int i = 0; i < guard; ++i)
        assert(bytes[i] == 64 && bytes[guard + pitch * height + i] == 64);
    const auto before = bytes;
    Image(surface, {1, 151, 63, 213}, picture.data(), 96, 96, 20);
    surface.pitch = -pitch;
    Fill(surface, layout.bounds, 255);
    Image(surface, {1, 151, 63, 213}, picture.data(), 96, 96, picture.size());
    Glyph(surface, 1, 130, glyph, 3, 3, 255, false);
    assert(bytes == before);
    std::cout << "Multi-build menu layout, pagination, image scaling and clipping passed\n";
}
