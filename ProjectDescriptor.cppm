module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectDescriptor;

import Kairo.Assets;
import Kairo.Editor.TextFormat;
import Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    enum class ProjectBuildKind : std::uint8_t { Development, Release };

    [[nodiscard]] constexpr std::string_view Name(ProjectBuildKind kind) noexcept
    {
        return kind == ProjectBuildKind::Development ? "development" : "release";
    }

    struct ProjectBuildProfile final
    {
        std::string Name;
        ProjectBuildKind Kind = ProjectBuildKind::Development;
        std::filesystem::path OutputDirectory;

        friend bool operator==(const ProjectBuildProfile&, const ProjectBuildProfile&) = default;
    };

    /// Persistent project bootstrap data. Paths are portable and relative to
    /// the directory containing the `.kproject` descriptor.
    struct ProjectDescriptor final
    {
        ProjectDescriptor() = default;
        ProjectDescriptor(std::string name, std::filesystem::path assetManifest,
            std::filesystem::path startupScene)
            : Name(std::move(name)), AssetManifest(std::move(assetManifest)),
              StartupScene(std::move(startupScene)) {}

        std::string Name;
        std::filesystem::path AssetManifest = "Assets.kassets";
        std::filesystem::path StartupScene = "Scenes/Main.kscene";
        std::string EngineVersion = "0.1.0";
        std::filesystem::path InputMap = "Config/Input.kinput";
        std::string RenderingProfile = "desktop";
        std::vector<std::string> EnabledPlugins;
        std::vector<ProjectBuildProfile> BuildProfiles{
            { "Development", ProjectBuildKind::Development, "Build/Development" },
            { "Release", ProjectBuildKind::Release, "Build/Release" }
        };

        friend bool operator==(const ProjectDescriptor&, const ProjectDescriptor&) = default;
    };

    /// Located syntax or semantic error from a Kairo project descriptor.
    class ProjectFormatError final : public std::runtime_error
    {
    public:
        ProjectFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo project " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace project_format_detail
    {
        constexpr std::size_t MaxProjectBytes = 1024u * 1024u;
        constexpr std::size_t MaxProjectNameBytes = 256u;
        constexpr std::size_t MaxProjectSettingBytes = 128u;

        using Token = FormatToken;

        inline void ValidateProjectName(std::string_view name)
        {
            ValidateUtf8Text(name, { 1u, MaxProjectNameBytes, false, false }, "Project name");
        }

        inline void ValidateSetting(std::string_view value, std::string_view label)
        {
            ValidateUtf8Text(value, { 1u, MaxProjectSettingBytes, false, false }, label);
        }

        [[nodiscard]] inline std::filesystem::path ParsePortablePath(
            const Token& token, std::size_t line)
        {
            try { return kairo::assets::NormalizeAssetPath(token.Text); }
            catch (const std::exception& error) { throw ProjectFormatError(line, token.Column, error.what()); }
        }

        [[nodiscard]] inline std::vector<Token> Tokenize(std::string_view line, std::size_t lineNumber)
        {
            return TokenizeFormatLine<ProjectFormatError>(line, lineNumber);
        }

        inline void RequireCount(const std::vector<Token>& tokens, std::size_t expected,
            std::size_t line, std::string_view statement)
        {
            RequireTokenCount<ProjectFormatError>(tokens, expected, line, statement);
        }

        [[nodiscard]] inline std::string Quote(std::string_view value)
        {
            return QuoteFormatText(value);
        }
    }

    /// Input: project name and project-root-relative manifest/scene paths.
    /// Output: no value; throws std::invalid_argument on unsafe or ambiguous data.
    /// Task: centralize project invariants for creation, parsing, and saving.
    inline void ValidateProjectDescriptor(const ProjectDescriptor& descriptor)
    {
        using namespace project_format_detail;
        ValidateProjectName(descriptor.Name);
        const auto assets = kairo::assets::NormalizeAssetPath(descriptor.AssetManifest);
        const auto scene = kairo::assets::NormalizeAssetPath(descriptor.StartupScene);
        const auto input = kairo::assets::NormalizeAssetPath(descriptor.InputMap);
        if (assets == scene)
            throw std::invalid_argument("Asset manifest and startup scene paths must be different.");
        if (input == assets || input == scene)
            throw std::invalid_argument("Input map path must be distinct from the asset manifest and startup scene.");
        ValidateSetting(descriptor.EngineVersion, "Engine version");
        ValidateSetting(descriptor.RenderingProfile, "Rendering profile");
        if (descriptor.BuildProfiles.empty()) throw std::invalid_argument("Project requires at least one build profile.");
        std::set<std::string> profileNames;
        for (const auto& profile : descriptor.BuildProfiles)
        {
            ValidateSetting(profile.Name, "Build profile name");
            (void)kairo::assets::NormalizeAssetPath(profile.OutputDirectory);
            if (!profileNames.insert(profile.Name).second)
                throw std::invalid_argument("Build profile names must be unique.");
        }
        std::set<std::string> plugins;
        for (const auto& plugin : descriptor.EnabledPlugins)
        {
            ValidateSetting(plugin, "Plugin identifier");
            if (!plugins.insert(plugin).second) throw std::invalid_argument("Plugin identifiers must be unique.");
        }
    }

    /// Format: `kairo-project 1` followed by exactly one `name`, `assets`, and
    /// `startup-scene` statement. Unknown and duplicate statements are errors.
    [[nodiscard]] inline ProjectDescriptor ParseProjectDescriptor(std::string_view source)
    {
        using namespace project_format_detail;
        if (source.size() > MaxProjectBytes) throw std::length_error("Kairo project descriptor exceeds the 1 MiB safety limit.");

        ProjectDescriptor descriptor;
        descriptor.Name.clear();
        bool headerSeen = false;
        std::uint32_t version = 0u;
        bool nameSeen = false;
        bool assetsSeen = false;
        bool sceneSeen = false;
        bool engineSeen = false;
        bool inputSeen = false;
        bool renderingSeen = false;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t lineNumber = 0u;
        while (std::getline(input, lineText))
        {
            ++lineNumber;
            const auto tokens = Tokenize(lineText, lineNumber);
            if (tokens.empty()) continue;
            if (!headerSeen)
            {
                RequireCount(tokens, 2u, lineNumber, "kairo-project header");
                if (tokens[0].Text != "kairo-project") throw ProjectFormatError(lineNumber, tokens[0].Column, "expected kairo-project header");
                if (tokens[1].Text == "1") version = 1u;
                else if (tokens[1].Text == "2") { version = 2u; descriptor.BuildProfiles.clear(); }
                else throw ProjectFormatError(lineNumber, tokens[1].Column, "unsupported project version");
                headerSeen = true;
                continue;
            }

            if (tokens[0].Text == "name")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (nameSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate name statement");
                descriptor.Name = tokens[1].Text;
                try { ValidateProjectName(descriptor.Name); }
                catch (const std::exception& error) { throw ProjectFormatError(lineNumber, tokens[1].Column, error.what()); }
                nameSeen = true;
            }
            else if (tokens[0].Text == "assets")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (assetsSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate assets statement");
                descriptor.AssetManifest = ParsePortablePath(tokens[1], lineNumber);
                assetsSeen = true;
            }
            else if (tokens[0].Text == "startup-scene")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (sceneSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate startup-scene statement");
                descriptor.StartupScene = ParsePortablePath(tokens[1], lineNumber);
                sceneSeen = true;
            }
            else if (tokens[0].Text == "engine-version")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (version != 2u) throw ProjectFormatError(lineNumber, tokens[0].Column, "engine-version requires project format 2");
                if (engineSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate engine-version statement");
                descriptor.EngineVersion = tokens[1].Text;
                engineSeen = true;
            }
            else if (tokens[0].Text == "input-map")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (version != 2u) throw ProjectFormatError(lineNumber, tokens[0].Column, "input-map requires project format 2");
                if (inputSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate input-map statement");
                descriptor.InputMap = ParsePortablePath(tokens[1], lineNumber);
                inputSeen = true;
            }
            else if (tokens[0].Text == "rendering-profile")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (version != 2u) throw ProjectFormatError(lineNumber, tokens[0].Column, "rendering-profile requires project format 2");
                if (renderingSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate rendering-profile statement");
                descriptor.RenderingProfile = tokens[1].Text;
                renderingSeen = true;
            }
            else if (tokens[0].Text == "plugin")
            {
                RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
                if (version != 2u) throw ProjectFormatError(lineNumber, tokens[0].Column, "plugin requires project format 2");
                descriptor.EnabledPlugins.push_back(tokens[1].Text);
            }
            else if (tokens[0].Text == "build-profile")
            {
                RequireCount(tokens, 4u, lineNumber, tokens[0].Text);
                if (version != 2u) throw ProjectFormatError(lineNumber, tokens[0].Column, "build-profile requires project format 2");
                ProjectBuildKind kind;
                if (tokens[2].Text == "development") kind = ProjectBuildKind::Development;
                else if (tokens[2].Text == "release") kind = ProjectBuildKind::Release;
                else throw ProjectFormatError(lineNumber, tokens[2].Column, "build profile kind must be development or release");
                descriptor.BuildProfiles.push_back({ tokens[1].Text, kind, ParsePortablePath(tokens[3], lineNumber) });
            }
            else throw ProjectFormatError(lineNumber, tokens[0].Column, "unknown statement '" + tokens[0].Text + "'");
        }

        if (!headerSeen) throw ProjectFormatError(1u, 1u, "missing kairo-project header");
        if (!nameSeen || !assetsSeen || !sceneSeen)
            throw ProjectFormatError(lineNumber + 1u, 1u, "project requires name, assets, and startup-scene statements");
        if (version == 2u && (!engineSeen || !inputSeen || !renderingSeen || descriptor.BuildProfiles.empty()))
            throw ProjectFormatError(lineNumber + 1u, 1u,
                "project format 2 requires engine-version, input-map, rendering-profile, and build-profile statements");
        try { ValidateProjectDescriptor(descriptor); }
        catch (const std::exception& error) { throw ProjectFormatError(lineNumber + 1u, 1u, error.what()); }
        return descriptor;
    }

    /// Output: canonical, deterministic, diff-friendly project descriptor text.
    [[nodiscard]] inline std::string SerializeProjectDescriptor(const ProjectDescriptor& descriptor)
    {
        using namespace project_format_detail;
        ValidateProjectDescriptor(descriptor);
        std::string source = "kairo-project 2\nname " + Quote(descriptor.Name) +
            "\nengine-version " + Quote(descriptor.EngineVersion) + "\nassets " +
            Quote(kairo::assets::NormalizeAssetPath(descriptor.AssetManifest).generic_string()) +
            "\nstartup-scene " + Quote(kairo::assets::NormalizeAssetPath(descriptor.StartupScene).generic_string()) +
            "\ninput-map " + Quote(kairo::assets::NormalizeAssetPath(descriptor.InputMap).generic_string()) +
            "\nrendering-profile " + Quote(descriptor.RenderingProfile) + "\n";
        for (const auto& plugin : descriptor.EnabledPlugins) source += "plugin " + Quote(plugin) + "\n";
        for (const auto& profile : descriptor.BuildProfiles)
            source += "build-profile " + Quote(profile.Name) + " " + std::string(Name(profile.Kind)) + " " +
                Quote(kairo::assets::NormalizeAssetPath(profile.OutputDirectory).generic_string()) + "\n";
        return source;
    }

    [[nodiscard]] inline ProjectDescriptor LoadProjectDescriptor(const std::filesystem::path& path)
    {
        return ParseProjectDescriptor(LoadBoundedTextFile(path,
            project_format_detail::MaxProjectBytes, "Kairo project descriptor"));
    }

    /// Task: flush a same-directory temporary and atomically replace the prior
    /// descriptor, preserving it when serialization or writing fails.
    inline void SaveProjectDescriptor(const std::filesystem::path& path, const ProjectDescriptor& descriptor)
    {
        SaveTextFileAtomically(path, SerializeProjectDescriptor(descriptor), "Kairo project descriptor");
    }
}
