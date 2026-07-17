module;

#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectLogicBuild;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CoreDocumentSchemas;
import Kairo.Editor.DocumentCompiler;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.DocumentValidation;
import Kairo.Editor.LogicDocumentCompiler;
import Kairo.Editor.ProjectDescriptor;
import Kairo.Editor.ProjectPaths;
import Kairo.EngineCore;

export namespace kairo::editor
{
    struct BuiltLogicArtifact final
    {
        kairo::assets::AssetID Document;
        std::filesystem::path SourcePath;
        std::filesystem::path ArtifactPath;
    };

    /// Compiles every unique logic document attached to the startup scene.
    /// Incomplete unattached documents remain saveable and are not build
    /// inputs. Any attached graph error aborts the build before publication.
    [[nodiscard]] inline std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot, const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        const auto root = CanonicalProjectRoot(projectRoot);
        std::set<kairo::assets::AssetID> documents;
        for (const kairo::engine::Entity entity : scene.Entities())
            if (scene.HasLogic(entity)) documents.insert(scene.Logic(entity).Document.ID);

        const DocumentSchemaRegistry schemas = CreateCoreDocumentSchemaRegistry();
        const LogicDocumentCompiler compiler;
        std::vector<std::pair<BuiltLogicArtifact, kairo::engine::CompiledLogicArtifact>> pending;
        pending.reserve(documents.size());
        for (const kairo::assets::AssetID id : documents)
        {
            const auto metadata = assets.Resolve(kairo::assets::DocumentAssetHandle{ id });
            const auto sourcePath = ResolveExistingProjectFile(root, metadata.Path, "logic document");
            const AuthoringDocument document = LoadDocument(sourcePath);
            if (document.ID() != id)
                throw std::invalid_argument("Logic document file identity disagrees with its asset metadata: " +
                    metadata.Path.generic_string());
            if (document.Kind() != DocumentKind::Logic)
                throw std::invalid_argument("Attached document is not a logic graph: " + metadata.Path.generic_string());
            const DocumentCompileResult result = CompileDocument(document, schemas, compiler);
            if (!result.Succeeded())
            {
                const auto error = std::find_if(result.Diagnostics.begin(), result.Diagnostics.end(),
                    [](const DocumentDiagnostic& diagnostic)
                    { return diagnostic.Severity == DiagnosticSeverity::Error; });
                const std::string detail = error == result.Diagnostics.end()
                    ? "unknown compiler failure" : error->Code + ": " + error->Message;
                throw std::runtime_error("Logic build failed for " + metadata.Path.generic_string() + " (" + detail + ")");
            }
            kairo::engine::CompiledLogicArtifact artifact;
            artifact.Source = id;
            artifact.SourceFingerprint = kairo::assets::FingerprintFile(sourcePath);
            artifact.Program = kairo::engine::ParseLogicProgram(result.Artifact->Payload);
            const auto artifactPath = kairo::engine::CompiledLogicPath(root, id);
            pending.push_back({ { id, sourcePath, artifactPath }, std::move(artifact) });
        }

        std::vector<BuiltLogicArtifact> built;
        built.reserve(pending.size());
        for (auto& [record, artifact] : pending)
        {
            kairo::engine::SaveCompiledLogicArtifact(record.ArtifactPath, artifact);
            built.push_back(std::move(record));
        }
        return built;
    }

    /// Project-file entry used by KairoHub, CI, and local build profiles.
    [[nodiscard]] inline std::vector<BuiltLogicArtifact> BuildProjectLogic(
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
