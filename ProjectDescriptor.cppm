module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectDescriptor;

import Kairo.Assets;
import Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    /// Persistent project bootstrap data. Paths are portable and relative to
    /// the directory containing the `.kproject` descriptor.
    struct ProjectDescriptor final
    {
        std::string Name;
        std::filesystem::path AssetManifest = "Assets.kassets";
        std::filesystem::path StartupScene = "Scenes/Main.kscene";

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

        struct Token final
        {
            std::string Text;
            std::size_t Column = 1u;
        };

        inline void ValidateProjectName(std::string_view name)
        {
            ValidateUtf8Text(name, { 1u, MaxProjectNameBytes, false, false }, "Project name");
        }

        [[nodiscard]] inline std::filesystem::path ParsePortablePath(
            const Token& token, std::size_t line)
        {
            try { return kairo::assets::NormalizeAssetPath(token.Text); }
            catch (const std::exception& error) { throw ProjectFormatError(line, token.Column, error.what()); }
        }

        [[nodiscard]] inline std::vector<Token> Tokenize(std::string_view line, std::size_t lineNumber)
        {
            std::vector<Token> tokens;
            std::size_t index = 0u;
            while (index < line.size())
            {
                while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) ++index;
                if (index == line.size() || line[index] == '#') break;

                Token token;
                token.Column = index + 1u;
                if (line[index] != '"')
                {
                    while (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                        line[index] != '\r' && line[index] != '#')
                        token.Text.push_back(line[index++]);
                }
                else
                {
                    ++index;
                    bool closed = false;
                    while (index < line.size())
                    {
                        const char character = line[index++];
                        if (character == '"')
                        {
                            closed = true;
                            break;
                        }
                        if (character != '\\')
                        {
                            token.Text.push_back(character);
                            continue;
                        }
                        if (index == line.size()) throw ProjectFormatError(lineNumber, index, "unfinished escape sequence");
                        const char escaped = line[index++];
                        switch (escaped)
                        {
                            case '\\': token.Text.push_back('\\'); break;
                            case '"': token.Text.push_back('"'); break;
                            case 'n': token.Text.push_back('\n'); break;
                            case 't': token.Text.push_back('\t'); break;
                            default: throw ProjectFormatError(lineNumber, index, "unknown quoted-string escape");
                        }
                    }
                    if (!closed) throw ProjectFormatError(lineNumber, token.Column, "unterminated quoted string");
                    if (index < line.size() && line[index] != ' ' && line[index] != '\t' && line[index] != '\r' && line[index] != '#')
                        throw ProjectFormatError(lineNumber, index + 1u, "quoted token must be followed by whitespace");
                }
                tokens.push_back(std::move(token));
            }
            return tokens;
        }

        inline void RequireCount(const std::vector<Token>& tokens, std::size_t expected,
            std::size_t line, std::string_view statement)
        {
            if (tokens.size() == expected) return;
            const std::size_t column = tokens.size() > expected ? tokens[expected].Column : 1u;
            throw ProjectFormatError(line, column, std::string(statement) + " expects " +
                std::to_string(expected - 1u) + " argument(s)");
        }

        [[nodiscard]] inline std::string Quote(std::string_view value)
        {
            std::string result = "\"";
            for (const char character : value)
            {
                switch (character)
                {
                    case '\\': result += "\\\\"; break;
                    case '"': result += "\\\""; break;
                    case '\n': result += "\\n"; break;
                    case '\t': result += "\\t"; break;
                    default: result.push_back(character); break;
                }
            }
            result.push_back('"');
            return result;
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
        if (assets == scene)
            throw std::invalid_argument("Asset manifest and startup scene paths must be different.");
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
        bool nameSeen = false;
        bool assetsSeen = false;
        bool sceneSeen = false;
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
                if (tokens[1].Text != "1") throw ProjectFormatError(lineNumber, tokens[1].Column, "unsupported project version");
                headerSeen = true;
                continue;
            }

            RequireCount(tokens, 2u, lineNumber, tokens[0].Text);
            if (tokens[0].Text == "name")
            {
                if (nameSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate name statement");
                descriptor.Name = tokens[1].Text;
                try { ValidateProjectName(descriptor.Name); }
                catch (const std::exception& error) { throw ProjectFormatError(lineNumber, tokens[1].Column, error.what()); }
                nameSeen = true;
            }
            else if (tokens[0].Text == "assets")
            {
                if (assetsSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate assets statement");
                descriptor.AssetManifest = ParsePortablePath(tokens[1], lineNumber);
                assetsSeen = true;
            }
            else if (tokens[0].Text == "startup-scene")
            {
                if (sceneSeen) throw ProjectFormatError(lineNumber, tokens[0].Column, "duplicate startup-scene statement");
                descriptor.StartupScene = ParsePortablePath(tokens[1], lineNumber);
                sceneSeen = true;
            }
            else throw ProjectFormatError(lineNumber, tokens[0].Column, "unknown statement '" + tokens[0].Text + "'");
        }

        if (!headerSeen) throw ProjectFormatError(1u, 1u, "missing kairo-project header");
        if (!nameSeen || !assetsSeen || !sceneSeen)
            throw ProjectFormatError(lineNumber + 1u, 1u, "project requires name, assets, and startup-scene statements");
        try { ValidateProjectDescriptor(descriptor); }
        catch (const std::exception& error) { throw ProjectFormatError(lineNumber + 1u, 1u, error.what()); }
        return descriptor;
    }

    /// Output: canonical, deterministic, diff-friendly project descriptor text.
    [[nodiscard]] inline std::string SerializeProjectDescriptor(const ProjectDescriptor& descriptor)
    {
        using namespace project_format_detail;
        ValidateProjectDescriptor(descriptor);
        return "kairo-project 1\nname " + Quote(descriptor.Name) + "\nassets " +
            Quote(kairo::assets::NormalizeAssetPath(descriptor.AssetManifest).generic_string()) +
            "\nstartup-scene " + Quote(kairo::assets::NormalizeAssetPath(descriptor.StartupScene).generic_string()) + "\n";
    }

    [[nodiscard]] inline ProjectDescriptor LoadProjectDescriptor(const std::filesystem::path& path)
    {
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(path, error);
        if (error) throw std::runtime_error("Cannot inspect Kairo project descriptor: " + error.message());
        if (bytes > project_format_detail::MaxProjectBytes)
            throw std::length_error("Kairo project descriptor exceeds the 1 MiB safety limit.");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open Kairo project descriptor: " + path.string());
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error("Cannot read complete Kairo project descriptor: " + path.string());
        return ParseProjectDescriptor(source);
    }

    /// Task: flush a same-directory temporary and atomically replace the prior
    /// descriptor, preserving it when serialization or writing fails.
    inline void SaveProjectDescriptor(const std::filesystem::path& path, const ProjectDescriptor& descriptor)
    {
        const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("Cannot create project descriptor directory: " + error.message());
        const auto temporary = std::filesystem::path(path.string() + ".tmp-" + kairo::assets::GenerateAssetID().ToString());
        try
        {
            const std::string source = SerializeProjectDescriptor(descriptor);
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open temporary project descriptor.");
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write complete project descriptor.");
            output.close();
            if (std::rename(temporary.string().c_str(), path.string().c_str()) != 0)
                throw std::runtime_error("Cannot atomically replace Kairo project descriptor.");
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
