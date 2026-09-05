module;

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Keep this file as an implementation unit of the compiler module. MSVC 19.44
// ICEs while synthesizing destruction for the imported schema/compiler object
// graph. Core compiler services are immutable and intentionally process-lived;
// build calls borrow them and own only their authored document/runtime bytes.
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
        struct CoreCompilerServices final
        {
            DocumentSchemaRegistry Schemas = CreateCoreDocumentSchemaRegistry();
            LogicDocumentCompiler Compiler;
        };

        [[nodiscard]] const CoreCompilerServices& Services()
        {
            // Deliberately process-lifetime. Avoiding static destruction also
            // avoids MSVC 19.44's symbols.c ICE without changing build results.
            static const CoreCompilerServices* services = new CoreCompilerServices{};
            return *services;
        }

        [[nodiscard]] std::vector<std::byte> ValidateRuntimePayload(
            std::vector<std::byte> payload)
        {
            kairo::engine::ValidateCompiledLogicPayload(payload);
            return payload;
        }

        [[nodiscard]] std::vector<std::byte> CompileLoadedDocument(
            const AuthoringDocument& document)
        {
            const CoreCompilerServices& services = Services();
            return ValidateRuntimePayload(CompileDocumentPayloadOrThrow(
                document, services.Schemas, services.Compiler));
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
