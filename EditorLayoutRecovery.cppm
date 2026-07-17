module;

#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

export module Kairo.Editor.LayoutRecovery;

export namespace kairo::editor
{
    // V2 adds the Kairo AI dock to the default right-side tool stack. Rebuild
    // V1 layouts once so the new panel cannot appear undocked over the viewport.
    inline constexpr std::uint32_t CurrentEditorLayoutVersion = 2u;

    /// Describes why the native shell should either restore an ImGui dock tree
    /// or construct the known-good default. The policy is independent from
    /// ImGui so recovery behavior can be tested without opening a window.
    enum class EditorLayoutDisposition : std::uint8_t
    {
        PersistenceDisabled,
        RestoreCompatible,
        RebuildMissing,
        RebuildInvalidVersion,
        RebuildObsoleteVersion
    };

    struct EditorLayoutPlan final
    {
        EditorLayoutDisposition Disposition = EditorLayoutDisposition::PersistenceDisabled;
        std::filesystem::path LayoutFile;
        std::filesystem::path VersionFile;
        std::optional<std::filesystem::path> PreservedLayout;

        [[nodiscard]] bool ShouldRebuild() const noexcept
        {
            return Disposition != EditorLayoutDisposition::RestoreCompatible;
        }
    };

    namespace detail
    {
        [[nodiscard]] inline std::filesystem::path VersionPath(const std::filesystem::path& layoutFile)
        {
            return std::filesystem::path(layoutFile.string() + ".version");
        }

        [[nodiscard]] inline std::optional<std::uint32_t> ReadVersion(const std::filesystem::path& path)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) return std::nullopt;
            std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            std::uint32_t value = 0u;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size()) return std::nullopt;
            return value;
        }

        inline void WriteVersionAtomically(const std::filesystem::path& path, std::uint32_t version)
        {
            const std::filesystem::path temporary(path.string() + ".tmp");
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Cannot create editor layout version file: " + temporary.string());
                output << version << '\n';
                output.flush();
                if (!output) throw std::runtime_error("Cannot write editor layout version file: " + temporary.string());
            }
            std::error_code error;
            std::filesystem::rename(temporary, path, error);
            if (!error) return;
            std::filesystem::remove(path, error);
            error.clear();
            std::filesystem::rename(temporary, path, error);
            if (error)
            {
                std::filesystem::remove(temporary);
                throw std::runtime_error("Cannot publish editor layout version file: " + error.message());
            }
        }

        [[nodiscard]] inline std::optional<std::filesystem::path> PreserveLayout(
            const std::filesystem::path& layoutFile, std::string_view reason)
        {
            if (!std::filesystem::exists(layoutFile)) return std::nullopt;
            for (std::uint32_t suffix = 0u; suffix < 10'000u; ++suffix)
            {
                std::filesystem::path candidate(layoutFile.string() + "." + std::string(reason));
                if (suffix != 0u) candidate += "." + std::to_string(suffix);
                candidate += ".bak";
                if (std::filesystem::exists(candidate)) continue;
                std::error_code error;
                std::filesystem::rename(layoutFile, candidate, error);
                if (error) throw std::runtime_error("Cannot preserve obsolete editor layout: " + error.message());
                return candidate;
            }
            throw std::runtime_error("Cannot preserve editor layout: backup limit reached.");
        }
    }

    /// Input: optional ImGui INI path and the layout schema understood by the
    /// current editor executable.
    /// Output: restore/rebuild decision plus any non-destructive backup path.
    /// Task: prevent obsolete or corrupt dock layouts from hiding the viewport
    /// while retaining compatible user customization across launches.
    [[nodiscard]] inline EditorLayoutPlan PrepareEditorLayout(
        const std::filesystem::path& layoutFile,
        std::uint32_t expectedVersion = CurrentEditorLayoutVersion)
    {
        if (layoutFile.empty()) return {};
        if (expectedVersion == 0u) throw std::invalid_argument("Editor layout version must be positive.");

        std::error_code error;
        if (!layoutFile.parent_path().empty())
        {
            std::filesystem::create_directories(layoutFile.parent_path(), error);
            if (error) throw std::runtime_error("Cannot create editor layout directory: " + error.message());
        }

        EditorLayoutPlan plan;
        plan.LayoutFile = layoutFile;
        plan.VersionFile = detail::VersionPath(layoutFile);
        const bool hasLayout = std::filesystem::exists(layoutFile);
        const auto storedVersion = detail::ReadVersion(plan.VersionFile);
        if (hasLayout && storedVersion == expectedVersion)
        {
            plan.Disposition = EditorLayoutDisposition::RestoreCompatible;
            return plan;
        }

        if (!hasLayout)
            plan.Disposition = EditorLayoutDisposition::RebuildMissing;
        else if (!storedVersion.has_value())
        {
            plan.Disposition = EditorLayoutDisposition::RebuildInvalidVersion;
            plan.PreservedLayout = detail::PreserveLayout(layoutFile, "invalid");
        }
        else
        {
            plan.Disposition = EditorLayoutDisposition::RebuildObsoleteVersion;
            plan.PreservedLayout = detail::PreserveLayout(layoutFile, "v" + std::to_string(*storedVersion));
        }
        detail::WriteVersionAtomically(plan.VersionFile, expectedVersion);
        return plan;
    }
}
