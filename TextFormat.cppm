module;

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

export module Kairo.Editor.TextFormat;
export import Kairo.EngineCore.TextFormat;

import Kairo.Assets;

export namespace kairo::editor
{
    using kairo::engine::FormatToken;
    using kairo::engine::TokenizeFormatLine;
    using kairo::engine::RequireTokenCount;
    using kairo::engine::QuoteFormatText;
    using kairo::engine::LoadBoundedTextFile;

    /// Editor-local atomic text writer.
    ///
    /// Keep the temporary in the destination directory, but do not append the
    /// generated ID to the complete destination path. Deep recovery staging
    /// paths can otherwise exceed the legacy Win32 path budget even when the
    /// final authored path itself is valid. The stream is explicitly closed
    /// before publication so Windows never has to replace an open file.
    inline void SaveTextFileAtomically(const std::filesystem::path& path,
        std::string_view source, std::string_view role)
    {
        const std::filesystem::path parent = path.has_parent_path()
            ? path.parent_path() : std::filesystem::path(".");
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error)
            throw std::runtime_error("Cannot create " + std::string(role) +
                " directory: " + error.message());

        const std::filesystem::path temporary = parent /
            (".kairo-tmp-" + kairo::assets::GenerateAssetID().ToString());
        try
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("Cannot open temporary " + std::string(role) + ".");
            output.write(source.data(), static_cast<std::streamsize>(source.size()));
            output.flush();
            if (!output)
                throw std::runtime_error("Cannot write complete temporary " + std::string(role) + ".");
            output.close();
            if (!output)
                throw std::runtime_error("Cannot close temporary " + std::string(role) + ".");
            kairo::assets::ReplaceFileAtomically(temporary, path);
        }
        catch (...)
        {
            std::filesystem::remove(temporary, error);
            throw;
        }
    }
}
