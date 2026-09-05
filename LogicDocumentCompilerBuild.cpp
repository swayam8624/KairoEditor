module;

#include <cstddef>
#include <filesystem>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Kairo.Editor.LogicDocumentCompiler;

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CoreDocumentSchemas;
import Kairo.Editor.DocumentCompiler;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.DocumentTypes;
import Kairo.EngineCore.LogicArtifact;

namespace kairo::editor
{
    std::vector<std::byte> CompileCoreLogicDocumentFile(
        const std::filesystem::path& sourcePath, std::string_view expectedDocumentID)
    {
        if (sourcePath.empty())
            throw std::invalid_argument("Logic compiler requires a source document path.");
        if (expectedDocumentID.empty())
            throw std::invalid_argument("Logic compiler requires an expected document ID.");

        AuthoringDocument document = LoadDocument(sourcePath);
        if (document.ID().ToString() != expectedDocumentID)
            throw std::invalid_argument(
                "Logic document file identity disagrees with its asset metadata: " +
                sourcePath.generic_string());
        if (document.Kind() != DocumentKind::Logic)
            throw std::invalid_argument(
                "Attached document is not a logic graph: " + sourcePath.generic_string());

        const DocumentSchemaRegistry schemas = CreateCoreDocumentSchemaRegistry();
        const LogicDocumentCompiler compiler;
        try
        {
            std::vector<std::byte> payload = CompileDocumentPayloadOrThrow(document, schemas, compiler);
            kairo::engine::ValidateCompiledLogicPayload(payload);
            return payload;
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(
                "Logic build failed for " + sourcePath.generic_string() + " (" + error.what() + ")");
        }
    }
}
