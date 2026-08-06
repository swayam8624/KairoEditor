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

export module Kairo.Editor.AssetBrowserModel;

import Kairo.Assets;

export namespace kairo::editor
{
    enum class AssetBrowserState
    {
        Current,
        SourceMissing,
        SourceChanged,
        Unimported,
        Generated
    };

    struct AssetBrowserFilter final
    {
        std::filesystem::path Folder;
        std::string Search;
        std::optional<kairo::assets::AssetType> Type;
        bool IncludeGenerated = true;
    };

    struct AssetBrowserEntry final
    {
        kairo::assets::AssetMetadata Metadata;
        AssetBrowserState State = AssetBrowserState::Generated;
        std::optional<kairo::assets::ImportRecord> Import;
        std::size_t DirectDependentCount = 0u;

        friend bool operator==(const AssetBrowserEntry&, const AssetBrowserEntry&) = default;
    };

    struct AssetDeletePlan final
    {
        kairo::assets::AssetID Asset;
        std::vector<kairo::assets::AssetMetadata> DirectDependents;

        [[nodiscard]] bool CanDelete() const noexcept { return DirectDependents.empty(); }
    };

    namespace asset_browser_detail
    {
        [[nodiscard]] inline std::string Lower(std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (unsigned char character : value)
                result.push_back(static_cast<char>(std::tolower(character)));
            return result;
        }

        [[nodiscard]] inline bool IsInFolder(const std::filesystem::path& path,
            const std::filesystem::path& folder)
        {
            if (folder.empty()) return true;
            const std::string pathKey = kairo::assets::PortableAssetPathKey(path);
            std::string folderKey = kairo::assets::PortableAssetPathKey(folder);
            if (!folderKey.ends_with('/')) folderKey.push_back('/');
            return pathKey == folderKey.substr(0u, folderKey.size() - 1u) ||
                pathKey.starts_with(folderKey);
        }

        [[nodiscard]] inline AssetBrowserState EvaluateState(
            const std::filesystem::path& projectRoot,
            const kairo::assets::AssetMetadata& metadata,
            const kairo::assets::ImportDatabase& imports,
            std::optional<kairo::assets::ImportRecord>& record)
        {
            if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile)
                return AssetBrowserState::Generated;
            record = imports.Find(metadata.ID);
            if (!record.has_value()) return AssetBrowserState::Unimported;
            switch (imports.Evaluate(projectRoot, metadata.ID))
            {
                case kairo::assets::SourceImportState::Current:
                    return AssetBrowserState::Current;
                case kairo::assets::SourceImportState::Missing:
                    return AssetBrowserState::SourceMissing;
                case kairo::assets::SourceImportState::Changed:
                    return AssetBrowserState::SourceChanged;
            }
            throw std::logic_error("Unhandled source import state.");
        }
    }

    [[nodiscard]] inline std::vector<AssetBrowserEntry> BuildAssetBrowserEntries(
        const std::filesystem::path& projectRoot,
        const kairo::assets::AssetRegistry& registry,
        const kairo::assets::ImportDatabase& imports,
        AssetBrowserFilter filter = {})
    {
        if (projectRoot.empty())
            throw std::invalid_argument("Asset browser requires a project root.");
        if (!filter.Folder.empty())
            filter.Folder = kairo::assets::NormalizeAssetPath(filter.Folder);
        const std::string search = asset_browser_detail::Lower(filter.Search);
        const auto snapshot = registry.Snapshot();
        std::vector<AssetBrowserEntry> result;
        result.reserve(snapshot.size());

        for (const kairo::assets::AssetMetadata& metadata : snapshot)
        {
            if (!filter.IncludeGenerated &&
                metadata.Origin != kairo::assets::AssetOrigin::SourceFile) continue;
            if (filter.Type.has_value() && metadata.Type != *filter.Type) continue;
            if (!asset_browser_detail::IsInFolder(metadata.Path, filter.Folder)) continue;
            if (!search.empty())
            {
                const std::string candidate = asset_browser_detail::Lower(
                    metadata.Path.generic_string() + " " + metadata.Importer);
                if (candidate.find(search) == std::string::npos) continue;
            }

            AssetBrowserEntry entry;
            entry.Metadata = metadata;
            entry.State = asset_browser_detail::EvaluateState(
                projectRoot, metadata, imports, entry.Import);
            for (const kairo::assets::AssetMetadata& other : snapshot)
                if (std::ranges::any_of(other.Dependencies,
                    [&metadata](const kairo::assets::AssetReference& dependency)
                    { return dependency.ID == metadata.ID; }))
                    ++entry.DirectDependentCount;
            result.push_back(std::move(entry));
        }
        std::ranges::sort(result, [](const AssetBrowserEntry& left,
            const AssetBrowserEntry& right)
        {
            const std::string leftKey =
                kairo::assets::PortableAssetPathKey(left.Metadata.Path);
            const std::string rightKey =
                kairo::assets::PortableAssetPathKey(right.Metadata.Path);
            if (leftKey != rightKey) return leftKey < rightKey;
            return left.Metadata.ID < right.Metadata.ID;
        });
        return result;
    }

    [[nodiscard]] inline AssetDeletePlan PlanAssetDeletion(
        const kairo::assets::AssetRegistry& registry, kairo::assets::AssetID asset)
    {
        if (!registry.Contains(asset))
            throw std::out_of_range("Cannot plan deletion for an unknown asset.");
        AssetDeletePlan plan;
        plan.Asset = asset;
        for (const kairo::assets::AssetMetadata& metadata : registry.Snapshot())
            if (std::ranges::any_of(metadata.Dependencies,
                [asset](const kairo::assets::AssetReference& dependency)
                { return dependency.ID == asset; }))
                plan.DirectDependents.push_back(metadata);
        std::ranges::sort(plan.DirectDependents, {}, &kairo::assets::AssetMetadata::ID);
        return plan;
    }

    [[nodiscard]] inline std::vector<kairo::assets::AssetMetadata> AssetDependencyClosure(
        const kairo::assets::AssetRegistry& registry, kairo::assets::AssetID root)
    {
        if (!registry.Contains(root))
            throw std::out_of_range("Cannot inspect dependencies for an unknown asset.");
        std::vector<kairo::assets::AssetMetadata> result;
        std::vector<kairo::assets::AssetID> pending{ root };
        std::vector<kairo::assets::AssetID> visited;
        while (!pending.empty())
        {
            const kairo::assets::AssetID current = pending.back();
            pending.pop_back();
            if (std::ranges::find(visited, current) != visited.end()) continue;
            visited.push_back(current);
            const kairo::assets::AssetMetadata metadata = registry.At(current);
            if (current != root) result.push_back(metadata);
            for (const kairo::assets::AssetReference& dependency : metadata.Dependencies)
                pending.push_back(dependency.ID);
        }
        std::ranges::sort(result, {}, &kairo::assets::AssetMetadata::ID);
        return result;
    }
}
