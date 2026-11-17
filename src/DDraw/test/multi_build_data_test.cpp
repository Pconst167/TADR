#include "../MultiBuildData.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    std::string Gadget(const std::string& name, const std::string& fields = "id=1;active=1;attribs=32;",
        const std::string& outer = "")
    {
        return "[GADGET4]{[COMMON]{name=" + name + ";" + fields + "}" + outer + "}";
    }
    void TestMenus()
    {
        std::vector<MultiBuildAssets::Button> buttons;
        const std::string full =
            "// Initial comments\r\n[GADGET0]{[COMMON]{id=0;name=HEADER;active=1;attribs=0;}"
            "totalgadgets=9;panel=ARMPAN;[VERSION]{major=1;minor=0;}}"
            "[GADGET1]{[COMMON]{id=7;name=ARMBUTT;active=1;}filename=armbutt;}"
            + Gadget("armfus", "id=1;active=1;attribs=32;help=;", "quickkey=121;text=;")
            + Gadget("ARMMOHO", "id=1;active=1;attribs=32;", "quickkey=113;")
            + Gadget("armfus", "id=1;active=1;attribs=32;", "quickkey=999;")
            + Gadget("EMPTY", "id=1;active=0;attribs=32;")
            + Gadget("WRONGID", "id=7;active=1;attribs=32;")
            + Gadget("NOATTRIB", "id=1;active=1;")
            + Gadget("DEFAULTINACTIVE", "id=1;attribs=32;")
            + "/* trailing comment */";
        assert(MultiBuildData::EnumerateMenu(full.data(), full.size(), buttons));
        assert(buttons.size() == 2 && buttons[0].name == "ARMFUS" && buttons[0].quickkey == 121);
        assert(buttons[1].name == "ARMMOHO" && buttons[1].quickkey == 113);
        const std::string quoted = Gadget("\"arm_moho-2\"", "ID=+1;ACTIVE=1;ATTRIBS=+32;", "quickkey=0;");
        assert(MultiBuildData::EnumerateMenu(quoted.data(), quoted.size(), buttons));
        assert(buttons.size() == 1 && buttons[0].name == "ARM_MOHO-2" && buttons[0].quickkey == 0);
        for (const auto& invalid : {
            Gadget("BLOCKED", "id=1;active=1;active=1;attribs=32;"),
            Gadget("BLOCKED", "id=1;active=1;attribs=32;", "quickkey=1;quickkey=2;"),
            Gadget("BLOCKED", "id=1;active=1;attribs=32;", "quickkey=999999999999;"),
            Gadget("../BLOCKED"), Gadget("bad/name"), Gadget("\"two names\""),
            std::string("[GADGET1]{[COMMON]{name=BLOCKED;id=1;active=1;attribs=32;}[COMMON]{}}"),
            std::string("[OTHER]{[COMMON]{name=BLOCKED;id=1;active=1;attribs=32;}}"),
            std::string("[GADGET1]{[OTHER]{name=BLOCKED;id=1;active=1;attribs=32;}}")})
        {
            assert(MultiBuildData::EnumerateMenu(invalid.data(), invalid.size(), buttons));
            assert(buttons.empty());
        }
        for (const auto& malformed : {full + "[GADGET9]{", full + "/* unfinished",
            full + "unexpected", full + "}", Gadget("\"unfinished")})
        {
            assert(!MultiBuildData::EnumerateMenu(malformed.data(), malformed.size(), buttons));
            assert(buttons.empty());
        }
        assert(!MultiBuildData::EnumerateMenu(nullptr, 1, buttons));
        assert(!MultiBuildData::EnumerateMenu(full.data(), 1024 * 1024 + 1, buttons));
        std::string nested;
        for (int i = 0; i < 34; ++i) nested += "[OTHER]{";
        for (int i = 0; i < 34; ++i) nested += "}";
        assert(!MultiBuildData::EnumerateMenu(nested.data(), nested.size(), buttons));
    }
    std::vector<unsigned char> Header(unsigned width, unsigned height, unsigned stride, unsigned encoding = 1)
    {
        std::vector<unsigned char> bytes(128, 0);
        bytes[0] = 10; bytes[1] = 5; bytes[2] = static_cast<unsigned char>(encoding);
        bytes[3] = 8; bytes[65] = 1;
        bytes[8] = static_cast<unsigned char>((width - 1) & 255);
        bytes[9] = static_cast<unsigned char>((width - 1) >> 8);
        bytes[10] = static_cast<unsigned char>((height - 1) & 255);
        bytes[11] = static_cast<unsigned char>((height - 1) >> 8);
        bytes[66] = static_cast<unsigned char>(stride & 255); bytes[67] = static_cast<unsigned char>(stride >> 8);
        return bytes;
    }
    void TestPictures()
    {
        MultiBuildAssets::Picture image;
        auto bytes = Header(3, 2, 4);
        bytes.insert(bytes.end(), {1,2,3,99,4,5,6,99});
        assert(MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        assert(image.width == 3 && image.height == 2);
        assert((image.pixels == std::vector<unsigned char>{1,2,3,4,5,6}));
        bytes = Header(3, 2, 4);
        bytes.insert(bytes.end(), {0xc4,7,0xc2,255,0xc2,9});
        assert(MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        assert((image.pixels == std::vector<unsigned char>{7,7,7,255,255,9}));
        // Decoding must not use the trailing palette as missing pixel data.
        auto truncated = Header(3, 2, 4);
        truncated.insert(truncated.end(), {1,2,3});
        truncated.push_back(12); truncated.insert(truncated.end(), 768, 7);
        assert(!MultiBuildData::DecodePCX8(truncated.data(), truncated.size(), image));
        assert(!image.width && !image.height && image.pixels.empty());
        bytes.push_back(12); bytes.insert(bytes.end(), 768, 0);
        assert(MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        bytes = Header(2, 1, 2, 0); bytes.insert(bytes.end(), {0xc0,0xff});
        assert(MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        assert(image.pixels[0] == 0xc0 && image.pixels[1] == 0xff);
        for (auto invalid : {Header(1025, 2, 1025), Header(2,1025,2), Header(4,1,2), Header(1,1,4097)})
            assert(!MultiBuildData::DecodePCX8(invalid.data(), invalid.size(), image));
        bytes = Header(2, 1, 2); bytes.insert(bytes.end(), {0xc3,8});
        assert(!MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        bytes = Header(2, 1, 2); bytes.push_back(0xc0);
        assert(!MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        bytes = Header(2, 1, 2); bytes.push_back(0xc2);
        assert(!MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        bytes = Header(2, 1, 2); bytes.insert(bytes.end(), {1,2}); bytes[65] = 3;
        assert(!MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), image));
        assert(!MultiBuildData::DecodePCX8(nullptr, 128, image));
        assert(!MultiBuildData::DecodePCX8(bytes.data(), 127, image));
    }
}

int main(int argc, char** argv)
{
    // Optional stdin modes let a local audit feed complete original mod files
    // without redistributing game GUI or picture assets in this repository.
    if (argc == 2)
    {
        const std::string input((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        if (std::string(argv[1]) == "menu")
        {
            std::vector<MultiBuildAssets::Button> buttons;
            if (!MultiBuildData::EnumerateMenu(input.data(), input.size(), buttons)) return 2;
            for (const auto& button : buttons) std::cout << button.name << '\t' << button.quickkey << '\n';
            return 0;
        }
        if (std::string(argv[1]) == "pcx")
        {
            MultiBuildAssets::Picture picture;
            if (!MultiBuildData::DecodePCX8(reinterpret_cast<const unsigned char*>(input.data()), input.size(), picture)) return 2;
            std::uint32_t hash = 2166136261u;
            for (unsigned char pixel : picture.pixels) { hash ^= pixel; hash *= 16777619u; }
            std::cout << picture.width << '\t' << picture.height << '\t' << picture.pixels.size() << '\t' << hash << '\n';
            return 0;
        }
        return 3;
    }
    TestMenus(); TestPictures();
    std::cout << "Multi-build menu and PCX data tests passed\n";
}
