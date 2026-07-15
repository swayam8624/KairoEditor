#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
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
}

TEST_CASE("Document compiler boundary publishes validated artifacts",
    "[KairoEditor][Document][Compiler]")
{
    CompilerGraphFixture fixture;
    RecordingDocumentCompiler compiler;
    compiler.Output.Payload = { std::byte{ 0x4b }, std::byte{ 0x52 } };
    compiler.Output.Diagnostics.push_back({ DiagnosticSeverity::Warning, "unused-output",
        "The terminal flow output is not connected.", fixture.PrintNode,
        fixture.Valid.Node(fixture.PrintNode).Pins[1].ID });
    const DocumentCompileResult compiled = CompileDocument(fixture.Valid, fixture.Schemas, compiler);
    REQUIRE(compiled.Succeeded());
    REQUIRE(compiled.Artifact.has_value());
    CHECK(compiled.Artifact->Source == fixture.DocumentID);
    CHECK(compiled.Artifact->Target == "kairo.logic.test-v1");
    CHECK(compiled.Artifact->Payload == compiler.Output.Payload);
    CHECK(compiler.Calls == 1u);
}

TEST_CASE("Document compiler boundary rejects invalid graphs and compiler kinds",
    "[KairoEditor][Document][Compiler]")
{
    CompilerGraphFixture fixture;
    AuthoringDocument invalid(fixture.DocumentID, DocumentKind::Logic, "Invalid Graph");
    (void)invalid.AddNode(fixture.Print);
    RecordingDocumentCompiler skipped;
    const DocumentCompileResult rejected = CompileDocument(invalid, fixture.Schemas, skipped);
    CHECK_FALSE(rejected.Succeeded());
    CHECK_FALSE(rejected.Artifact.has_value());
    CHECK(skipped.Calls == 0u);
    CHECK(HasErrors(rejected.Diagnostics));

    RecordingDocumentCompiler wrongKind;
    wrongKind.CompilerKind = DocumentKind::Material;
    const DocumentCompileResult kindFailure = CompileDocument(fixture.Valid, fixture.Schemas, wrongKind);
    CHECK_FALSE(kindFailure.Succeeded());
    CHECK(wrongKind.Calls == 0u);
    CHECK(HasDiagnosticCode(kindFailure.Diagnostics, "compiler-contract"));
}

TEST_CASE("Document compiler boundary contains backend contract failures",
    "[KairoEditor][Document][Compiler]")
{
    CompilerGraphFixture fixture;
    RecordingDocumentCompiler throwing;
    throwing.Throws = true;
    const DocumentCompileResult backendFailure = CompileDocument(fixture.Valid, fixture.Schemas, throwing);
    CHECK_FALSE(backendFailure.Succeeded());
    CHECK(throwing.Calls == 1u);
    CHECK(HasDiagnosticCode(backendFailure.Diagnostics, "compiler-failure"));

    RecordingDocumentCompiler badDiagnostic;
    badDiagnostic.Output.Diagnostics.push_back({ DiagnosticSeverity::Error, "bad code",
        "Invalid code.", std::nullopt, std::nullopt });
    const DocumentCompileResult contractFailure = CompileDocument(
        fixture.Valid, fixture.Schemas, badDiagnostic);
    CHECK_FALSE(contractFailure.Succeeded());
    CHECK(HasDiagnosticCode(contractFailure.Diagnostics, "compiler-contract"));
}
