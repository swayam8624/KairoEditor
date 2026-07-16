module;

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

export module Kairo.Editor.KeymapSettings;

import Kairo.Editor.InputRouter;

export namespace kairo::editor
{
    [[nodiscard]] constexpr std::string_view Name(KeymapProfile profile) noexcept
    {
        switch (profile)
        {
            case KeymapProfile::Kairo: return "kairo";
            case KeymapProfile::Blender: return "blender";
            case KeymapProfile::Unreal: return "unreal";
            case KeymapProfile::Unity: return "unity";
        }
        return "invalid";
    }

    [[nodiscard]] inline KeymapProfile ParseKeymapProfile(std::string_view value)
    {
        if (value == "kairo") return KeymapProfile::Kairo;
        if (value == "blender") return KeymapProfile::Blender;
        if (value == "unreal") return KeymapProfile::Unreal;
        if (value == "unity") return KeymapProfile::Unity;
        throw std::invalid_argument("Keymap profile must be kairo, blender, unreal, or unity.");
    }

    [[nodiscard]] inline std::string SerializeKeymapSettings(KeymapProfile profile)
    {
        return "kairo-keymap 1\nprofile " + std::string(Name(profile)) + "\n";
    }

    [[nodiscard]] inline KeymapProfile ParseKeymapSettings(std::string_view source)
    {
        std::istringstream input{ std::string(source) };
        std::string header;
        std::string version;
        std::string statement;
        std::string profile;
        if (!(input >> header >> version) || header != "kairo-keymap" || version != "1")
            throw std::invalid_argument("Keymap settings require a 'kairo-keymap 1' header.");
        if (!(input >> statement >> profile) || statement != "profile")
            throw std::invalid_argument("Keymap settings require exactly one profile statement.");
        std::string trailing;
        if (input >> trailing)
            throw std::invalid_argument("Keymap settings contain an unknown trailing statement.");
        return ParseKeymapProfile(profile);
    }

    /// Output: one OS-user-owned settings path, never a project-relative path.
    /// `KAIRO_EDITOR_SETTINGS_DIR` provides a deterministic CI/test override.
    [[nodiscard]] inline std::filesystem::path DefaultKeymapSettingsPath()
    {
        if (const char* overrideDirectory = std::getenv("KAIRO_EDITOR_SETTINGS_DIR");
            overrideDirectory != nullptr && *overrideDirectory != '\0')
            return std::filesystem::path(overrideDirectory) / "keymap.settings";
#if defined(_WIN32)
        if (const char* appData = std::getenv("APPDATA"); appData != nullptr && *appData != '\0')
            return std::filesystem::path(appData) / "Kairo" / "keymap.settings";
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            return std::filesystem::path(home) / "Library" / "Application Support" /
                "Kairo" / "keymap.settings";
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0')
            return std::filesystem::path(xdg) / "Kairo" / "keymap.settings";
        if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0')
            return std::filesystem::path(home) / ".config" / "Kairo" / "keymap.settings";
#endif
        throw std::runtime_error("Cannot resolve a user settings directory for KairoEditor.");
    }

    [[nodiscard]] inline KeymapProfile LoadKeymapSettings(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            if (!std::filesystem::exists(path)) return KeymapProfile::Kairo;
            throw std::runtime_error("Cannot read keymap settings: " + path.string());
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.eof() && input.fail())
            throw std::runtime_error("Failed while reading keymap settings: " + path.string());
        return ParseKeymapSettings(contents.str());
    }

    inline void SaveKeymapSettings(const std::filesystem::path& path, KeymapProfile profile)
    {
        if (path.empty()) throw std::invalid_argument("Keymap settings path cannot be empty.");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create keymap settings temporary file.");
            output << SerializeKeymapSettings(profile);
            output.flush();
            if (!output) throw std::runtime_error("Failed while writing keymap settings.");
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
        if (error)
        {
            std::filesystem::remove(temporary);
            throw std::runtime_error("Cannot publish keymap settings: " + error.message());
        }
    }
}
