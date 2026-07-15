module;

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

export module Kairo.Editor.ProjectPaths;

import Kairo.Assets;

export namespace kairo::editor
{
    /// Output: true when candidate is root itself or one of its descendants.
    /// Preconditions: both paths are absolute and lexically/canonically
    /// normalized. Component comparison avoids unsafe string-prefix tests such
    /// as treating `/Game2` as a child of `/Game`.
    [[nodiscard]] inline bool IsWithinProjectRoot(const std::filesystem::path& root,
        const std::filesystem::path& candidate) noexcept
    {
        auto rootPart = root.begin();
        auto candidatePart = candidate.begin();
        for (; rootPart != root.end(); ++rootPart, ++candidatePart)
            if (candidatePart == candidate.end() || *rootPart != *candidatePart) return false;
        return true;
    }

    [[nodiscard]] inline std::filesystem::path CanonicalExistingFile(
        const std::filesystem::path& path, std::string_view role)
    {
        if (path.empty()) throw std::invalid_argument(std::string(role) + " path cannot be empty.");
        std::error_code error;
        const auto canonical = std::filesystem::canonical(path, error);
        if (error) throw std::runtime_error("Cannot resolve " + std::string(role) + ": " + error.message());
        if (!std::filesystem::is_regular_file(canonical, error) || error)
            throw std::runtime_error(std::string(role) + " is not a regular file.");
        return canonical;
    }

    [[nodiscard]] inline std::filesystem::path CanonicalProjectRoot(const std::filesystem::path& root)
    {
        if (root.empty()) throw std::invalid_argument("Project root cannot be empty.");
        std::error_code error;
        const auto canonical = std::filesystem::canonical(root, error);
        if (error) throw std::runtime_error("Cannot resolve project root: " + error.message());
        if (!std::filesystem::is_directory(canonical, error) || error)
            throw std::runtime_error("Project root is not a directory.");
        return canonical;
    }

    /// Resolves an existing project-relative file and rejects symlink escapes
    /// after canonicalization. NormalizeAssetPath rejects absolute paths and
    /// parent traversal before filesystem access.
    [[nodiscard]] inline std::filesystem::path ResolveExistingProjectFile(
        const std::filesystem::path& root, const std::filesystem::path& relative,
        std::string_view role)
    {
        const auto normalized = kairo::assets::NormalizeAssetPath(relative);
        const auto canonical = CanonicalExistingFile(root / normalized, role);
        if (!IsWithinProjectRoot(root, canonical))
            throw std::invalid_argument(std::string(role) + " escapes the project root.");
        return canonical;
    }

    /// Resolves a future project-relative output through all existing path
    /// components. This catches symlink escapes while permitting missing final
    /// directories that the atomic writer will create.
    [[nodiscard]] inline std::filesystem::path ResolveProjectOutput(
        const std::filesystem::path& root, const std::filesystem::path& relative)
    {
        const auto normalized = kairo::assets::NormalizeAssetPath(relative);
        std::error_code error;
        const auto weak = std::filesystem::weakly_canonical(root / normalized, error);
        if (error) throw std::runtime_error("Cannot resolve project output path: " + error.message());
        if (!IsWithinProjectRoot(root, weak))
            throw std::invalid_argument("Project output path escapes the project root.");
        return weak;
    }
}
