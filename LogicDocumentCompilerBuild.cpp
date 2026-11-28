module;

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <optional>
#include <exception>

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
import Kairo.Editor.DocumentValidation;
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

    std::vector<DocumentDiagnostic> CheckCoreLogicDocument(const AuthoringDocument& document)
    {
        const auto& services=logic_document_compiler_detail::Services();
        return CheckDocumentWithRuntimeValidator(document, services.Schemas, services.Compiler,
            [](const std::vector<std::byte>& payload)
            {
                kairo::engine::ValidateCompiledLogicPayload(payload);
            });
    }

    void InitializeLogicStarter(AuthoringDocument& document)
    {
        if (document.Kind()!=DocumentKind::Logic || document.NodeCount()!=0u)
            throw std::invalid_argument("A starter requires a fresh empty logic document.");
        const auto& schemas=logic_document_compiler_detail::Services().Schemas;
        // Construct a candidate so any error leaves the caller's graph intact.
        AuthoringDocument candidate(document); // retain reserved ID cursors
        const auto begin=candidate.AddNode(schemas.Require("kairo.logic.event-begin-play"),{0.0,0.0});
        const auto print=candidate.AddNode(schemas.Require("kairo.logic.print"),{260.0,0.0});
        const auto pin=[&](NodeID node,std::string_view key)
        {
            for (const auto& p:candidate.Node(node).Pins) if (p.Key==key) return p.ID;
            throw std::logic_error("Starter schema is missing a required pin.");
        };
        candidate.SetPinDefault(pin(print,"message"),DocumentValue(std::string("Hello from Kairo!")));
        candidate.Connect(pin(begin,"then"),pin(print,"in"));
        if (HasErrors(CheckCoreLogicDocument(candidate)))
            throw std::logic_error("Built-in logic starter did not compile.");
        document.SwapContents(candidate);
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
