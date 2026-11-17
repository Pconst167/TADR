#include "config.h"
#include "MultiBuildAssets.h"

#if MULTI_BUILD_ENABLE
#include "oddraw.h"
#include "tamem.h"
#include "taHPI.h"
#include "tafunctions.h"
#include "MultiBuildData.h"
#include <cstring>
#include <map>
#include <mutex>

namespace
{
    std::mutex cacheMutex;
    std::map<std::string, std::vector<MultiBuildAssets::Button>> menuCache;
    std::map<std::string, MultiBuildAssets::Picture> pictureCache;
    const TAdynmemStruct* menuWorld = nullptr;
    const UnitDefStruct* menuDefinitions = nullptr;
    int menuTick = -1;
    std::size_t menuButtons = 0, pictureBytes = 0;

    bool BoundedString(const char* data, std::size_t limit, std::string& out)
    {
        out.clear();
        if (!data) return false;
        std::size_t size = 0;
        while (size < limit && data[size]) ++size;
        if (!size || size == limit) return false;
        out.assign(data, size);
        return true;
    }
    bool Read(const std::string& path, std::size_t maximum, std::vector<unsigned char>& out)
    {
        out.clear();
        if (path.empty() || path.size() >= 260) return false;
        char filename[260];
        std::memcpy(filename, path.c_str(), path.size() + 1);
        const unsigned expected = filelength_HPI(filename);
        if (!expected || expected > maximum) return false;
        unsigned size = 0;
        struct File
        {
            char* bytes;
            ~File() { if (bytes) freeTAMem(bytes); }
        } file = {readfile_HPI(filename, &size)};
        if (!file.bytes || !size || size > maximum) return false;
        out.assign(reinterpret_cast<const unsigned char*>(file.bytes),
            reinterpret_cast<const unsigned char*>(file.bytes) + size);
        return true;
    }
    std::string Join(const std::string& directory, const std::string& name)
    {
        return directory + (directory.back() == '\\' || directory.back() == '/' ? "" : "\\") + name;
    }
}

namespace MultiBuildAssets
{
    std::vector<Button> Menu(const TAdynmemStruct& ta, const UnitDefStruct& constructor)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (&ta != menuWorld || ta.UnitDef != menuDefinitions || ta.GameTime < menuTick)
        {
            menuCache.clear(); menuButtons = 0;
            menuWorld = &ta; menuDefinitions = ta.UnitDef;
        }
        menuTick = ta.GameTime;
        if (!constructor.GUINum) return {};
        static_assert(sizeof(ta.desktopGUI) >= 0x9b6 + 256, "Native GUI directory layout");
        std::string directory, name;
        if (!BoundedString(reinterpret_cast<const char*>(&ta.desktopGUI) + 0x9b6, 256, directory)) return {};
        std::size_t size = 0;
        while (size < sizeof(constructor.UnitName) && constructor.UnitName[size]) ++size;
        if (!MultiBuildData::UnitName(constructor.UnitName, size, name)) return {};
        const std::string prefix = Join(directory, name);
        const bool zeroPage = (constructor.UnitTypeMask_0 & 0x80000000u) != 0;
        const std::string key = prefix + ":" + std::to_string(constructor.GUINum) + (zeroPage ? ":0" : ":1");
        const auto cached = menuCache.find(key);
        if (cached != menuCache.end()) return cached->second;
        std::vector<Button> buttons;
        std::vector<unsigned char> bytes;
        for (unsigned page = 0; page < constructor.GUINum; ++page)
        {
            if (!page && !zeroPage) continue;
            if (!Read(prefix + std::to_string(page) + ".GUI", 1024 * 1024, bytes)) continue;
            std::vector<Button> pageButtons;
            if (!MultiBuildData::EnumerateMenu(reinterpret_cast<const char*>(bytes.data()), bytes.size(), pageButtons)) continue;
            for (const auto& button : pageButtons)
            {
                bool duplicate = false;
                for (const auto& old : buttons) if (old.name == button.name) duplicate = true;
                if (!duplicate) buttons.push_back(button);
                if (buttons.size() >= 4096) break;
            }
            if (buttons.size() >= 4096) break;
        }
        if (menuCache.size() >= 512 || menuButtons + buttons.size() > 8192)
        { menuCache.clear(); menuButtons = 0; }
        menuButtons += buttons.size();
        menuCache.emplace(key, buttons);
        return buttons;
    }
    Picture BuildPicture(const char* unitName)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        std::string name, directory;
        if (!unitName) return {};
        std::size_t size = 0;
        while (size < 33 && unitName[size]) ++size;
        if (!MultiBuildData::UnitName(unitName, size, name)) return {};
        // Native unit-picture loaders at 0x44C188 and 0x494421 use this
        // mod-patched directory literal, then UnitDef.UnitName + ".pcx".
        if (!BoundedString(reinterpret_cast<const char*>(0x505fe4), 64, directory)) return {};
        const std::string path = Join(directory, name + ".pcx");
        const auto cached = pictureCache.find(path);
        if (cached != pictureCache.end()) return cached->second;
        Picture picture;
        std::vector<unsigned char> bytes;
        if (Read(path, 16 * 1024 * 1024, bytes))
            MultiBuildData::DecodePCX8(bytes.data(), bytes.size(), picture);
        if (pictureCache.size() >= 512 || pictureBytes + picture.pixels.size() > 16 * 1024 * 1024)
        { pictureCache.clear(); pictureBytes = 0; }
        pictureBytes += picture.pixels.size();
        pictureCache.emplace(path, picture);
        return picture;
    }
}
#else
namespace MultiBuildAssets
{
    std::vector<Button> Menu(const TAdynmemStruct&, const UnitDefStruct&) { return {}; }
    Picture BuildPicture(const char*) { return {}; }
}
#endif
