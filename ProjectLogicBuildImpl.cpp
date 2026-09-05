module;

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <set>
#include <span>
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
    namespace project_logic_build_detail
    {
        inline void ValidateRuntimePayload(std::span<const std::byte> payload)
        {
            (void)kairo::engine::ParseLogicProgram(payload);
        }

        inline void PublishRuntimeArtifact(const BuiltLogicArtifact& record,
            const kairo::assets::AssetFingerprint& fingerprint,
            std::span<const std::byte> payload)
        {
            // Keep the EngineCore-owned artifact type inside this narrow helper.
            // MSVC 19.44 ICEs when a container of this imported module type is
            // destroyed inside the larger project-build function.
            kairo::engine::CompiledLogicArtifact artifact;
            artifact.Source = record.Document;
            artifact.SourceFingerprint = fingerprint;
            artifact.Program = kairo::engine::ParseLogicProgram(payload);
            kairo::engine::SaveCompiledLogicArtifact(record.ArtifactPath, artifact);
        }
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

        const DocumentSchemaRegistry schemas = CreateCoreDocumentSchemaRegistry();
        const LogicDocumentCompiler compiler;

        // Preserve the build boundary: every attached graph is compiled and its
        // runtime payload is parsed before publication begins. Stage only plain
        // byte/fingerprint values here; the EngineCore artifact object is
        // materialized one-at-a-time in the narrow publication helper above.
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

            project_logic_build_detail::ValidateRuntimePayload(result.Artifact->Payload);

            BuiltLogicArtifact record;
            record.Document = id;
            record.SourcePath = sourcePath;
            record.ArtifactPath = kairo::engine::CompiledLogicPath(root, id);
            pendingRecords.push_back(std::move(record));
            pendingFingerprints.push_back(kairo::assets::FingerprintFile(sourcePath));
            pendingPayloads.push_back(std::move(result.Artifact->Payload));
        }

        for (std::size_t index = 0u; index < pendingRecords.size(); ++index)
            project_logic_build_detail::PublishRuntimeArtifact(
                pendingRecords[index], pendingFingerprints[index], pendingPayloads[index]);
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
