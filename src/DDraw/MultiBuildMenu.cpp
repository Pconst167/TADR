#include "config.h"
#include "MultiBuildMenu.h"

#if MULTI_BUILD_ENABLE

#include "oddraw.h"
#include "iddrawsurface.h"
#include "tamem.h"
#include "tafunctions.h"
#include "Gaf.h"
#include "dialog.h"
#include "VoteDialog.h"
#include "cincome.h"
#include "MultiBuild.h"
#include "MultiBuildAssets.h"
#include "MultiBuildMenuGeometry.h"
#include "font.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    namespace Geometry = MultiBuildMenuGeometry;
    using Geometry::Rect;
    using Geometry::Kind;
    std::recursive_mutex menuMutex;
    struct State
    {
        TAdynmemStruct* world = nullptr;
        int localPlayer = -1;
        unsigned tick = 0;
        bool active = false, build = true;
        int page = 0, wheel = 0;
        Geometry::Hit pressed;
        std::vector<unsigned> selection;
        std::vector<MultiBuildOption> options;
        std::vector<MultiBuildAssets::Picture> pictures;
        std::vector<bool> loaded;
    } state;
    bool suppressLeftUp = false;

    void DirtyNative(TAdynmemStruct* ta)
    {
        if (!ta) return;
        int hops = 0;
        for (auto* gui = ta->desktopGUI.TheActive_GUIMEM; gui && hops < 32; gui = gui->per_active, ++hops)
            gui->Active_b = 1;
    }

    bool Context(TAdynmemStruct* ta)
    {
        std::vector<UnitStruct*> constructors;
        if (!ta || !DataShare || DataShare->TAProgress != TAInGame || !MultiBuild::Selection(ta, constructors))
        {
            if (state.active) DirtyNative(ta);
            state = State();
            return false;
        }
        std::vector<unsigned> selection;
        selection.reserve(constructors.size());
        for (const auto* unit : constructors)
            selection.push_back((static_cast<unsigned>(static_cast<unsigned short>(unit->UnitInGameIndex)) << 16)
                | static_cast<unsigned short>(unit->UnitID));
        std::sort(selection.begin(), selection.end());
        if (ta != state.world || state.localPlayer != ta->LocalHumanPlayer_PlayerID
            || ta->GameTime < state.tick || selection != state.selection)
        {
            DirtyNative(ta);
            state = State();
            state.world = ta;
            state.localPlayer = ta->LocalHumanPlayer_PlayerID;
            state.selection = std::move(selection);
            state.options = MultiBuild::Options(ta);
            state.pictures.resize(state.options.size());
            state.loaded.resize(state.options.size(), false);
            state.active = !state.options.empty();
        }
        state.tick = ta->GameTime;
        return state.active;
    }

    bool Command(const char (&name)[16])
    {
        char normalized[17] = {};
        for (int i = 0; i < 16 && name[i]; ++i)
            normalized[i] = name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i];
        const std::size_t length = std::strlen(normalized);
        const char* commands[] = {"ONOFF", "CLOAK", "FIREORD", "MOVEORD", "RECLAIM", "REPAIR",
            "CAPTURE", "MOVE", "STOP", "PATROL", "DEFEND", "ATTACK", "LOAD", "UNLOAD"};
        for (const char* command : commands)
        {
            const std::size_t suffix = std::strlen(command);
            if (length >= suffix && std::strcmp(normalized + length - suffix, command) == 0) return true;
        }
        return false;
    }

    struct PanelInfo
    {
        Geometry::Layout layout;
        std::vector<Rect> excluded;
    };

    PanelInfo InspectPanels(TAdynmemStruct* ta)
    {
        PanelInfo result;
        const int sidebar = ta->GameSreen_Rect.left;
        const int height = static_cast<int>(ta->ScreenHeight);
        int top = 128, commandsTop = height - 254, hops = 0;
        for (auto* gui = ta->desktopGUI.TheActive_GUIMEM; gui && hops < 32; gui = gui->per_active, ++hops)
        {
            if (!gui->ControlsAry) continue;
            const auto* controls = gui->ControlsAry;
            const auto& panel = controls[0];
            const Rect rect = {panel.xpos, panel.ypos, panel.xpos + panel.width, panel.ypos + panel.height};
            const int count = panel.totalgadgets;
            if (rect.left >= 0 && rect.right <= sidebar + 1 && panel.height >= 128
                && panel.ypos >= 64 && count > 0 && count <= 256)
            {
                int firstCommand = height, matches = 0;
                for (int i = 1; i <= count; ++i)
                    if (controls[i].width > 0 && controls[i].height > 0 && Command(controls[i].name))
                    {
                        ++matches;
                        firstCommand = (std::min)(firstCommand, panel.ypos + controls[i].ypos);
                    }
                if (matches >= 2)
                {
                    top = (std::max)(128, static_cast<int>(panel.ypos));
                    commandsTop = firstCommand;
                    break;
                }
            }
            // Panels above the ordinary sidebar keep their native pixels and
            // input. Do not draw the new menu over chat or an options popup.
            if (gui->per_active && panel.width > 0 && panel.height > 0) result.excluded.push_back(rect);
        }
        result.layout = Geometry::Arrange(sidebar, height, top, commandsTop);
        return result;
    }

    bool Obscured(const PanelInfo& panels, int x, int y)
    {
        for (const auto& rect : panels.excluded) if (Geometry::Contains(rect, x, y)) return true;
        RECT rect = {};
        auto* dialog = LocalShare ? static_cast<Dialog*>(LocalShare->Dialog) : nullptr;
        if (dialog && dialog->IsShow(&rect) && x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom)
            return true;
        if (g_VoteDialog && g_VoteDialog->IsVisible() && g_VoteDialog->lpDialogSurf)
        {
            DDSURFACEDESC desc = {};
            desc.dwSize = sizeof(desc);
            if (g_VoteDialog->lpDialogSurf->GetSurfaceDesc(&desc) == DD_OK
                && x >= g_VoteDialog->posX && y >= g_VoteDialog->posY
                && static_cast<unsigned>(x - g_VoteDialog->posX) < desc.dwWidth
                && static_cast<unsigned>(y - g_VoteDialog->posY) < desc.dwHeight) return true;
        }
        auto* income = LocalShare ? static_cast<CIncome*>(LocalShare->Income) : nullptr;
        return income && income->IsPointOver(x, y);
    }

    bool CurrentMouse(POINT& point)
    {
        const auto* program = *TAProgramStruct_PtrPtr;
        return program && program->TAClass_Hwnd && GetForegroundWindow() == program->TAClass_Hwnd
            && GetCursorPos(&point) && ScreenToClient(program->TAClass_Hwnd, &point);
    }

    void DrawMenuBorder(const Geometry::Surface& surface, Rect rect, unsigned char top, unsigned char bottom)
    {
        Geometry::Fill(surface, {rect.left, rect.top, rect.right, rect.top + 1}, top);
        Geometry::Fill(surface, {rect.left, rect.top, rect.left + 1, rect.bottom}, top);
        Geometry::Fill(surface, {rect.left, rect.bottom - 1, rect.right, rect.bottom}, bottom);
        Geometry::Fill(surface, {rect.right - 1, rect.top, rect.right, rect.bottom}, bottom);
    }

    struct Font { const unsigned char* data; int height; };
    Font MenuFont(const TAdynmemStruct& ta)
    {
        const auto* font = static_cast<const unsigned char*>(ta.smalFontHandle);
        return font && font[0] > 0 && font[0] <= 32 ? Font{font, font[0]} : Font{nullptr, 8};
    }
    const unsigned char* Glyph(const Font& font, unsigned char code, int& width)
    {
        width = 8;
        if (!font.data) return reinterpret_cast<const unsigned char*>(ThinFont) + code * 8;
        if (code < font.data[3]) { width = 0; return nullptr; }
        const unsigned index = 4 + (code - font.data[3]) * 2;
        const unsigned offset = font.data[index] | (font.data[index + 1] << 8);
        if (!offset || font.data[offset] > 64) { width = 0; return nullptr; }
        width = font.data[offset];
        return font.data + offset + 1;
    }
    void DrawMenuText(Geometry::Surface surface, const Font& font, Rect bounds, const char* text, unsigned char color)
    {
        if (!text || bounds.bottom - bounds.top < font.height) return;
        surface.clip = SmallMapViewport::Intersect(surface.clip, bounds);
        int count = 0, length = 0;
        for (; count < 63 && text[count]; ++count)
        {
            int width = 0;
            Glyph(font, static_cast<unsigned char>(text[count]), width);
            if (length + width > bounds.right - bounds.left - 4) break;
            length += width;
        }
        const int x = bounds.left + (bounds.right - bounds.left - length) / 2;
        const int y = bounds.top + (bounds.bottom - bounds.top - font.height) / 2;
        for (int pass = 0; pass < 2; ++pass)
        {
            int pen = x;
            for (int i = 0; i < count; ++i)
            {
                int width = 0;
                const auto* bits = Glyph(font, static_cast<unsigned char>(text[i]), width);
                Geometry::Glyph(surface, pen, y, bits, width, font.height, color, pass == 0);
                pen += width;
            }
        }
    }

    void DrawMenuButton(const Geometry::Surface& surface, const Font& font, Rect rect, const char* text, bool selected)
    {
        Geometry::Fill(surface, rect, selected ? 91 : 93);
        DrawMenuBorder(surface, rect, selected ? 0 : 86, selected ? 86 : 0);
        DrawMenuText(surface, font, rect, text, selected ? 208 : 255);
    }

    void DrawMenuCursor(const Geometry::Surface& surface, TAdynmemStruct* ta, const PanelInfo& panels, POINT mouse)
    {
        if (Obscured(panels, mouse.x, mouse.y)) return;
        auto* sequence = ta->cursor_ary[cursornormal];
        if (!sequence || !sequence->Frames || !sequence->PtrFrameAry[0].PtrFrame) return;
        OFFSCREEN context = {};
        context.lpSurface = surface.pixels;
        context.Width = surface.pitch;
        context.Height = surface.height;
        context.lPitch = surface.pitch;
        context.field_10 = 10000;
        context.field_2C = 1;
        context.ScreenRect = {0, 0, surface.width - 1, surface.height - 1};
        GafFrame2OFFScreen(&context, sequence->PtrFrameAry[0].PtrFrame, mouse.x, mouse.y);
    }
}

namespace MultiBuildMenu
{
    bool Message(HWND window, UINT message, WPARAM keys, LPARAM position)
    {
        std::lock_guard<std::recursive_mutex> lock(menuMutex);
        if (message == WM_KILLFOCUS || message == WM_CANCELMODE || message == WM_CAPTURECHANGED)
        {
            state.pressed = {};
            suppressLeftUp = false;
            return false;
        }
        if (message != WM_LBUTTONDOWN && message != WM_LBUTTONUP && message != WM_LBUTTONDBLCLK
            && message != WM_MOUSEWHEEL && message != WM_MOUSEMOVE) return false;
        auto* ta = *TAmainStruct_PtrPtr;
        const bool matchingUp = message == WM_LBUTTONUP && suppressLeftUp;
        if (!Context(ta))
        {
            if (matchingUp) suppressLeftUp = false;
            return matchingUp;
        }
        const auto panels = InspectPanels(ta);
        const auto& layout = panels.layout;
        state.page = (std::min)(state.page, Geometry::Pages(state.options.size(), layout.capacity) - 1);
        POINT point = {static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
        if (message == WM_MOUSEWHEEL) ScreenToClient(window, &point);
        const auto hit = !Obscured(panels, point.x, point.y)
            ? Geometry::HitTest(layout, state.build, state.page, state.options.size(), point.x, point.y) : Geometry::Hit();
        if (message == WM_MOUSEMOVE) return false; // Let TA move its normal mouse pointer.
        if (message == WM_MOUSEWHEEL)
        {
            if (!state.build || hit.kind == Kind::None) return false;
            state.wheel += static_cast<short>(HIWORD(keys));
            const int steps = state.wheel / WHEEL_DELTA;
            state.wheel %= WHEEL_DELTA;
            state.page = (std::max)(0, (std::min)(state.page - steps,
                Geometry::Pages(state.options.size(), layout.capacity) - 1));
            return true;
        }
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK)
        {
            if (hit.kind == Kind::None) return false;
            state.pressed = hit;
            suppressLeftUp = true;
            return true;
        }
        if (!matchingUp) return false;
        suppressLeftUp = false;
        const auto pressed = state.pressed;
        state.pressed = {};
        if (!(pressed == hit)) return true;
        switch (hit.kind)
        {
        case Kind::Build: state.build = true; DirtyNative(ta); break;
        case Kind::Orders: state.build = false; DirtyNative(ta); break;
        case Kind::Previous: state.page = (std::max)(0, state.page - 1); break;
        case Kind::Next: state.page = (std::min)(state.page + 1,
            Geometry::Pages(state.options.size(), layout.capacity) - 1); break;
        case Kind::Item:
            if (hit.index >= 0 && static_cast<std::size_t>(hit.index) < state.options.size())
                MultiBuild::Choose(ta, state.options[hit.index].type);
            break;
        default: break;
        }
        return true;
    }

    void Draw(char* pixels, int width, int height, int pitch, TAdynmemStruct* ta)
    {
        std::lock_guard<std::recursive_mutex> lock(menuMutex);
        if (!Context(ta)) return;
        const auto panels = InspectPanels(ta);
        const auto& layout = panels.layout;
        if (!layout.capacity) return;
        Geometry::Surface surface = {reinterpret_cast<unsigned char*>(pixels), width, height, pitch, layout.bounds,
            panels.excluded.data(), panels.excluded.size()};
        if (!Geometry::Valid(surface)) return;
        const auto font = MenuFont(*ta);
        POINT mouse = {-1, -1};
        const bool pointer = CurrentMouse(mouse);
        const auto hover = pointer && !Obscured(panels, mouse.x, mouse.y)
            ? Geometry::HitTest(layout, state.build, state.page, state.options.size(), mouse.x, mouse.y) : Geometry::Hit();
        if (state.build)
        {
            // Clear every slot, including the last page's empty cells. Native
            // panels are cached, so merely drawing the new icons leaves ghosts.
            Geometry::Fill(surface, layout.bounds, 93);
            state.page = (std::min)(state.page, Geometry::Pages(state.options.size(), layout.capacity) - 1);
            for (int cell = 0; cell < layout.capacity; ++cell)
            {
                const auto index = static_cast<std::size_t>(state.page) * layout.capacity + cell;
                const auto rect = Geometry::Cell(layout, cell);
                Geometry::Fill(surface, rect, 95);
                if (index >= state.options.size()) { DrawMenuBorder(surface, rect, 0, 91); continue; }
                if (!state.loaded[index])
                {
                    state.pictures[index] = MultiBuildAssets::BuildPicture(state.options[index].name);
                    state.loaded[index] = true;
                }
                const auto& picture = state.pictures[index];
                Geometry::Image(surface, {rect.left + 1, rect.top + 1, rect.right - 1, rect.bottom - 1},
                    picture.pixels.data(), picture.width, picture.height, picture.pixels.size());
                const bool selected = ta->PrepareOrder_Type == ordertype::BUILD
                    && static_cast<unsigned short>(ta->BuildUnitID) == state.options[index].type;
                const bool highlighted = hover.kind == Kind::Item && hover.index == static_cast<int>(index);
                DrawMenuBorder(surface, rect, highlighted || selected ? 208 : 86, highlighted || selected ? 208 : 0);
                if (picture.pixels.empty()) DrawMenuText(surface, font, rect, state.options[index].label, 255);
            }
            DrawMenuButton(surface, font, layout.previous, "<", false);
            DrawMenuButton(surface, font, layout.next, ">", false);
            char page[32] = {};
            std::snprintf(page, sizeof(page), "%d/%d", state.page + 1, Geometry::Pages(state.options.size(), layout.capacity));
            DrawMenuText(surface, font, layout.page, page, 255);
            DrawMenuText(surface, font, layout.caption, hover.kind == Kind::Item && hover.index >= 0
                ? state.options[hover.index].label : "Select building", 255);
        }
        DrawMenuButton(surface, font, layout.build, "BUILD", state.build);
        DrawMenuButton(surface, font, layout.orders, "ORDERS", !state.build);
        if (pointer && hover.kind != Kind::None) DrawMenuCursor(surface, ta, panels, mouse);
    }
}

#else

namespace MultiBuildMenu
{
    bool Message(HWND, UINT, WPARAM, LPARAM) { return false; }
    void Draw(char*, int, int, int, TAdynmemStruct*) {}
}

#endif
