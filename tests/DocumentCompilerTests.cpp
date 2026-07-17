#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import Kairo.Editor;
import Kairo.EngineCore;
import Kairo.Foundation.Math;

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

    [[nodiscard]] const DocumentPin& Pin(const AuthoringDocument& document,
        NodeID node, std::string_view key)
    {
        for (const DocumentPin& pin : document.Node(node).Pins)
            if (pin.Key == key) return pin;
        throw std::logic_error("test node is missing expected pin");
    }

    class RuntimeHost final : public kairo::engine::LogicHost
    {
    public:
        std::vector<std::string> Messages;
        kairo::engine::Entity Positioned;
        kairo::engine::Entity Impulsed;
        kairo::foundation::math::Vec3d Position;
        kairo::foundation::math::Vec3d Impulse;
        void Print(kairo::engine::Entity, std::string_view message) override
        { Messages.emplace_back(message); }
        void SetEntityPosition(kairo::engine::Entity entity,
            const kairo::foundation::math::Vec3d& value) override
        { Positioned = entity; Position = value; }
        void ApplyEntityImpulse(kairo::engine::Entity entity,
            const kairo::foundation::math::Vec3d& value) override
        { Impulsed = entity; Impulse = value; }
    };

    void TestCoreLogicCompiler()
    {
        const auto schemas = CreateCoreDocumentSchemaRegistry();
        const auto id = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000777");
        AuthoringDocument document(id, DocumentKind::Logic, "Runtime Logic");
        const NodeID begin = document.AddNode(schemas.Require("kairo.logic.event-begin-play"));
        const NodeID setPosition = document.AddNode(schemas.Require("kairo.logic.set-position"));
        const NodeID print = document.AddNode(schemas.Require("kairo.logic.print"));
        const NodeID input = document.AddNode(schemas.Require("kairo.logic.input-action"));
        const NodeID collision = document.AddNode(schemas.Require("kairo.logic.collision-begin"));
        const NodeID branch = document.AddNode(schemas.Require("kairo.logic.branch"));
        const NodeID impulse = document.AddNode(schemas.Require("kairo.logic.apply-impulse"));
        const NodeID collisionImpulse = document.AddNode(schemas.Require("kairo.logic.apply-impulse"));
        const NodeID entity = document.AddNode(schemas.Require("kairo.logic.entity-reference"));
        const NodeID position = document.AddNode(schemas.Require("kairo.logic.vector3"));
        const NodeID impulseValue = document.AddNode(schemas.Require("kairo.logic.vector3"));
        document.SetPinDefault(Pin(document, print, "message").ID, DocumentValue(std::string("Ready")));
        document.SetProperty(input, "action", DocumentValue(std::string("Jump")));
        document.SetPinDefault(Pin(document, branch, "condition").ID, DocumentValue(true));
        document.SetProperty(entity, "entity", DocumentValue(kairo::engine::Entity{ 7u }));
        document.SetProperty(position, "value", DocumentValue(kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 }));
        document.SetProperty(impulseValue, "value", DocumentValue(kairo::foundation::math::Vec3d{ 0.0, 5.0, 0.0 }));

        const auto connect = [&](NodeID from, std::string_view output, NodeID to, std::string_view inputKey)
        { document.Connect(Pin(document, from, output).ID, Pin(document, to, inputKey).ID); };
        connect(begin, "then", setPosition, "in");
        connect(entity, "entity", setPosition, "entity");
        connect(position, "value", setPosition, "position");
        connect(setPosition, "then", print, "in");
        connect(input, "pressed", branch, "in");
        connect(branch, "true", impulse, "in");
        connect(entity, "entity", impulse, "entity");
        connect(impulseValue, "value", impulse, "impulse");
        connect(collision, "then", collisionImpulse, "in");
        connect(collision, "other", collisionImpulse, "entity");
        connect(impulseValue, "value", collisionImpulse, "impulse");

        LogicDocumentCompiler compiler;
        const DocumentCompileResult first = CompileDocument(document, schemas, compiler);
        const DocumentCompileResult second = CompileDocument(document, schemas, compiler);
        Expect(first.Succeeded(), "core logic graph must compile into runtime bytecode");
        Expect(second.Succeeded(), "repeated core logic compilation must succeed");
        if (!first.Artifact || !second.Artifact) return;
        Expect(first.Artifact->Payload == second.Artifact->Payload,
            "logic compilation must be byte-for-byte deterministic");
        const kairo::engine::LogicProgram program =
            kairo::engine::ParseLogicProgram(first.Artifact->Payload);
        RuntimeHost host;
        kairo::engine::LogicInstance instance(program);
        (void)instance.Dispatch({ 99u }, { .Event = kairo::engine::LogicEventKind::BeginPlay,
            .Action = {}, .DeltaSeconds = 0.0, .ActionValue = 0.0, .OtherEntity = {} }, host);
        Expect(host.Messages == std::vector<std::string>{ "Ready" },
            "compiled Begin Play flow must invoke Print");
        Expect(host.Positioned == kairo::engine::Entity{ 7u } &&
            host.Position == kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 },
            "compiled transform flow must preserve typed constants");
        (void)instance.Dispatch({ 99u }, { .Event = kairo::engine::LogicEventKind::InputPressed,
            .Action = "Jump", .DeltaSeconds = 0.0, .ActionValue = 1.0, .OtherEntity = {} }, host);
        Expect(host.Impulsed == kairo::engine::Entity{ 7u } &&
            host.Impulse == kairo::foundation::math::Vec3d{ 0.0, 5.0, 0.0 },
            "compiled input and branch flow must invoke the physics host");
        host.Impulsed = {};
        (void)instance.Dispatch({ 99u }, { .Event = kairo::engine::LogicEventKind::CollisionBegin,
            .Action = {}, .DeltaSeconds = 0.0, .ActionValue = 0.0,
            .OtherEntity = kairo::engine::Entity{ 42u } }, host);
        Expect(host.Impulsed == kairo::engine::Entity{ 42u } &&
            host.Impulse == kairo::foundation::math::Vec3d{ 0.0, 5.0, 0.0 },
            "compiled collision flow must expose the counterpart entity to physics nodes");
    }

    void TestCoreLogicCompilerRejectsCycles()
    {
        const auto schemas = CreateCoreDocumentSchemaRegistry();
        const auto id = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000778");
        AuthoringDocument document(id, DocumentKind::Logic, "Cycle");
        const NodeID begin = document.AddNode(schemas.Require("kairo.logic.event-begin-play"));
        const NodeID first = document.AddNode(schemas.Require("kairo.logic.print"));
        const NodeID second = document.AddNode(schemas.Require("kairo.logic.print"));
        (void)begin;
        document.Connect(Pin(document, first, "then").ID, Pin(document, second, "in").ID);
        document.Connect(Pin(document, second, "then").ID, Pin(document, first, "in").ID);
        LogicDocumentCompiler compiler;
        const auto result = CompileDocument(document, schemas, compiler);
        Expect(!result.Succeeded() && HasDiagnosticCode(result.Diagnostics, "flow-cycle"),
            "flow cycles must fail with a stable compiler diagnostic");
    }

    void TestProjectLogicBuildPublishesSourceBoundArtifacts()
    {
        const auto root = std::filesystem::temp_directory_path() /
            ("kairo-logic-build-" + kairo::assets::GenerateAssetID().ToString());
        std::filesystem::create_directories(root / "Logic");
        const auto id = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000779");
        const auto schemas = CreateCoreDocumentSchemaRegistry();
        AuthoringDocument document(id, DocumentKind::Logic, "Built Logic");
        const NodeID begin = document.AddNode(schemas.Require("kairo.logic.event-begin-play"));
        const NodeID print = document.AddNode(schemas.Require("kairo.logic.print"));
        document.SetPinDefault(Pin(document, print, "message").ID,
            DocumentValue(std::string("Published")));
        document.Connect(Pin(document, begin, "then").ID, Pin(document, print, "in").ID);
        const auto source = root / "Logic" / "Built.kdoc";
        SaveDocument(source, document);

        kairo::assets::AssetRegistry assets;
        assets.Insert({ id, kairo::assets::AssetType::Document,
            kairo::assets::AssetOrigin::SourceFile, "Logic/Built.kdoc",
            "kairo.document-v1", 1u, {} });
        kairo::engine::Scene scene;
        const auto entity = scene.CreateEntity("Scripted");
        scene.SetLogic(entity, { { id }, true });
        const auto built = BuildAttachedLogicArtifacts(root, scene, assets);
        Expect(built.size() == 1u && std::filesystem::is_regular_file(built.front().ArtifactPath),
            "project logic build must atomically publish each attached graph");
        if (!built.empty())
        {
            const auto artifact = kairo::engine::LoadCompiledLogicArtifact(built.front().ArtifactPath);
            Expect(artifact.Source == id &&
                artifact.SourceFingerprint == kairo::assets::FingerprintFile(source),
                "published artifact must bind source identity and exact bytes");
            std::ofstream changed(source, std::ios::app);
            changed << '\n';
            changed.close();
            Expect(artifact.SourceFingerprint != kairo::assets::FingerprintFile(source),
                "source edits must make a previously published artifact detectably stale");
        }
        std::filesystem::remove_all(root);
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
    TestCoreLogicCompiler();
    TestCoreLogicCompilerRejectsCycles();
    TestProjectLogicBuildPublishesSourceBoundArtifacts();

    if (FailureCount == 0)
    {
        std::cout << "All document compiler boundary checks passed.\n";
        return 0;
    }
    std::cerr << FailureCount << " document compiler boundary check(s) failed.\n";
    return 1;
}
