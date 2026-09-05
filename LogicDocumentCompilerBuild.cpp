module;

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Keep this file as an implementation unit of the compiler module. MSVC 19.44
// cannot compile one function that simultaneously owns the loaded document,
// schema registry, concrete compiler, payload, and exception teardown graph.
// The two-stage boundary below keeps those lifetimes in separate functions
// without changing any runtime validation or bytecode semantics.
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
    namespace logic_document_compiler_detail
    {
        [[nodiscard]] std::vector<std::byte> CompileLoadedDocument(
            const AuthoringDocument& document)
        {
            const DocumentSchemaRegistry schemas = CreateCoreDocumentSchemaRegistry();
            const LogicDocumentCompiler compiler;
            std::vector<std::byte> payload =
                CompileDocumentPayloadOrThrow(document, schemas, compiler);
            kairo::engine::ValidateCompiledLogicPayload(payload);
            return payload;
        }
    }

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

        return logic_document_compiler_detail::CompileLoadedDocument(document);
    }
}
