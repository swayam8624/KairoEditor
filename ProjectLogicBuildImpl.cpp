module;

#include <cstddef>
#include <filesystem>
#include <set>
#include <utility>
#include <vector>

module Kairo.Editor.ProjectLogicBuild;

import Kairo.Assets;
import Kairo.Editor.LogicDocumentCompiler;
import Kairo.Editor.ProjectDescriptor;
import Kairo.Editor.ProjectPaths;
import Kairo.EngineCore;

namespace kairo::editor
{
    namespace project_logic_build_detail
    {
        struct PendingLogicArtifact final
        {
            BuiltLogicArtifact Record;
            kairo::assets::AssetFingerprint Fingerprint;
            std::vector<std::byte> Payload;
        };
    }

    std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        const auto root = CanonicalProjectRoot(projectRoot);
        std::set<kairo::assets::AssetID> documents;
        for (const kairo::engine::Entity entity : scene.Entities())
            if (scene.HasLogic(entity)) documents.insert(scene.Logic(entity).Document.ID);

        // Stage every payload and exact source fingerprint before publication.
        // All document/compiler lifetimes stay behind LogicDocumentCompiler's
        // payload-only facade; no generic compiler contract is imported here.
        // This unit owns only stable IDs, paths, fingerprints, and runtime bytes.
        std::vector<project_logic_build_detail::PendingLogicArtifact> pending;
        pending.reserve(documents.size());

        for (const kairo::assets::AssetID id : documents)
        {
            const auto metadata = assets.Resolve(kairo::assets::DocumentAssetHandle{ id });
            const auto sourcePath = ResolveExistingProjectFile(root, metadata.Path, "logic document");

            project_logic_build_detail::PendingLogicArtifact staged;
            staged.Record.Document = id;
            staged.Record.SourcePath = sourcePath;
            staged.Record.ArtifactPath = kairo::engine::CompiledLogicPath(root, id);
            staged.Payload = CompileCoreLogicDocumentFile(sourcePath, id.ToString());
            staged.Fingerprint = kairo::assets::FingerprintFile(sourcePath);
            pending.push_back(std::move(staged));
        }

        // Publication starts only after the complete batch compiled and parsed.
        // A failed graph therefore cannot leave a partially rebuilt project.
        for (const auto& staged : pending)
            kairo::engine::SaveCompiledLogicPayload(
                staged.Record.ArtifactPath,
                staged.Record.Document,
                staged.Fingerprint,
                staged.Payload);

        std::vector<BuiltLogicArtifact> built;
        built.reserve(pending.size());
        for (auto& staged : pending) built.push_back(std::move(staged.Record));
        return built;
    }

    std::vector<BuiltLogicArtifact> BuildProjectLogic(
        const std::filesystem::path& projectFile)
    {
        const auto descriptorPath = CanonicalExistingFile(projectFile, "project descriptor");
        const auto root = descriptorPath.parent_path();
        const ProjectDescriptor descriptor = LoadProjectDescriptor(descriptorPath);
        kairo::assets::AssetRegistry assets;
        kairo::assets::LoadAssetManifest(
            ResolveExistingProjectFile(root, descriptor.AssetManifest, "asset manifest"), assets);
        kairo::engine::Scene scene;
        kairo::engine::LoadScene(
            ResolveExistingProjectFile(root, descriptor.StartupScene, "startup scene"), assets, scene);
        return BuildAttachedLogicArtifacts(root, scene, assets);
    }
}
