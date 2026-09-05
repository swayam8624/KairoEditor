module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module Kairo.Editor.ProjectLogicBuild;

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

namespace kairo::editor
{
    std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets)
    {
        const auto root = CanonicalProjectRoot(projectRoot);
        std::set<kairo::assets::AssetID> documents;
        for (const kairo::engine::Entity entity : scene.Entities())
            if (scene.HasLogic(entity)) documents.insert(scene.Logic(entity).Document.ID);

        const DocumentSchemaRegistry schemas = CreateCoreDocumentSchemaRegistry();
        const LogicDocumentCompiler compiler;

        // Preserve the build boundary: every attached graph is compiled and its
        // runtime payload is validated before publication begins. Only plain
        // bytes and fingerprints cross the Editor/EngineCore ownership boundary;
        // runtime LogicProgram/CompiledLogicArtifact objects remain EngineCore-owned.
        std::vector<BuiltLogicArtifact> pendingRecords;
        std::vector<kairo::assets::AssetFingerprint> pendingFingerprints;
        std::vector<std::vector<std::byte>> pendingPayloads;
        pendingRecords.reserve(documents.size());
        pendingFingerprints.reserve(documents.size());
        pendingPayloads.reserve(documents.size());

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
            DocumentCompileResult result = CompileDocument(document, schemas, compiler);
            if (!result.Succeeded())
            {
                const auto error = std::find_if(result.Diagnostics.begin(), result.Diagnostics.end(),
                    [](const DocumentDiagnostic& diagnostic)
                    { return diagnostic.Severity == DiagnosticSeverity::Error; });
                const std::string detail = error == result.Diagnostics.end()
                    ? "unknown compiler failure" : error->Code + ": " + error->Message;
                throw std::runtime_error("Logic build failed for " + metadata.Path.generic_string() + " (" + detail + ")");
            }

            kairo::engine::ValidateCompiledLogicPayload(result.Artifact->Payload);

            BuiltLogicArtifact record;
            record.Document = id;
            record.SourcePath = sourcePath;
            record.ArtifactPath = kairo::engine::CompiledLogicPath(root, id);
            pendingRecords.push_back(std::move(record));
            pendingFingerprints.push_back(kairo::assets::FingerprintFile(sourcePath));
            pendingPayloads.push_back(std::move(result.Artifact->Payload));
        }

        for (std::size_t index = 0u; index < pendingRecords.size(); ++index)
            kairo::engine::SaveCompiledLogicPayload(
                pendingRecords[index].ArtifactPath,
                pendingRecords[index].Document,
                pendingFingerprints[index],
                pendingPayloads[index]);
        return pendingRecords;
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
