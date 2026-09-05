module;

#include <filesystem>
#include <vector>

export module Kairo.Editor.ProjectLogicBuild;

import Kairo.Assets;
import Kairo.Editor.ProjectDocuments;
import Kairo.EngineCore.Scene;

export namespace kairo::editor
{
    struct BuiltLogicArtifact final
    {
        kairo::assets::AssetID Document;
        std::filesystem::path SourcePath;
        std::filesystem::path ArtifactPath;
    };

    /// Compiles every logic document attached to the current scene before
    /// publishing any portable runtime artifacts. Fails closed on missing,
    /// dirty, wrong-kind, invalid, or dependency-cyclic documents.
    [[nodiscard]] std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets,
        ProjectDocuments& documents);

    [[nodiscard]] std::vector<BuiltLogicArtifact> BuildProjectLogic(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets,
        ProjectDocuments& documents);
}
