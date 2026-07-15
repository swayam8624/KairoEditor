#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import Kairo.Editor;
import Kairo.EngineCore;

using namespace kairo::editor;

namespace
{
    int FailureCount = 0;

    void Expect(bool condition, std::string_view message)
    {
        if (condition) return;
        std::cerr << "FAILED: " << message << '\n';
        ++FailureCount;
    }

    [[nodiscard]] NodeSchema MakePrintSchema()
    {
        NodeSchema schema;
        schema.Kind = DocumentKind::Logic;
        schema.TypeKey = "kairo.logic.print";
        schema.DisplayName = "Print";
        schema.Category = "Debug";
        schema.Pins = {
            { "in", "In", PinDirection::Input, ValueType::Flow,
                PinCardinality::Single, true, std::nullopt },
            { "out", "Out", PinDirection::Output, ValueType::Flow,
                PinCardinality::Multiple, false, std::nullopt },
            { "message", "Message", PinDirection::Input, ValueType::String,
                PinCardinality::Single, false, DocumentValue(std::string{}) }
        };
        return schema;
    }

    class RecordingDocumentCompiler final : public DocumentCompiler
    {
    public:
        DocumentKind CompilerKind = DocumentKind::Logic;
        std::string TargetKey = "kairo.logic.test-v1";
        DocumentCompilerOutput Output;
        bool Throws = false;
        mutable std::size_t Calls = 0u;

        [[nodiscard]] DocumentKind Kind() const noexcept override { return CompilerKind; }
        [[nodiscard]] std::string_view Target() const noexcept override { return TargetKey; }
        [[nodiscard]] DocumentCompilerOutput Compile(const AuthoringDocument&,
            const DocumentSchemaRegistry&) const override
        {
            ++Calls;
            if (Throws) throw std::runtime_error("deliberate backend failure");
            return Output;
        }
    };

    struct CompilerGraphFixture final
    {
        kairo::assets::AssetID DocumentID =
            kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000507");
        NodeSchema Start;
        NodeSchema Print = MakePrintSchema();
        DocumentSchemaRegistry Schemas;
        AuthoringDocument Valid{ DocumentID, DocumentKind::Logic, "Compilable Graph" };
        NodeID StartNode;
        NodeID PrintNode;

        CompilerGraphFixture()
        {
            Start.Kind = DocumentKind::Logic;
            Start.TypeKey = "kairo.logic.start";
            Start.DisplayName = "Start";
            Start.Category = "Events";
            Start.Pins = {
                { "out", "Out", PinDirection::Output, ValueType::Flow,
                    PinCardinality::Multiple, false, std::nullopt }
            };
            Schemas.Register(Start);
            Schemas.Register(Print);
            StartNode = Valid.AddNode(Start);
            PrintNode = Valid.AddNode(Print);
            Valid.Connect(Valid.Node(StartNode).Pins[0].ID, Valid.Node(PrintNode).Pins[0].ID);
        }
    };

    [[nodiscard]] bool HasDiagnosticCode(
        const std::vector<DocumentDiagnostic>& diagnostics, std::string_view code)
    {
        for (const DocumentDiagnostic& diagnostic : diagnostics)
            if (diagnostic.Code == code) return true;
        return false;
    }
    void TestSuccessfulCompilation()
    {
        CompilerGraphFixture fixture;
        RecordingDocumentCompiler compiler;
        compiler.Output.Payload = { std::byte{ 0x4b }, std::byte{ 0x52 } };
        compiler.Output.Diagnostics.push_back({ DiagnosticSeverity::Warning, "unused-output",
            "The terminal flow output is not connected.", fixture.PrintNode,
            fixture.Valid.Node(fixture.PrintNode).Pins[1].ID });
        const DocumentCompileResult compiled = CompileDocument(
            fixture.Valid, fixture.Schemas, compiler);

        Expect(compiled.Succeeded(), "a valid document must compile");
        Expect(compiled.Artifact.has_value(), "a successful compile must publish an artifact");
        if (compiled.Artifact)
        {
            Expect(compiled.Artifact->Source == fixture.DocumentID,
                "the artifact must retain its source document ID");
            Expect(compiled.Artifact->Target == "kairo.logic.test-v1",
                "the artifact must retain the compiler target");
            Expect(compiled.Artifact->Payload == compiler.Output.Payload,
                "the artifact must retain the compiler payload");
        }
        Expect(compiler.Calls == 1u, "a valid document must invoke its compiler exactly once");
    }

    void TestRejectedInputs()
    {
        CompilerGraphFixture fixture;
        AuthoringDocument invalid(fixture.DocumentID, DocumentKind::Logic, "Invalid Graph");
        (void)invalid.AddNode(fixture.Print);
        RecordingDocumentCompiler skipped;
        const DocumentCompileResult rejected = CompileDocument(invalid, fixture.Schemas, skipped);
        Expect(!rejected.Succeeded(), "an invalid graph must not compile");
        Expect(!rejected.Artifact.has_value(), "an invalid graph must not publish an artifact");
        Expect(skipped.Calls == 0u, "validation failure must skip the compiler backend");
        Expect(HasErrors(rejected.Diagnostics), "validation failure must report an error");

        RecordingDocumentCompiler wrongKind;
        wrongKind.CompilerKind = DocumentKind::Material;
        const DocumentCompileResult kindFailure = CompileDocument(
            fixture.Valid, fixture.Schemas, wrongKind);
        Expect(!kindFailure.Succeeded(), "a compiler kind mismatch must fail");
        Expect(wrongKind.Calls == 0u, "a mismatched compiler must not be invoked");
        Expect(HasDiagnosticCode(kindFailure.Diagnostics, "compiler-contract"),
            "a compiler kind mismatch must report a contract diagnostic");
    }

    void TestBackendFailures()
    {
        CompilerGraphFixture fixture;
        RecordingDocumentCompiler throwing;
        throwing.Throws = true;
        const DocumentCompileResult backendFailure = CompileDocument(
            fixture.Valid, fixture.Schemas, throwing);
        Expect(!backendFailure.Succeeded(), "a throwing backend must fail compilation");
        Expect(throwing.Calls == 1u, "the throwing backend must be invoked exactly once");
        Expect(HasDiagnosticCode(backendFailure.Diagnostics, "compiler-failure"),
            "a backend exception must become a compiler-failure diagnostic");

        RecordingDocumentCompiler badDiagnostic;
        badDiagnostic.Output.Diagnostics.push_back({ DiagnosticSeverity::Error, "bad code",
            "Invalid code.", std::nullopt, std::nullopt });
        const DocumentCompileResult contractFailure = CompileDocument(
            fixture.Valid, fixture.Schemas, badDiagnostic);
        Expect(!contractFailure.Succeeded(), "an invalid backend diagnostic must fail compilation");
        Expect(HasDiagnosticCode(contractFailure.Diagnostics, "compiler-contract"),
            "an invalid backend diagnostic must report a contract failure");
    }
}

int main()
{
    TestSuccessfulCompilation();
    TestRejectedInputs();
    TestBackendFailures();

    if (FailureCount == 0)
    {
        std::cout << "All document compiler boundary checks passed.\n";
        return 0;
    }
    std::cerr << FailureCount << " document compiler boundary check(s) failed.\n";
    return 1;
}
