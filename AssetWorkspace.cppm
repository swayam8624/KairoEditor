module;

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.AssetWorkspace;

import Kairo.Assets.AssetID;
import Kairo.Assets.ImportDatabase;
import Kairo.Assets.Metadata;
import Kairo.Assets.Registry;
import Kairo.Assets.Types;

export namespace kairo::editor
{
    enum class AssetWorkspaceStatus
    {
        Current,
        Changed,
        MissingSource,
        Generated,
        Builtin,
        Unknown
    };

    [[nodiscard]] constexpr std::string_view NameOfAssetWorkspaceStatus(
        AssetWorkspaceStatus status) noexcept
    {
        switch (status)
        {
            case AssetWorkspaceStatus::Current: return "Current";
            case AssetWorkspaceStatus::Changed: return "Changed";
            case AssetWorkspaceStatus::MissingSource: return "Missing";
            case AssetWorkspaceStatus::Generated: return "Generated";
            case AssetWorkspaceStatus::Builtin: return "Builtin";
            case AssetWorkspaceStatus::Unknown: return "Untracked";
        }
        return "Untracked";
    }

    struct AssetWorkspaceEntry final
    {
        kairo::assets::AssetMetadata Metadata;
        AssetWorkspaceStatus Status = AssetWorkspaceStatus::Unknown;
        std::optional<kairo::assets::ImportRecord> Import;
        std::vector<kairo::assets::AssetReference> Dependents;

        [[nodiscard]] bool CanDelete() const noexcept { return Dependents.empty(); }
    };

    struct AssetWorkspaceFilter final
    {
        std::optional<kairo::assets::AssetType> Type;
        std::string Search;
        bool IncludeCurrent = true;
        bool IncludeChanged = true;
        bool IncludeMissing = true;
        bool IncludeGenerated = true;
        bool IncludeBuiltin = true;
        bool IncludeUnknown = true;
    };

    [[nodiscard]] inline AssetWorkspaceStatus StatusForAsset(
        const std::filesystem::path& projectRoot,
        const kairo::assets::AssetMetadata& metadata,
        const kairo::assets::ImportDatabase& imports)
    {
        using namespace kairo::assets;
        switch (metadata.Origin)
        {
            case AssetOrigin::Generated: return AssetWorkspaceStatus::Generated;
            case AssetOrigin::Builtin: return AssetWorkspaceStatus::Builtin;
            case AssetOrigin::SourceFile:
            {
                if (!imports.Find(metadata.ID).has_value()) return AssetWorkspaceStatus::Unknown;
                switch (imports.Evaluate(projectRoot, metadata.ID))
                {
                    case SourceImportState::Current: return AssetWorkspaceStatus::Current;
                    case SourceImportState::Changed: return AssetWorkspaceStatus::Changed;
                    case SourceImportState::Missing: return AssetWorkspaceStatus::MissingSource;
                }
            }
        }
        return AssetWorkspaceStatus::Unknown;
    }

    /// Read-only backing model shared by the content browser, dependency view,
    /// safe-delete dialog, import status badges, and future thumbnail scheduler.
    /// The snapshot owns all values and remains valid across registry mutation.
    class AssetWorkspace final
    {
    public:
        [[nodiscard]] static AssetWorkspace Build(const std::filesystem::path& projectRoot,
            const kairo::assets::AssetRegistry& registry,
            const kairo::assets::ImportDatabase& imports)
        {
            if (projectRoot.empty()) throw std::invalid_argument("Asset workspace requires a project root.");
            AssetWorkspace workspace;
            const std::vector<kairo::assets::AssetMetadata> metadata = registry.Snapshot();
            workspace.m_Entries.reserve(metadata.size());
            for (const auto& asset : metadata)
            {
                AssetWorkspaceEntry entry;
                entry.Metadata = asset;
                entry.Import = imports.Find(asset.ID);
                entry.Status = StatusForAsset(projectRoot, asset, imports);
                for (const auto& candidate : metadata)
                {
                    for (const auto& dependency : candidate.Dependencies)
                    {
                        if (dependency.ID == asset.ID)
                            entry.Dependents.push_back({ candidate.ID, candidate.Type });
                    }
                }
                std::ranges::sort(entry.Dependents, {}, &kairo::assets::AssetReference::ID);
                workspace.m_Entries.push_back(std::move(entry));
            }
            std::ranges::sort(workspace.m_Entries, [](const auto& left, const auto& right) {
                const std::string leftPath = left.Metadata.Path.generic_string();
                const std::string rightPath = right.Metadata.Path.generic_string();
                if (leftPath != rightPath) return leftPath < rightPath;
                return left.Metadata.ID < right.Metadata.ID;
            });
            return workspace;
        }

        [[nodiscard]] const std::vector<AssetWorkspaceEntry>& Entries() const noexcept
        {
            return m_Entries;
        }

        [[nodiscard]] const AssetWorkspaceEntry& At(kairo::assets::AssetID id) const
        {
            const auto found = std::ranges::find(m_Entries, id,
                [](const AssetWorkspaceEntry& entry) { return entry.Metadata.ID; });
            if (found == m_Entries.end()) throw std::out_of_range("Asset workspace does not contain this asset.");
            return *found;
        }

        [[nodiscard]] std::vector<AssetWorkspaceEntry> Filter(const AssetWorkspaceFilter& filter) const
        {
            std::vector<AssetWorkspaceEntry> result;
            std::string needle = filter.Search;
            std::ranges::transform(needle, needle.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            for (const AssetWorkspaceEntry& entry : m_Entries)
            {
                if (filter.Type.has_value() && entry.Metadata.Type != *filter.Type) continue;
                if (!VisibleStatus(entry.Status, filter)) continue;
                if (!needle.empty())
                {
                    std::string path = entry.Metadata.Path.generic_string();
                    std::ranges::transform(path, path.begin(), [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    });
                    if (path.find(needle) == std::string::npos) continue;
                }
                result.push_back(entry);
            }
            return result;
        }

        [[nodiscard]] std::vector<kairo::assets::AssetReference> DeleteBlockers(
            kairo::assets::AssetID id) const
        {
            return At(id).Dependents;
        }

    private:
        std::vector<AssetWorkspaceEntry> m_Entries;

        [[nodiscard]] static bool VisibleStatus(
            AssetWorkspaceStatus status, const AssetWorkspaceFilter& filter) noexcept
        {
            switch (status)
            {
                case AssetWorkspaceStatus::Current: return filter.IncludeCurrent;
                case AssetWorkspaceStatus::Changed: return filter.IncludeChanged;
                case AssetWorkspaceStatus::MissingSource: return filter.IncludeMissing;
                case AssetWorkspaceStatus::Generated: return filter.IncludeGenerated;
                case AssetWorkspaceStatus::Builtin: return filter.IncludeBuiltin;
                case AssetWorkspaceStatus::Unknown: return filter.IncludeUnknown;
            }
            return false;
        }
    };
}
