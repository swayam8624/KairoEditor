module;

#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module Kairo.Editor.ProjectLogicBuild;

import Kairo.Assets;
import Kairo.Editor.CoreDocumentSchemas;
import Kairo.Editor.DocumentCompiler;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.LogicDocumentCompiler;
import Kairo.Editor.ProjectDocuments;
import Kairo.EngineCore.LogicArtifact;
import Kairo.EngineCore.Scene;

namespace kairo::editor
{
    std::vector<BuiltLogicArtifact> BuildAttachedLogicArtifacts(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets,
        ProjectDocuments& documents)
    {
        if (projectRoot.empty())
            throw std::invalid_argument("Attached logic build requires a project root.");

        std::set<kairo::assets::AssetID> attached;
        for (const kairo::engine::Entity entity : scene.Entities())
            if (const auto logic = scene.Logic(entity); logic.has_value())
                attached.insert(logic->DocumentAsset);
        if (attached.empty()) return {};

        DocumentSchemaRegistry schemas = BuildCoreDocumentSchemas();
        DocumentCompilerRegistry compilers;
        compilers.Register(MakeLogicDocumentCompiler());

        std::vector<std::pair<BuiltLogicArtifact, kairo::engine::CompiledLogicArtifact>> pending;
        pending.reserve(attached.size());

        for (const kairo::assets::AssetID id : attached)
        {
            const auto metadata = assets.Find(id);
            if (!metadata.has_value())
                throw std::runtime_error("Scene references an unregistered logic document asset.");
            if (metadata->Type != kairo::assets::AssetType::Document)
                throw std::runtime_error("Scene logic attachment does not reference a document asset.");
            const auto kind = DocumentKindFromPath(metadata->Path);
            if (!kind.has_value() || *kind != DocumentKind::Logic)
                throw std::runtime_error("Scene logic attachment is not a .klogic document.");

            bool openedHere = false;
            if (!documents.Contains(id))
            {
                documents.Open(metadata->Path, id);
                openedHere = true;
            }

            try
            {
                const AuthoringDocument& document = documents.Get(id);
                if (document.IsDirty())
                    throw std::runtime_error("Attached logic document must be saved before build.");
                const DocumentCompileResult result = CompileDocument(document, schemas, compilers);
                if (!result.Succeeded() || !result.Artifact.has_value())
                    throw std::runtime_error("Attached logic document failed compilation.");
                const kairo::engine::LogicProgram program =
                    kairo::engine::ParseLogicProgram(result.Artifact->Payload);
                kairo::engine::CompiledLogicArtifact artifact;
                artifact.Source = id;
                artifact.SourceFingerprint = kairo::assets::FingerprintFile(projectRoot / metadata->Path);
                artifact.Program = program;
                const std::filesystem::path artifactPath =
                    kairo::engine::CompiledLogicArtifactPath(projectRoot, id);
                pending.push_back({ { id, metadata->Path, artifactPath }, std::move(artifact) });
                if (openedHere) documents.Close(id);
            }
            catch (...)
            {
                if (openedHere && documents.Contains(id))
                    documents.Close(id);
                throw;
            }
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

    std::vector<BuiltLogicArtifact> BuildProjectLogic(
        const std::filesystem::path& projectRoot,
        const kairo::engine::Scene& scene,
        const kairo::assets::AssetRegistry& assets,
        ProjectDocuments& documents)
    {
        return BuildAttachedLogicArtifacts(projectRoot, scene, assets, documents);
    }
}
