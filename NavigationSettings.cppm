module;

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

export module Kairo.Editor.NavigationSettings;

export namespace kairo::editor
{
    enum class ViewportScrollBehavior : unsigned char { Dolly, Pan };

    /// Input: user-authored navigation preferences in backend-neutral units.
    /// Output: validated camera and canvas sensitivity values.
    /// Task: keep trackpad/mouse behavior consistent across editor sessions and
    /// UI backends without embedding platform key codes in the camera model.
    struct NavigationSettings final
    {
        float OrbitSensitivity = 0.008f;
        float PanSensitivity = 0.0022f;
        float DollySensitivity = 0.013f;
        float FlySpeed = 1.8f;
        bool InvertOrbitX = false;
        bool InvertOrbitY = false;
        ViewportScrollBehavior ScrollBehavior = ViewportScrollBehavior::Dolly;

        friend bool operator==(const NavigationSettings&, const NavigationSettings&) = default;
    };

    inline void ValidateNavigationSettings(const NavigationSettings& settings)
    {
        const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
        if (!positive(settings.OrbitSensitivity) || !positive(settings.PanSensitivity) ||
            !positive(settings.DollySensitivity) || !positive(settings.FlySpeed))
            throw std::invalid_argument("Navigation sensitivities and fly speed must be finite and positive.");
    }

    [[nodiscard]] inline std::filesystem::path DefaultNavigationSettingsPath()
    {
#if defined(_WIN32)
        if (const char* root = std::getenv("APPDATA")) return std::filesystem::path(root) / "Kairo" / "navigation.settings";
#elif defined(__APPLE__)
        if (const char* root = std::getenv("HOME"))
            return std::filesystem::path(root) / "Library" / "Application Support" / "Kairo" / "navigation.settings";
#else
        if (const char* root = std::getenv("XDG_CONFIG_HOME"))
            return std::filesystem::path(root) / "kairo" / "navigation.settings";
        if (const char* root = std::getenv("HOME"))
            return std::filesystem::path(root) / ".config" / "kairo" / "navigation.settings";
#endif
        return {};
    }

    [[nodiscard]] inline std::string SerializeNavigationSettings(const NavigationSettings& settings)
    {
        ValidateNavigationSettings(settings);
        std::ostringstream output;
        output << "kairo-navigation 1\n"
            << "orbit-sensitivity " << settings.OrbitSensitivity << '\n'
            << "pan-sensitivity " << settings.PanSensitivity << '\n'
            << "dolly-sensitivity " << settings.DollySensitivity << '\n'
            << "fly-speed " << settings.FlySpeed << '\n'
            << "invert-orbit-x " << (settings.InvertOrbitX ? "true" : "false") << '\n'
            << "invert-orbit-y " << (settings.InvertOrbitY ? "true" : "false") << '\n'
            << "scroll-behavior " << (settings.ScrollBehavior == ViewportScrollBehavior::Dolly ? "dolly" : "pan") << '\n';
        return output.str();
    }

    [[nodiscard]] inline NavigationSettings ParseNavigationSettings(std::string_view source)
    {
        std::istringstream input{ std::string(source) };
        std::string magic;
        unsigned version = 0u;
        if (!(input >> magic >> version) || magic != "kairo-navigation" || version != 1u)
            throw std::invalid_argument("Navigation settings require 'kairo-navigation 1'.");
        NavigationSettings result;
        bool seen[7]{};
        std::string key;
        std::string value;
        while (input >> key >> value)
        {
            auto once = [&](int index)
            {
                if (seen[index]) throw std::invalid_argument("Navigation settings repeat '" + key + "'.");
                seen[index] = true;
            };
            auto number = [&]()
            {
                std::size_t consumed = 0u;
                const float parsed = std::stof(value, &consumed);
                if (consumed != value.size()) throw std::invalid_argument("Navigation setting is not numeric: " + key);
                return parsed;
            };
            auto boolean = [&]()
            {
                if (value == "true") return true;
                if (value == "false") return false;
                throw std::invalid_argument("Navigation setting requires true or false: " + key);
            };
            if (key == "orbit-sensitivity") { once(0); result.OrbitSensitivity = number(); }
            else if (key == "pan-sensitivity") { once(1); result.PanSensitivity = number(); }
            else if (key == "dolly-sensitivity") { once(2); result.DollySensitivity = number(); }
            else if (key == "fly-speed") { once(3); result.FlySpeed = number(); }
            else if (key == "invert-orbit-x") { once(4); result.InvertOrbitX = boolean(); }
            else if (key == "invert-orbit-y") { once(5); result.InvertOrbitY = boolean(); }
            else if (key == "scroll-behavior")
            {
                once(6);
                if (value == "dolly") result.ScrollBehavior = ViewportScrollBehavior::Dolly;
                else if (value == "pan") result.ScrollBehavior = ViewportScrollBehavior::Pan;
                else throw std::invalid_argument("Scroll behavior must be dolly or pan.");
            }
            else throw std::invalid_argument("Unknown navigation setting: " + key);
        }
        if (!input.eof()) throw std::invalid_argument("Navigation settings are malformed.");
        ValidateNavigationSettings(result);
        return result;
    }

    [[nodiscard]] inline NavigationSettings LoadNavigationSettings(const std::filesystem::path& path)
    {
        if (path.empty()) return {};
        std::ifstream input(path, std::ios::binary);
        if (!input) return {};
        std::ostringstream source;
        source << input.rdbuf();
        if (!input.eof() && input.fail()) throw std::runtime_error("Cannot read navigation settings.");
        return ParseNavigationSettings(source.str());
    }

    inline void SaveNavigationSettings(const std::filesystem::path& path,
        const NavigationSettings& settings)
    {
        if (path.empty()) throw std::invalid_argument("Navigation settings path cannot be empty.");
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) throw std::runtime_error("Cannot create navigation settings directory: " + error.message());
        const auto temporary = path.string() + ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot create navigation settings temporary file.");
            output << SerializeNavigationSettings(settings);
            output.flush();
            if (!output) throw std::runtime_error("Cannot write navigation settings.");
        }
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
        }
        if (error) throw std::runtime_error("Cannot publish navigation settings: " + error.message());
    }
}
