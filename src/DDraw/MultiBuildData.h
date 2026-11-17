#pragma once

#include "MexUpgradeMenu.h"
#include "MultiBuildAssets.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// File-format readers only: no Windows, live game pointers or AI CANBUILD data.
namespace MultiBuildData
{
    inline bool UnitName(const char* text, std::size_t length, std::string& normalized)
    {
        normalized.clear();
        if (!text || !length || length > 32) return false;
        for (std::size_t i = 0; i < length; ++i)
        {
            const char c = text[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            { normalized.clear(); return false; }
            normalized += c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        }
        return true;
    }

    namespace Detail
    {
        using namespace MexUpgradeMenu::Detail;
        struct Fields
        {
            unsigned seen = 0;
            bool valid = true;
            std::string name;
            std::int32_t id = 0, active = 0, attribs = 0;
            bool Build() const
            {
                return valid && !name.empty() && id == 1 && active != 0 && (attribs & 32) != 0;
            }
        };
        struct Section
        {
            Fields common;
            unsigned commons = 0;
            int quickkey = 0;
            bool quickkeySeen = false, valid = true;
        };
        enum Context { Root, Other, GadgetBody, CommonBody };
        class Parser
        {
            Reader reader;
            std::vector<MultiBuildAssets::Button>& buttons;
            bool Body(Context context, unsigned depth, Section& section)
            {
                if (depth > 32) return false;
                for (;;)
                {
                    const Token token = reader.Next();
                    if (token.kind == End) return context == Root;
                    if (token.kind == CloseBody)
                    {
                        if (context == Root) return false;
                        if (context == GadgetBody && section.valid && section.commons == 1 && section.common.Build())
                        {
                            bool duplicate = false;
                            for (const auto& old : buttons) if (old.name == section.common.name) duplicate = true;
                            if (!duplicate)
                            {
                                if (buttons.size() >= 4096) return false;
                                MultiBuildAssets::Button button;
                                button.name = section.common.name; button.quickkey = section.quickkey;
                                buttons.push_back(button);
                            }
                        }
                        return true;
                    }
                    if (token.kind == Semicolon) continue;
                    if (token.kind == OpenSection)
                    {
                        const Token name = reader.Next();
                        if (name.kind != Word || reader.Next().kind != CloseSection || reader.Next().kind != OpenBody) return false;
                        const bool common = context == GadgetBody && Equal(name.text, "common");
                        const Context childContext = common ? CommonBody
                            : context == Root && Gadget(name.text) ? GadgetBody : Other;
                        Section child;
                        if (!Body(childContext, depth + 1, child)) return false;
                        if (common) { ++section.commons; section.common = child.common; }
                        continue;
                    }
                    if (token.kind != Word || reader.Next().kind != Assign) return false;
                    unsigned values = 0;
                    Token value = {Invalid, {nullptr, 0}};
                    for (;;)
                    {
                        const Token part = reader.Next();
                        if (part.kind == Semicolon) break;
                        if (part.kind != Word && part.kind != Assign) return false;
                        value = part; ++values;
                    }
                    if (context == GadgetBody && Equal(token.text, "quickkey"))
                    {
                        std::int32_t key = 0;
                        if (section.quickkeySeen || values != 1 || value.kind != Word
                            || !Integer(value.text, key) || key < 0 || key > 65535)
                            section.valid = false;
                        else section.quickkey = key;
                        section.quickkeySeen = true;
                    }
                    if (context != CommonBody) continue;
                    const unsigned field = Equal(token.text, "name") ? 1 : Equal(token.text, "id") ? 2
                        : Equal(token.text, "active") ? 4 : Equal(token.text, "attribs") ? 8 : 0;
                    if (!field) continue;
                    Fields& fields = section.common;
                    if ((fields.seen & field) || values != 1 || value.kind != Word) { fields.valid = false; continue; }
                    fields.seen |= field;
                    if (field == 1)
                    {
                        if (!UnitName(value.text.data, value.text.size, fields.name)) fields.valid = false;
                    }
                    else
                    {
                        std::int32_t number = 0;
                        if (!Integer(value.text, number)) { fields.valid = false; continue; }
                        if (field == 2) fields.id = number;
                        else if (field == 4) fields.active = number;
                        else fields.attribs = number;
                    }
                }
            }
        public:
            Parser(const char* data, std::size_t length, std::vector<MultiBuildAssets::Button>& out)
                : reader(data, length), buttons(out) {}
            bool Parse() { Section ignored; return Body(Root, 0, ignored); }
        };
        inline unsigned Word16(const unsigned char* p)
        {
            return static_cast<unsigned>(p[0]) | static_cast<unsigned>(p[1]) << 8;
        }
    }

    inline bool EnumerateMenu(const char* data, std::size_t length, std::vector<MultiBuildAssets::Button>& out)
    {
        out.clear();
        if (!data || !length || length > 1024 * 1024) return false;
        std::vector<MultiBuildAssets::Button> parsed;
        if (!Detail::Parser(data, length, parsed).Parse()) return false;
        out.swap(parsed);
        return true;
    }

    inline bool DecodePCX8(const unsigned char* data, std::size_t length, MultiBuildAssets::Picture& out)
    {
        out = MultiBuildAssets::Picture();
        if (!data || length < 128 || length > 16 * 1024 * 1024 || data[0] != 10
            || data[1] != 5 || data[2] > 1 || data[3] != 8 || data[65] != 1) return false;
        const unsigned left = Detail::Word16(data + 4), top = Detail::Word16(data + 6);
        const unsigned right = Detail::Word16(data + 8), bottom = Detail::Word16(data + 10);
        if (right < left || bottom < top) return false;
        const unsigned width = right - left + 1, height = bottom - top + 1;
        const unsigned stride = Detail::Word16(data + 66);
        if (!width || width > 1024 || !height || height > 1024 || stride < width || stride > 4096) return false;
        std::size_t limit = length;
        if (length >= 128 + 769 && data[length - 769] == 12) limit -= 769;
        const std::size_t decodedSize = static_cast<std::size_t>(stride) * height;
        std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height);
        std::size_t at = 128, decoded = 0;
        while (decoded < decodedSize)
        {
            if (at >= limit) return false;
            unsigned char value = data[at++];
            unsigned run = 1;
            if (data[2] && (value & 0xc0) == 0xc0)
            {
                run = value & 0x3f;
                if (!run || at >= limit) return false;
                value = data[at++];
            }
            if (run > decodedSize - decoded) return false;
            for (unsigned i = 0; i < run; ++i, ++decoded)
            {
                const std::size_t column = decoded % stride;
                if (column < width) pixels[(decoded / stride) * width + column] = value;
            }
        }
        out.width = static_cast<int>(width); out.height = static_cast<int>(height);
        out.pixels.swap(pixels);
        return true;
    }
}
