module;

#include <cstddef>
#include <exception>
#include <iterator>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentCompiler;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.DocumentValidation;
import Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    inline constexpr std::size_t MaximumCompilerDiagnostics = 100'000u;
    inline constexpr std::size_t MaximumCompiledDocumentBytes = 512u * 1024u * 1024u;

    /// Backend-owned output before it is admitted as a runtime artifact.
    /// Diagnostics may contain warnings with a successful payload or errors
    /// that intentionally suppress artifact publication.
    struct DocumentCompilerOutput final
    {
        std::vector<std::byte> Payload;
        std::vector<DocumentDiagnostic> Diagnostics;
    };

    /// Immutable, identity-bearing output accepted by the editor boundary.
    /// Target identifies the consuming VM/runtime format rather than a file
    /// extension. Payload interpretation remains owned by that target backend.
    struct CompiledDocument final
    {
        kairo::assets::AssetID Source;
        DocumentKind Kind = DocumentKind::Logic;
        std::string Target;
        std::vector<std::byte> Payload;
    };

    struct DocumentCompileResult final
    {
        std::optional<CompiledDocument> Artifact;
        std::vector<DocumentDiagnostic> Diagnostics;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Artifact.has_value() && !HasErrors(Diagnostics);
        }
    };

    /// Domain implementation boundary. Logic, material, audio, animation, and
    /// simulation compilers implement this interface independently; the editor
    /// kernel never embeds their execution semantics. Implementations must be
    /// deterministic for an identical document/schema snapshot and may not
    /// mutate either input.
    class DocumentCompiler
    {
    public:
        virtual ~DocumentCompiler() = default;
        [[nodiscard]] virtual DocumentKind Kind() const noexcept = 0;
        [[nodiscard]] virtual std::string_view Target() const noexcept = 0;
        [[nodiscard]] virtual DocumentCompilerOutput Compile(
            const AuthoringDocument& document, const DocumentSchemaRegistry& schemas) const = 0;
    };

    namespace document_compiler_detail
    {
        [[nodiscard]] inline DocumentDiagnostic ContractFailure(std::string message)
        {
            return { DiagnosticSeverity::Error, "compiler-contract", std::move(message),
                std::nullopt, std::nullopt };
        }

        [[nodiscard]] inline std::string SafeFailureMessage(const std::exception& error)
        {
            const std::string_view message(error.what());
            if (message.empty() || message.size() > 4000u || !IsValidUtf8(message))
                return "Compiler threw an exception without a safe diagnostic message.";
            return "Compiler failed: " + std::string(message);
        }

        inline void ValidateDiagnostic(const DocumentDiagnostic& diagnostic,
            const AuthoringDocument& document)
        {
            if (diagnostic.Code.size() > 128u || !IsSchemaKey(diagnostic.Code, false))
                throw std::invalid_argument("Compiler diagnostic has an invalid code.");
            ValidateUtf8Text(diagnostic.Message, { 1u, 4096u, true, false }, "Compiler diagnostic message");
            if (diagnostic.Node.has_value() && !document.Contains(*diagnostic.Node))
                throw std::invalid_argument("Compiler diagnostic references an unknown node.");
            if (diagnostic.Pin.has_value())
            {
                if (!document.Contains(*diagnostic.Pin))
                    throw std::invalid_argument("Compiler diagnostic references an unknown pin.");
                if (diagnostic.Node.has_value() && document.NodeForPin(*diagnostic.Pin) != *diagnostic.Node)
                    throw std::invalid_argument("Compiler diagnostic pin does not belong to its node.");
            }
        }
    }

    /// Input: immutable authored document, schema snapshot, and matching domain
    /// compiler. Output: diagnostics plus an optional validated artifact.
    /// Task: prevent malformed documents or backend contract violations from
    /// entering play/build. Backend exceptions become diagnostics; allocation
    /// failures remain exceptional because continuing may be unsafe.
    [[nodiscard]] inline DocumentCompileResult CompileDocument(const AuthoringDocument& document,
        const DocumentSchemaRegistry& schemas, const DocumentCompiler& compiler)
    {
        using namespace document_compiler_detail;
        DocumentCompileResult result;
        result.Diagnostics = ValidateDocument(document, schemas);
        if (HasErrors(result.Diagnostics)) return result;

        if (compiler.Kind() != document.Kind())
        {
            result.Diagnostics.push_back(ContractFailure(
                "Compiler kind does not match the authoring document kind."));
            return result;
        }

        const std::string target(compiler.Target());
        if (target.empty() || target.size() > 128u || !IsSchemaKey(target, true))
        {
            result.Diagnostics.push_back(ContractFailure("Compiler target key is invalid."));
            return result;
        }

        DocumentCompilerOutput output;
        try { output = compiler.Compile(document, schemas); }
        catch (const std::bad_alloc&) { throw; }
        catch (const std::exception& error)
        {
            result.Diagnostics.push_back({ DiagnosticSeverity::Error, "compiler-failure",
                SafeFailureMessage(error), std::nullopt, std::nullopt });
            return result;
        }
        catch (...)
        {
            result.Diagnostics.push_back({ DiagnosticSeverity::Error, "compiler-failure",
                "Compiler failed with an unknown exception.", std::nullopt, std::nullopt });
            return result;
        }

        if (output.Diagnostics.size() > MaximumCompilerDiagnostics)
        {
            result.Diagnostics.push_back(ContractFailure("Compiler exceeded the diagnostic safety limit."));
            return result;
        }
        if (output.Payload.size() > MaximumCompiledDocumentBytes)
        {
            result.Diagnostics.push_back(ContractFailure("Compiler exceeded the artifact safety limit."));
            return result;
        }
        try
        {
            for (const DocumentDiagnostic& diagnostic : output.Diagnostics)
                ValidateDiagnostic(diagnostic, document);
        }
        catch (const std::exception& error)
        {
            result.Diagnostics.push_back(ContractFailure(error.what()));
            return result;
        }

        result.Diagnostics.insert(result.Diagnostics.end(),
            std::make_move_iterator(output.Diagnostics.begin()),
            std::make_move_iterator(output.Diagnostics.end()));
        if (HasErrors(result.Diagnostics)) return result;
        result.Artifact = CompiledDocument{ document.ID(), document.Kind(), target, std::move(output.Payload) };
        return result;
    }
}
