module;

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectLifecycle;

import Kairo.Assets;
import Kairo.Editor.ProjectDescriptor;

export namespace kairo::editor
{
    [[nodiscard]] inline bool IsWithinProjectRoot(
        const std::filesystem::path& root,
        const std::filesystem::path& candidate) noexcept
    {
        auto rootIt = root.begin();
        auto candidateIt = candidate.begin();
        for (; rootIt != root.end(); ++rootIt, ++candidateIt)
            if (candidateIt == candidate.end() || *candidateIt != *rootIt)
                return false;
        return true;
    }

    /// Resolves the executable used by Editor Play. A project-authored
    /// play-executable always wins over a host fallback and is constrained to
    /// the project root. The pure resolution contract is shared with tests so
    /// the UI cannot silently regress to generic KairoPlayer.
    [[nodiscard]] inline std::filesystem::path ResolveProjectRuntimeExecutable(
        const std::filesystem::path& projectRoot,
        const ProjectDescriptor& descriptor,
        const std::optional<std::filesystem::path>& fallback = std::nullopt)
    {
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(projectRoot, error);
        if (error || !std::filesystem::is_directory(root))
            throw std::runtime_error("Cannot resolve project root for Play.");

        if (descriptor.PlayExecutable.has_value())
        {
            const auto candidate = std::filesystem::weakly_canonical(
                root / *descriptor.PlayExecutable, error);
            if (error || !IsWithinProjectRoot(root, candidate))
                throw std::runtime_error(
                    "Project Play executable escapes or cannot be resolved inside the project.");
            if (!std::filesystem::is_regular_file(candidate, error) || error)
                throw std::runtime_error(
                    "Project Play executable is missing. Build the game first: " +
                    candidate.string());
            return candidate;
        }

        if (fallback.has_value())
        {
            const auto candidate = std::filesystem::weakly_canonical(*fallback, error);
            if (!error && std::filesystem::is_regular_file(candidate, error) && !error)
                return candidate;
        }

        throw std::runtime_error(
            "No project Play executable is configured and no runnable KairoPlayer fallback was found.");
    }

    [[nodiscard]] inline std::filesystem::path DefaultRecentProjectsPath()
    {
#if defined(_WIN32)
        if (const char* root = std::getenv("APPDATA")) return std::filesystem::path(root) / "Kairo" / "recent-projects";
#elif defined(__APPLE__)
        if (const char* root = std::getenv("HOME"))
            return std::filesystem::path(root) / "Library" / "Application Support" / "Kairo" / "recent-projects";
#else
        if (const char* root = std::getenv("XDG_CONFIG_HOME"))
            return std::filesystem::path(root) / "kairo" / "recent-projects";
        if (const char* root = std::getenv("HOME"))
            return std::filesystem::path(root) / ".config" / "kairo" / "recent-projects";
#endif
        return {};
    }

    class RecentProjects final
    {
    public:
        static constexpr std::size_t MaximumEntries = 32u;

        void Touch(std::filesystem::path projectFile)
        {
            if (projectFile.empty())
                throw std::invalid_argument("Recent project path cannot be empty.");
            std::error_code error;
            projectFile = std::filesystem::absolute(projectFile, error).lexically_normal();
            if (error)
                throw std::runtime_error("Cannot resolve recent project path: " + error.message());
            std::erase(m_Entries, projectFile);
            m_Entries.insert(m_Entries.begin(), std::move(projectFile));
            if (m_Entries.size() > MaximumEntries) m_Entries.resize(MaximumEntries);
        }

        void Remove(const std::filesystem::path& projectFile)
        { std::erase(m_Entries, projectFile.lexically_normal()); }

        void PruneMissing()
        {
            std::erase_if(m_Entries, [](const std::filesystem::path& path)
            {
                std::error_code error;
                return !std::filesystem::is_regular_file(path, error) || error;
            });
        }

        [[nodiscard]] const std::vector<std::filesystem::path>& Entries() const noexcept
        { return m_Entries; }

        void Save(const std::filesystem::path& path) const
        {
            if (path.empty()) throw std::invalid_argument("Recent-projects path cannot be empty.");
            std::error_code error;
            const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
            std::filesystem::create_directories(parent, error);
            if (error) throw std::runtime_error("Cannot create recent-projects directory: " + error.message());

            const std::filesystem::path temporary = parent /
                (".kairo-recent-" + kairo::assets::GenerateAssetID().ToString());
            try
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Cannot write recent-projects file.");
                output << "kairo-recent-projects 1\n";
                for (const auto& entry : m_Entries)
                    output << std::quoted(entry.generic_string()) << '\n';
                output.flush();
                if (!output) throw std::runtime_error("Cannot flush recent-projects file.");
                output.close();
                if (!output) throw std::runtime_error("Cannot close recent-projects file.");
                kairo::assets::ReplaceFileAtomically(temporary, path);
            }
            catch (...)
            {
                std::filesystem::remove(temporary, error);
                throw;
            }
        }

        [[nodiscard]] static RecentProjects Load(const std::filesystem::path& path)
        {
            RecentProjects result;
            std::ifstream input(path, std::ios::binary);
            if (!input) return result;
            std::string header;
            std::getline(input, header);
            if (header != "kairo-recent-projects 1")
                throw std::invalid_argument("Recent-projects file version is unsupported.");
            std::string value;
            while (input >> std::quoted(value))
            {
                if (result.m_Entries.size() >= MaximumEntries)
                    throw std::length_error("Recent-projects file exceeds its entry limit.");
                result.m_Entries.emplace_back(value);
            }
            if (!input.eof()) throw std::invalid_argument("Recent-projects file is malformed.");
            return result;
        }

    private:
        std::vector<std::filesystem::path> m_Entries;
    };

    /// Copies a validated project into a new sibling staging directory and
    /// publishes it through one directory rename. This powers Save Project As
    /// and template instantiation without exposing partially copied projects.
    inline void CloneProjectDirectory(const std::filesystem::path& sourceProjectFile,
        const std::filesystem::path& destinationDirectory)
    {
        if (sourceProjectFile.empty() || destinationDirectory.empty())
            throw std::invalid_argument("Project clone requires source and destination paths.");
        std::error_code error;
        const std::filesystem::path sourceFile =
            std::filesystem::canonical(sourceProjectFile, error);
        if (error || !std::filesystem::is_regular_file(sourceFile))
            throw std::invalid_argument("Project clone source descriptor does not exist.");
        (void)LoadProjectDescriptor(sourceFile);
        const std::filesystem::path sourceRoot = sourceFile.parent_path();
        const std::filesystem::path destination =
            std::filesystem::absolute(destinationDirectory, error).lexically_normal();
        if (error) throw std::runtime_error("Cannot resolve project destination: " + error.message());
        if (std::filesystem::exists(destination, error))
            throw std::invalid_argument("Project clone destination already exists.");
        if (error) throw std::runtime_error("Cannot inspect project destination: " + error.message());
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) throw std::runtime_error("Cannot create project destination parent: " + error.message());

        const std::filesystem::path staging = destination.parent_path() /
            ("." + destination.filename().string() + ".cloning-" +
             kairo::assets::GenerateAssetID().ToString());
        try
        {
            std::filesystem::copy(sourceRoot, staging,
                std::filesystem::copy_options::recursive |
                std::filesystem::copy_options::copy_symlinks, error);
            if (error) throw std::runtime_error("Cannot copy project template: " + error.message());
            const std::filesystem::path copiedDescriptor = staging / sourceFile.filename();
            (void)LoadProjectDescriptor(copiedDescriptor);
            std::filesystem::rename(staging, destination, error);
            if (error) throw std::runtime_error("Cannot publish cloned project: " + error.message());
        }
        catch (...)
        {
            std::filesystem::remove_all(staging, error);
            throw;
        }
    }
}
