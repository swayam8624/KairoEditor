module;

#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.KeymapSettings;

import Kairo.Editor.Actions;
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

    struct EditorKeymapSettings final
    {
        KeymapProfile Profile = KeymapProfile::Kairo;
        std::vector<KeymapOverride> Overrides;

        [[nodiscard]] friend bool operator==(
            const EditorKeymapSettings& left, const EditorKeymapSettings& right)
        {
            if (left.Profile != right.Profile || left.Overrides.size() != right.Overrides.size()) return false;
            for (const KeymapOverride& expected : left.Overrides)
            {
                const auto found = std::ranges::find_if(right.Overrides,
                    [&](const KeymapOverride& candidate)
                    {
                        return candidate.Action == expected.Action && candidate.Context == expected.Context;
                    });
                if (found == right.Overrides.end() || found->Chords.size() != expected.Chords.size() ||
                    !std::ranges::is_permutation(found->Chords, expected.Chords)) return false;
            }
            return true;
        }
    };

    [[nodiscard]] inline std::string FormatInputChord(InputChord chord)
    {
        std::string result;
        const auto append = [&](std::string_view token)
        {
            if (!result.empty()) result.push_back('+');
            result.append(token);
        };
        const auto modifiers = static_cast<std::uint8_t>(chord.Modifiers);
        if ((modifiers & static_cast<std::uint8_t>(KeyModifiers::Shortcut)) != 0u) append("shortcut");
        if ((modifiers & static_cast<std::uint8_t>(KeyModifiers::Shift)) != 0u) append("shift");
        if ((modifiers & static_cast<std::uint8_t>(KeyModifiers::Alt)) != 0u) append("alt");
        append(Name(chord.Key));
        return result;
    }

    [[nodiscard]] inline InputChord ParseInputChord(std::string_view text)
    {
        if (text.empty() || text.size() > 64u)
            throw std::invalid_argument("A key chord must contain 1 to 64 ASCII bytes.");
        KeyModifiers modifiers = KeyModifiers::None;
        std::optional<EditorKey> key;
        std::size_t first = 0u;
        while (first <= text.size())
        {
            const std::size_t plus = text.find('+', first);
            const std::string_view token = text.substr(first,
                plus == std::string_view::npos ? text.size() - first : plus - first);
            if (token.empty()) throw std::invalid_argument("A key chord contains an empty token.");
            auto addModifier = [&](KeyModifiers value)
            {
                const auto existing = static_cast<std::uint8_t>(modifiers);
                const auto bit = static_cast<std::uint8_t>(value);
                if ((existing & bit) != 0u) throw std::invalid_argument("A key chord repeats a modifier.");
                modifiers = modifiers | value;
            };
            if (token == "shortcut") addModifier(KeyModifiers::Shortcut);
            else if (token == "shift") addModifier(KeyModifiers::Shift);
            else if (token == "alt") addModifier(KeyModifiers::Alt);
            else
            {
                const auto parsed = ParseEditorKey(token);
                if (!parsed.has_value()) throw std::invalid_argument("A key chord contains an unknown key.");
                if (key.has_value()) throw std::invalid_argument("A key chord contains multiple keys.");
                key = *parsed;
            }
            if (plus == std::string_view::npos) break;
            first = plus + 1u;
        }
        if (!key.has_value()) throw std::invalid_argument("A key chord requires one key.");
        return { *key, modifiers };
    }

    [[nodiscard]] inline std::string SerializeKeymapSettings(const EditorKeymapSettings& settings)
    {
        (void)BuildInputBindings(settings.Profile, settings.Overrides);
        std::vector<KeymapOverride> overrides = settings.Overrides;
        std::ranges::sort(overrides, [](const KeymapOverride& left, const KeymapOverride& right)
        {
            if (left.Context != right.Context) return left.Context < right.Context;
            return left.Action < right.Action;
        });
        std::ostringstream output;
        output << "kairo-keymap 2\nprofile " << Name(settings.Profile) << '\n';
        for (KeymapOverride& overrideBinding : overrides)
        {
            std::ranges::sort(overrideBinding.Chords, [](InputChord left, InputChord right)
            {
                if (left.Key != right.Key) return left.Key < right.Key;
                return left.Modifiers < right.Modifiers;
            });
            if (overrideBinding.Chords.empty())
                output << "unbind " << Name(overrideBinding.Context) << ' '
                    << Key(overrideBinding.Action) << '\n';
            else
                for (const InputChord chord : overrideBinding.Chords)
                    output << "override " << Name(overrideBinding.Context) << ' '
                        << Key(overrideBinding.Action) << ' ' << FormatInputChord(chord) << '\n';
        }
        return output.str();
    }

    [[nodiscard]] inline EditorKeymapSettings ParseEditorKeymapSettings(std::string_view source)
    {
        if (source.size() > 1024u * 1024u)
            throw std::length_error("Keymap settings exceed the 1 MiB safety limit.");
        std::istringstream input{ std::string(source) };
        std::string line;
        std::size_t lineNumber = 0u;
        auto fail = [&](std::string message) -> void
        {
            throw std::invalid_argument("Keymap settings line " + std::to_string(lineNumber) + ": " + message);
        };
        if (!std::getline(input, line)) fail("missing header.");
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream header(line);
        std::string magic;
        unsigned version = 0u;
        std::string trailing;
        if (!(header >> magic >> version) || magic != "kairo-keymap" ||
            (version != 1u && version != 2u) || (header >> trailing))
            fail("expected 'kairo-keymap 1' or 'kairo-keymap 2'.");

        EditorKeymapSettings result;
        bool hasProfile = false;
        std::map<std::pair<InputContext, EditorAction>, std::vector<InputChord>> grouped;
        std::set<std::pair<InputContext, EditorAction>> explicitlyUnbound;
        while (std::getline(input, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::istringstream statement(line);
            std::string operation;
            statement >> operation;
            if (operation == "profile")
            {
                std::string profile;
                if (hasProfile || !(statement >> profile) || (statement >> trailing))
                    fail("profile must appear exactly once with one value.");
                try { result.Profile = ParseKeymapProfile(profile); }
                catch (const std::exception& error) { fail(error.what()); }
                hasProfile = true;
                continue;
            }
            if (version == 1u) fail("version 1 supports only the profile statement.");
            if (operation != "override" && operation != "unbind")
                fail("unknown statement '" + operation + "'.");
            std::string contextText;
            std::string actionText;
            std::string chordText;
            if (!(statement >> contextText >> actionText)) fail("binding statement is incomplete.");
            const auto context = ParseInputContext(contextText);
            const auto action = ParseEditorAction(actionText);
            if (!context.has_value()) fail("unknown input context '" + contextText + "'.");
            if (!action.has_value()) fail("unknown editor action '" + actionText + "'.");
            const auto identity = std::pair{ *context, *action };
            if (operation == "unbind")
            {
                if ((statement >> trailing) || grouped.contains(identity) || !explicitlyUnbound.insert(identity).second)
                    fail("unbind conflicts with another override for the same action/context.");
                grouped.emplace(identity, std::vector<InputChord>{});
                continue;
            }
            if (!(statement >> chordText) || (statement >> trailing) || explicitlyUnbound.contains(identity))
                fail("override requires exactly one chord and cannot follow unbind.");
            try { grouped[identity].push_back(ParseInputChord(chordText)); }
            catch (const std::exception& error) { fail(error.what()); }
        }
        if (!hasProfile) fail("missing profile statement.");
        for (auto& [identity, chords] : grouped)
            result.Overrides.push_back({ identity.second, identity.first, std::move(chords) });
        try { (void)BuildInputBindings(result.Profile, result.Overrides); }
        catch (const std::exception& error) { fail(error.what()); }
        return result;
    }

    [[nodiscard]] inline std::string SerializeKeymapSettings(KeymapProfile profile)
    {
        return SerializeKeymapSettings(EditorKeymapSettings{ profile, {} });
    }

    [[nodiscard]] inline KeymapProfile ParseKeymapSettings(std::string_view source)
    {
        return ParseEditorKeymapSettings(source).Profile;
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

    [[nodiscard]] inline EditorKeymapSettings LoadEditorKeymapSettings(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            if (!std::filesystem::exists(path)) return {};
            throw std::runtime_error("Cannot read keymap settings: " + path.string());
        }
        std::ostringstream contents;
        contents << input.rdbuf();
        if (!input.eof() && input.fail())
            throw std::runtime_error("Failed while reading keymap settings: " + path.string());
        return ParseEditorKeymapSettings(contents.str());
    }

    [[nodiscard]] inline KeymapProfile LoadKeymapSettings(const std::filesystem::path& path)
    {
        return LoadEditorKeymapSettings(path).Profile;
    }

    inline void SaveKeymapSettings(const std::filesystem::path& path,
        const EditorKeymapSettings& settings)
    {
        if (path.empty()) throw std::invalid_argument("Keymap settings path cannot be empty.");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create keymap settings temporary file.");
            output << SerializeKeymapSettings(settings);
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

    inline void SaveKeymapSettings(const std::filesystem::path& path, KeymapProfile profile)
    {
        SaveKeymapSettings(path, EditorKeymapSettings{ profile, {} });
    }
}
