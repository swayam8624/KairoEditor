module;

#include <filesystem>
#include <vector>

export module Kairo.Editor.ProjectLogicBuild;

import Kairo.Assets;
import Kairo.EngineCore;

export namespace kairo::editor
{
    struct BuiltLogicArtifact final
    {
        kairo::assets::AssetID Document;
        std::filesystem::path SourcePath;
        std::filesystem::path ArtifactPath;
    };

    /// The public module keeps only the stable build contract; implementation
    /// lives in a private module unit so consumers do not import compiler-heavy
    /// document and logic backend state through this interface.
    /// Compiles every unique logic document attached to the startup scene.
    /// Incomplete unattached documents remain saveable and are not build
    /// inputs. Any attached graph error aborts the build before publication.
    [[nodiscard]] std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets);

    /// Project-file entry used by KairoHub, CI, and local build profiles.
    [[nodiscard]] std::vector<BuiltLogicArtifact> BuildProjectLogic(
        const std::filesystem::path& projectFile);
}
