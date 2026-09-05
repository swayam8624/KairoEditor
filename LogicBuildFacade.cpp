#include "LogicBuildFacade.hpp"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CoreDocumentSchemas;
import Kairo.Editor.DocumentCompiler;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.LogicDocumentCompiler;
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
        DocumentCompileResult result = CompileDocument(document, schemas, compiler);
        if (!result.Succeeded())
        {
            std::string detail = "unknown compiler failure";
            for (const DocumentDiagnostic& diagnostic : result.Diagnostics)
            {
                if (diagnostic.Severity != DiagnosticSeverity::Error) continue;
                detail = diagnostic.Code + ": " + diagnostic.Message;
                break;
            }
            throw std::runtime_error(
                "Logic build failed for " + sourcePath.generic_string() + " (" + detail + ")");
        }

        kairo::engine::ValidateCompiledLogicPayload(result.Artifact->Payload);
        return std::move(result.Artifact->Payload);
    }
}
