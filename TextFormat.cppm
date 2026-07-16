module;

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.TextFormat;

import Kairo.Assets;

export namespace kairo::editor
{
    struct FormatToken final
    {
        std::string Text;
        std::size_t Column = 1u;
    };

    /// Error must be constructible from (line, column, message). Keeping the
    /// tokenizer error-agnostic lets each public format retain its own located
    /// exception type without duplicating quote/escape parsing.
    template<class Error>
    [[nodiscard]] std::vector<FormatToken> TokenizeFormatLine(
        std::string_view line, std::size_t lineNumber)
    {
        std::vector<FormatToken> tokens;
        std::size_t index = 0u;
        while (index < line.size())
        {
            while (index < line.size() && (line[index] == ' ' || line[index] == '\t' || line[index] == '\r')) ++index;
            if (index == line.size() || line[index] == '#') break;

            FormatToken token;
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
                    if (character == '"') { closed = true; break; }
                    if (character != '\\') { token.Text.push_back(character); continue; }
                    if (index == line.size()) throw Error(lineNumber, index, "unfinished escape sequence");
                    const char escaped = line[index++];
                    switch (escaped)
                    {
                        case '\\': token.Text.push_back('\\'); break;
                        case '"': token.Text.push_back('"'); break;
                        case 'n': token.Text.push_back('\n'); break;
                        case 't': token.Text.push_back('\t'); break;
                        default: throw Error(lineNumber, index, "unknown quoted-string escape");
                    }
                }
                if (!closed) throw Error(lineNumber, token.Column, "unterminated quoted string");
                if (index < line.size() && line[index] != ' ' && line[index] != '\t' &&
                    line[index] != '\r' && line[index] != '#')
                    throw Error(lineNumber, index + 1u, "quoted token must be followed by whitespace");
            }
            tokens.push_back(std::move(token));
        }
        return tokens;
    }

    template<class Error>
    inline void RequireTokenCount(const std::vector<FormatToken>& tokens, std::size_t expected,
        std::size_t line, std::string_view statement)
    {
        if (tokens.size() == expected) return;
        const std::size_t column = tokens.size() > expected ? tokens[expected].Column : 1u;
        throw Error(line, column, std::string(statement) + " expects " +
            std::to_string(expected - 1u) + " argument(s)");
    }

    [[nodiscard]] inline std::string QuoteFormatText(std::string_view value)
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

    [[nodiscard]] inline std::string LoadBoundedTextFile(const std::filesystem::path& path,
        std::size_t maximumBytes, std::string_view role)
    {
        std::error_code error;
        const std::uintmax_t bytes = std::filesystem::file_size(path, error);
        if (error) throw std::runtime_error("Cannot inspect " + std::string(role) + ": " + error.message());
        if (bytes > maximumBytes) throw std::length_error(std::string(role) + " exceeds its safety limit.");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("Cannot open " + std::string(role) + ": " + path.string());
        std::string source(static_cast<std::size_t>(bytes), '\0');
        if (!source.empty() && !input.read(source.data(), static_cast<std::streamsize>(source.size())))
            throw std::runtime_error("Cannot read complete " + std::string(role) + ": " + path.string());
        return source;
    }

    /// Task: preserve the previous destination on serialization/write failure.
    /// The temporary is unique and colocated so the final host rename is one
    /// atomic filesystem operation on supported local filesystems.
    inline void SaveTextFileAtomically(const std::filesystem::path& path,
        std::string_view source, std::string_view role)
    {
        const std::filesystem::path parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("Cannot create " + std::string(role) + " directory: " + error.message());
        const std::filesystem::path temporary = path.string() + ".tmp-" + kairo::assets::GenerateAssetID().ToString();
        try
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("Cannot open temporary " + std::string(role) + ".");
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output) throw std::runtime_error("Cannot write complete temporary " + std::string(role) + ".");
            output.close();
            kairo::assets::ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
