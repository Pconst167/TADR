#pragma once

#include "SmallMapViewport.h"
#include <cstdint>
#include <cstddef>

namespace MultiBuildMenuGeometry
{
    using SmallMapViewport::Rect;
    inline bool Contains(Rect r, int x, int y)
    {
        return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
    }
    struct Layout
    {
        Rect bounds = {}, build = {}, orders = {}, grid = {}, previous = {}, next = {}, page = {}, caption = {};
        int columns = 0, rows = 0, tile = 0, capacity = 0;
    };
    inline Layout Arrange(int width, int height, int top, int commandsTop)
    {
        Layout result;
        if (width < 48 || width > 512 || height <= 0 || top < 0 || top >= height) return result;
        commandsTop = SmallMapViewport::Low(commandsTop, height);
        if (commandsTop - top < 90) return result;
        result.columns = width >= 96 ? 2 : 1;
        result.tile = SmallMapViewport::Low(SmallMapViewport::Low(64, width / result.columns), commandsTop - top - 62);
        result.rows = SmallMapViewport::Low(6, (commandsTop - top - 62) / result.tile);
        if (result.rows <= 0) return {};
        result.capacity = result.rows * result.columns;
        result.bounds = {0, top, width, commandsTop};
        result.build = {2, top + 2, width / 2 - 1, top + 21};
        result.orders = {width / 2 + 1, top + 2, width - 2, top + 21};
        result.grid = {(width - result.columns * result.tile) / 2, top + 22,
            (width + result.columns * result.tile) / 2, top + 22 + result.rows * result.tile};
        const int pagerY = result.grid.bottom + 2;
        result.previous = {3, pagerY, 27, pagerY + 19};
        result.next = {width - 27, pagerY, width - 3, pagerY + 19};
        result.page = {27, pagerY, width - 27, pagerY + 19};
        result.caption = {3, pagerY + 22, width - 3, commandsTop - 2};
        return result;
    }
    inline int Pages(std::size_t count, int capacity)
    {
        if (capacity <= 0 || count == 0) return 1;
        return static_cast<int>((count - 1) / static_cast<std::size_t>(capacity) + 1);
    }
    inline Rect Cell(const Layout& layout, int index)
    {
        if (index < 0 || index >= layout.capacity || layout.columns <= 0) return {};
        const int x = layout.grid.left + index % layout.columns * layout.tile;
        const int y = layout.grid.top + index / layout.columns * layout.tile;
        return {x, y, x + layout.tile, y + layout.tile};
    }
    enum class Kind { None, Build, Orders, Previous, Next, Item, Body };
    struct Hit
    {
        Kind kind = Kind::None;
        int index = -1;
        bool operator==(const Hit& other) const { return kind == other.kind && index == other.index; }
    };
    inline Hit HitTest(const Layout& layout, bool build, int page, std::size_t count, int x, int y)
    {
        if (Contains(layout.build, x, y)) return {Kind::Build, -1};
        if (Contains(layout.orders, x, y)) return {Kind::Orders, -1};
        if (!build || !Contains(layout.bounds, x, y)) return {};
        if (Contains(layout.previous, x, y)) return {Kind::Previous, -1};
        if (Contains(layout.next, x, y)) return {Kind::Next, -1};
        if (Contains(layout.grid, x, y) && layout.tile > 0)
        {
            const int cell = (y - layout.grid.top) / layout.tile * layout.columns + (x - layout.grid.left) / layout.tile;
            const std::int64_t option = static_cast<std::int64_t>(page) * layout.capacity + cell;
            if (page >= 0 && option >= 0 && static_cast<std::uint64_t>(option) < count)
                return {Kind::Item, static_cast<int>(option)};
        }
        return {Kind::Body, -1};
    }
    struct Surface
    {
        unsigned char* pixels;
        int width, height, pitch;
        Rect clip;
        const Rect* excluded = nullptr;
        std::size_t excludedCount = 0;
    };
    inline bool Valid(const Surface& s)
    {
        return s.pixels && s.width > 0 && s.height > 0 && s.pitch >= s.width;
    }
    inline bool Writable(const Surface& s, int x, int y)
    {
        if (x < 0 || y < 0 || x >= s.width || y >= s.height || !Contains(s.clip, x, y)) return false;
        for (std::size_t i = 0; i < s.excludedCount; ++i) if (Contains(s.excluded[i], x, y)) return false;
        return true;
    }
    inline void Pixel(const Surface& s, int x, int y, unsigned char color)
    {
        if (Writable(s, x, y)) s.pixels[static_cast<std::size_t>(y) * s.pitch + x] = color;
    }
    inline void Fill(const Surface& s, Rect r, unsigned char color)
    {
        if (!Valid(s)) return;
        r = SmallMapViewport::Intersect(SmallMapViewport::Intersect(r, s.clip), {0, 0, s.width, s.height});
        for (int y = r.top; y < r.bottom; ++y)
            for (int x = r.left; x < r.right; ++x) Pixel(s, x, y, color);
    }
    inline void Image(const Surface& s, Rect r, const unsigned char* pixels, int width, int height, std::size_t size)
    {
        if (!Valid(s) || !pixels || width <= 0 || height <= 0 || r.right <= r.left || r.bottom <= r.top
            || static_cast<std::uint64_t>(width) * height > size) return;
        const Rect clipped = SmallMapViewport::Intersect(SmallMapViewport::Intersect(r, s.clip), {0, 0, s.width, s.height});
        for (int y = clipped.top; y < clipped.bottom; ++y)
            for (int x = clipped.left; x < clipped.right; ++x)
            {
                const int sourceX = static_cast<int>((static_cast<std::int64_t>(x) - r.left) * width / (r.right - r.left));
                const int sourceY = static_cast<int>((static_cast<std::int64_t>(y) - r.top) * height / (r.bottom - r.top));
                Pixel(s, x, y, pixels[static_cast<std::size_t>(sourceY) * width + sourceX]);
            }
    }
    inline void Glyph(const Surface& s, int x, int y, const unsigned char* bits, int width, int height,
        unsigned char color, bool outline)
    {
        if (!Valid(s) || !bits || width <= 0 || width > 64 || height <= 0 || height > 32) return;
        for (int row = 0; row < height; ++row)
            for (int col = 0; col < width; ++col)
            {
                const int bit = row * width + col;
                if (!(bits[bit / 8] & (128 >> (bit % 8)))) continue;
                if (outline)
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) Pixel(s, x + col + dx, y + row + dy, 0);
                else Pixel(s, x + col, y + row, color);
            }
    }
}
