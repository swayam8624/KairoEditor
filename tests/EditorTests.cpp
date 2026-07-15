#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

import Kairo.Editor;
import Kairo.EngineCore;
import Kairo.Foundation.Math;

using namespace kairo::editor;

static_assert(!std::is_same_v<NodeID, PinID>);

TEST_CASE("Document text and values enforce persistent data boundaries", "[KairoEditor][Document][Types]")
{
    CHECK(IsValidUtf8("Kairo \xE2\x9C\x93"));
    CHECK_FALSE(IsValidUtf8(std::string("\xC0\xAF", 2u)));
    CHECK_FALSE(IsValidUtf8(std::string("\xED\xA0\x80", 3u)));
    REQUIRE_NOTHROW(ValidateUtf8Text("line one\nline two", { 1u, 64u, true, false }, "test text"));
    REQUIRE_THROWS_AS(ValidateUtf8Text("line one\nline two", { 1u, 64u, false, false }, "test text"),
        std::invalid_argument);

    CHECK(DocumentValue{}.Type() == ValueType::Flow);
    CHECK(DocumentValue(true).Get<bool>());
    CHECK(DocumentValue(std::int64_t{ 42 }).Get<std::int64_t>() == 42);
    CHECK(DocumentValue(kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 }).Type() == ValueType::Vector3);
    REQUIRE_THROWS_AS(DocumentValue(std::numeric_limits<double>::infinity()), std::invalid_argument);
    REQUIRE_THROWS_AS(DocumentValue(std::string("bad\xC0", 4u)), std::invalid_argument);
    REQUIRE_THROWS_AS(DocumentValue(kairo::assets::AssetID{}), std::invalid_argument);
    REQUIRE_THROWS_AS(DocumentValue(1.0).Get<bool>(), std::logic_error);

    const NodeID node{ 7u };
    const PinID pin{ 7u };
    CHECK(static_cast<bool>(node));
    CHECK(static_cast<bool>(pin));
    CHECK(StableLocalIDHash<NodeIDTag>{}(node) == StableLocalIDHash<PinIDTag>{}(pin));
}

TEST_CASE("Document schema registry validates and orders node contracts", "[KairoEditor][Document][Schema]")
{
    NodeSchema add;
    add.Kind = DocumentKind::Logic;
    add.TypeKey = "kairo.logic.add-float";
    add.DisplayName = "Add Float";
    add.Category = "Math";
    add.Pins = {
        { "a", "A", PinDirection::Input, ValueType::Float, PinCardinality::Single, false, DocumentValue(0.0) },
        { "b", "B", PinDirection::Input, ValueType::Float, PinCardinality::Single, false, DocumentValue(0.0) },
        { "result", "Result", PinDirection::Output, ValueType::Float, PinCardinality::Multiple, false, std::nullopt }
    };
    add.PropertyDefaults.emplace("clamp", DocumentValue(false));

    NodeSchema print;
    print.Kind = DocumentKind::Logic;
    print.TypeKey = "kairo.logic.print";
    print.DisplayName = "Print";
    print.Category = "Debug";
    print.Pins = {
        { "in", "In", PinDirection::Input, ValueType::Flow, PinCardinality::Single, true, std::nullopt },
        { "out", "Out", PinDirection::Output, ValueType::Flow, PinCardinality::Multiple, false, std::nullopt },
        { "message", "Message", PinDirection::Input, ValueType::String, PinCardinality::Single,
            false, DocumentValue(std::string{}) }
    };

    DocumentSchemaRegistry registry;
    registry.Register(print);
    registry.Register(add);
    CHECK(registry.Size() == 2u);
    CHECK(registry.Require(add.TypeKey) == add);
    const auto schemas = registry.Snapshot(DocumentKind::Logic);
    REQUIRE(schemas.size() == 2u);
    CHECK(schemas[0].TypeKey == "kairo.logic.add-float");
    CHECK(schemas[1].TypeKey == "kairo.logic.print");
    REQUIRE_THROWS_AS(registry.Register(add), std::invalid_argument);
    REQUIRE_THROWS_AS(registry.Require("missing"), std::out_of_range);

    NodeSchema duplicatePins = add;
    duplicatePins.TypeKey = "kairo.logic.invalid-duplicate";
    duplicatePins.Pins[1].Key = duplicatePins.Pins[0].Key;
    REQUIRE_THROWS_AS(ValidateNodeSchema(duplicatePins), std::invalid_argument);

    NodeSchema mismatchedDefault = add;
    mismatchedDefault.TypeKey = "kairo.logic.invalid-default";
    mismatchedDefault.Pins[0].DefaultValue = DocumentValue(true);
    REQUIRE_THROWS_AS(ValidateNodeSchema(mismatchedDefault), std::invalid_argument);
    CHECK_FALSE(IsSchemaKey(".leading", true));
    CHECK_FALSE(IsSchemaKey("double..segment", true));
    CHECK_FALSE(IsSchemaKey("9invalid", false));
}

namespace
{
    [[nodiscard]] NodeSchema MakeAddFloatSchema()
    {
        NodeSchema schema;
        schema.Kind = DocumentKind::Logic;
        schema.TypeKey = "kairo.logic.add-float";
        schema.DisplayName = "Add Float";
        schema.Category = "Math";
        schema.Pins = {
            { "a", "A", PinDirection::Input, ValueType::Float, PinCardinality::Single, false, DocumentValue(0.0) },
            { "b", "B", PinDirection::Input, ValueType::Float, PinCardinality::Single, false, DocumentValue(0.0) },
            { "result", "Result", PinDirection::Output, ValueType::Float, PinCardinality::Multiple, false, std::nullopt }
        };
        schema.PropertyDefaults.emplace("clamp", DocumentValue(false));
        return schema;
    }

    [[nodiscard]] NodeSchema MakePrintSchema()
    {
        NodeSchema schema;
        schema.Kind = DocumentKind::Logic;
        schema.TypeKey = "kairo.logic.print";
        schema.DisplayName = "Print";
        schema.Category = "Debug";
        schema.Pins = {
            { "in", "In", PinDirection::Input, ValueType::Flow, PinCardinality::Single, true, std::nullopt },
            { "out", "Out", PinDirection::Output, ValueType::Flow, PinCardinality::Multiple, false, std::nullopt },
            { "message", "Message", PinDirection::Input, ValueType::String, PinCardinality::Single,
                false, DocumentValue(std::string{}) }
        };
        return schema;
    }

    class IntegerCommand final : public EditorCommand
    {
    public:
        IntegerCommand(int& value, int delta, bool fail = false)
            : m_Value(&value), m_Delta(delta), m_Fail(fail) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Change Integer"; }
        void Execute() override
        {
            if (m_Fail) throw std::runtime_error("deliberate command failure");
            *m_Value += m_Delta;
        }
        void Undo() override { *m_Value -= m_Delta; }
    private:
        int* m_Value;
        int m_Delta;
        bool m_Fail;
    };
}

TEST_CASE("Authoring documents enforce typed deterministic graph topology", "[KairoEditor][Document][Graph]")
{
    const auto documentID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000501");
    const NodeSchema add = MakeAddFloatSchema();
    AuthoringDocument document(documentID, DocumentKind::Logic, "Damage Formula");
    const NodeID left = document.AddNode(add, { -120.0, 40.0 });
    const NodeID right = document.AddNode(add, { 180.0, 40.0 });
    REQUIRE(left.Value == 1u);
    REQUIRE(right.Value == 2u);
    REQUIRE(document.Node(left).Pins.size() == 3u);
    CHECK(document.PinCount() == 6u);
    const PinID leftResult = document.Node(left).Pins[2].ID;
    const PinID rightA = document.Node(right).Pins[0].ID;
    const PinID rightB = document.Node(right).Pins[1].ID;

    CHECK(document.CanConnect(leftResult, rightA).Allowed);
    document.Connect(leftResult, rightA);
    CHECK(document.ConnectionCount() == 1u);
    CHECK(document.IsConnected(rightA));
    CHECK(document.NodeForPin(rightA) == right);
    CHECK(document.CanConnect(leftResult, rightA).Code == "duplicate");
    CHECK(document.CanConnect(rightA, leftResult).Code == "direction");
    document.Connect(leftResult, rightB);
    CHECK(document.ConnectionCount() == 2u);
    CHECK(document.CanConnect(document.Node(right).Pins[2].ID, rightA).Code == "input-cardinality");

    document.SetProperty(left, "clamp", DocumentValue(true));
    CHECK(document.Node(left).Properties.at("clamp").Get<bool>());
    REQUIRE_THROWS_AS(document.SetProperty(left, "clamp", DocumentValue(1.0)), std::invalid_argument);
    document.SetPinDefault(rightB, DocumentValue(8.5));
    CHECK(document.Pin(rightB).DefaultValue->Get<double>() == 8.5);
    REQUIRE_THROWS_AS(document.SetNodePosition(left,
        { std::numeric_limits<double>::quiet_NaN(), 0.0 }), std::invalid_argument);

    document.RemoveNode(left);
    CHECK_FALSE(document.Contains(left));
    CHECK(document.ConnectionCount() == 0u);
    CHECK(document.PinCount() == 3u);
    REQUIRE_THROWS_AS(document.Disconnect(leftResult, rightA), std::out_of_range);

    REQUIRE_THROWS_AS(AuthoringDocument({}, DocumentKind::Logic, "Invalid"), std::invalid_argument);
    REQUIRE_THROWS_AS(AuthoringDocument(documentID, DocumentKind::Logic, "bad\nname"), std::invalid_argument);
    NodeSchema material = add;
    material.Kind = DocumentKind::Material;
    material.TypeKey = "kairo.material.add-float";
    REQUIRE_THROWS_AS(document.AddNode(material), std::invalid_argument);
}

TEST_CASE("Authoring documents restore IDs and report schema-aware diagnostics", "[KairoEditor][Document][Validation]")
{
    const auto documentID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000502");
    const NodeSchema add = MakeAddFloatSchema();
    const NodeSchema print = MakePrintSchema();
    DocumentSchemaRegistry schemas;
    schemas.Register(add);
    schemas.Register(print);

    AuthoringDocument document(documentID, DocumentKind::Logic, "Restored Graph");
    document.AddNodeWithIDs(add, { 42u }, { PinID{ 100u }, PinID{ 101u }, PinID{ 102u } }, { 10.0, 20.0 });
    REQUIRE_THROWS_AS(document.AddNodeWithIDs(add, { 42u },
        { PinID{ 110u }, PinID{ 111u }, PinID{ 112u } }), std::invalid_argument);
    REQUIRE_THROWS_AS(document.AddNodeWithIDs(add, { 50u },
        { PinID{ 100u }, PinID{ 111u }, PinID{ 112u } }), std::invalid_argument);
    const NodeID printNode = document.AddNode(print);
    CHECK(printNode.Value == 43u);
    CHECK(document.Node(printNode).Pins.front().ID.Value == 103u);

    auto diagnostics = ValidateDocument(document, schemas);
    REQUIRE(HasErrors(diagnostics));
    const auto required = std::ranges::find(diagnostics, std::string("required-input"), &DocumentDiagnostic::Code);
    REQUIRE(required != diagnostics.end());
    CHECK(required->Node == printNode);
    CHECK(required->Pin == document.Node(printNode).Pins.front().ID);
    document.ClearPinDefault(document.Node(printNode).Pins[2].ID);
    diagnostics = ValidateDocument(document, schemas);
    CHECK(std::ranges::find(diagnostics, std::string("missing-input-value"),
        &DocumentDiagnostic::Code) != diagnostics.end());

    DocumentSchemaRegistry missingSchemas;
    diagnostics = ValidateDocument(document, missingSchemas);
    CHECK(HasErrors(diagnostics));
    CHECK(diagnostics.front().Code == "unknown-node-type");

    AuthoringDocument complete(documentID, DocumentKind::Logic, "Complete Graph");
    NodeSchema start;
    start.Kind = DocumentKind::Logic;
    start.TypeKey = "kairo.logic.start";
    start.DisplayName = "Start";
    start.Category = "Events";
    start.Pins = {
        { "out", "Out", PinDirection::Output, ValueType::Flow, PinCardinality::Multiple, false, std::nullopt }
    };
    schemas.Register(start);
    const NodeID startNode = complete.AddNode(start);
    const NodeID completePrint = complete.AddNode(print);
    complete.Connect(complete.Node(startNode).Pins[0].ID, complete.Node(completePrint).Pins[0].ID);
    CHECK_FALSE(HasErrors(ValidateDocument(complete, schemas)));

    const auto mismatch = complete.CanConnect(complete.Node(startNode).Pins[0].ID,
        complete.Node(completePrint).Pins[2].ID);
    CHECK_FALSE(mismatch.Allowed);
    CHECK(mismatch.Code == "type");
}

TEST_CASE("Authoring documents persist canonically without installed schemas",
    "[KairoEditor][Document][Persistence]")
{
    const auto documentID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000503");
    const auto textureID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000504");
    AuthoringDocument original(documentID, DocumentKind::Logic, "Lossless \"Plugin\" Graph");

    DocumentNode source;
    source.ID = { 40u };
    source.TypeKey = "plugin.signal.source";
    source.Position = { -0.0, 25.5 };
    source.Pins.push_back({ { 100u }, "value", "Value", PinDirection::Output,
        ValueType::Float, PinCardinality::Multiple, false, std::nullopt });
    source.Properties.emplace("asset", DocumentValue(textureID));
    source.Properties.emplace("enabled", DocumentValue(true));
    source.Properties.emplace("label", DocumentValue(std::string("line one\nline \"two\"")));
    source.Properties.emplace("layer", DocumentValue(std::int64_t{ -17 }));
    source.Properties.emplace("scale", DocumentValue(1.0 / 3.0));
    source.Properties.emplace("uv", DocumentValue(kairo::foundation::math::Vec2d{ 0.25, 0.75 }));
    source.Properties.emplace("position", DocumentValue(kairo::foundation::math::Vec3d{ 1.0, 2.0, 3.0 }));
    source.Properties.emplace("color", DocumentValue(kairo::foundation::math::Vec4d{ 0.1, 0.2, 0.3, 1.0 }));
    original.RestoreNode(std::move(source));

    DocumentNode sink;
    sink.ID = { 80u };
    sink.TypeKey = "plugin.signal.sink";
    sink.Position = { 320.0, 25.5 };
    sink.Pins.push_back({ { 200u }, "value", "Value", PinDirection::Input,
        ValueType::Float, PinCardinality::Single, true, std::nullopt });
    original.RestoreNode(std::move(sink));
    original.Connect({ 100u }, { 200u });

    const std::string canonical = SerializeDocument(original);
    const AuthoringDocument parsed = ParseDocument(canonical);
    CHECK(SerializeDocument(parsed) == canonical);
    CHECK(parsed.ID() == original.ID());
    CHECK(parsed.Name() == original.Name());
    CHECK(parsed.Node({ 40u }).Properties.at("asset").Get<kairo::assets::AssetID>() == textureID);
    CHECK(parsed.Node({ 40u }).Properties.at("label").Get<std::string>() == "line one\nline \"two\"");
    CHECK(parsed.ConnectionCount() == 1u);

    DocumentSchemaRegistry unavailablePlugins;
    const auto diagnostics = ValidateDocument(parsed, unavailablePlugins);
    REQUIRE(HasErrors(diagnostics));
    CHECK(std::ranges::count(diagnostics, std::string("unknown-node-type"), &DocumentDiagnostic::Code) == 2u);

    const auto path = std::filesystem::temp_directory_path() /
        ("kairo-document-test-" + kairo::assets::GenerateAssetID().ToString() + ".kdoc");
    SaveDocument(path, original);
    CHECK(SerializeDocument(LoadDocument(path)) == canonical);
    std::filesystem::remove(path);
}

TEST_CASE("Authoring document parser reports precise structural failures",
    "[KairoEditor][Document][Persistence]")
{
    const auto requireLocation = [](std::string_view source, std::size_t line, std::size_t column)
    {
        try
        {
            (void)ParseDocument(source);
            FAIL("Expected a located document parse failure");
        }
        catch (const DocumentFormatError& error)
        {
            CHECK(error.Line == line);
            CHECK(error.Column == column);
        }
    };

    requireLocation(
        "kairo-document 1\n"
        "id 00000000-0000-4000-8000-000000000505\n"
        "kind logic\n"
        "name \"Duplicate Pin\"\n"
        "node 1 plugin.test 0 0\n"
        "pin 4 first \"First\" output float multiple false no-default\n"
        "pin 4 second \"Second\" output float multiple false no-default\n"
        "end-node\n", 7u, 5u);

    requireLocation(
        "kairo-document 1\n"
        "id 00000000-0000-4000-8000-000000000505\n"
        "kind logic\n"
        "name \"Bad Number\"\n"
        "node 1 plugin.test 0 0\n"
        "property gain float nan\n"
        "end-node\n", 6u, 21u);

    requireLocation(
        "kairo-document 1\n"
        "id 00000000-0000-4000-8000-000000000505\n"
        "kind logic\n"
        "name \"\"\n", 4u, 6u);

    requireLocation(
        "kairo-document 1\n"
        "id 00000000-0000-4000-8000-000000000505\n"
        "kind logic\n"
        "name \"Metadata Order\"\n"
        "connect 1 2\n"
        "name \"Too Late\"\n", 6u, 1u);

    requireLocation(
        "kairo-document 1\n"
        "id 00000000-0000-4000-8000-000000000505\n"
        "kind logic\n"
        "name \"Missing Terminator\"\n"
        "node 1 plugin.test 0 0\n", 6u, 1u);
}

TEST_CASE("Command history preserves causal branches and bounded storage", "[KairoEditor][Commands]")
{
    int value = 0;
    CommandHistory history(2u);
    history.Execute(std::make_unique<IntegerCommand>(value, 1));
    history.Execute(std::make_unique<IntegerCommand>(value, 2));
    CHECK(value == 3);
    CHECK(history.UndoName() == "Change Integer");
    history.Undo();
    CHECK(value == 1);
    REQUIRE(history.CanRedo());

    history.Execute(std::make_unique<IntegerCommand>(value, 4));
    CHECK(value == 5);
    CHECK_FALSE(history.CanRedo());
    history.Execute(std::make_unique<IntegerCommand>(value, 8));
    CHECK(history.RetainedCount() == 2u);
    CHECK(history.AppliedCount() == 2u);
    history.Undo();
    history.Undo();
    CHECK(value == 1);
    REQUIRE_THROWS_AS(history.Undo(), std::logic_error);

    const auto retained = history.RetainedCount();
    REQUIRE_THROWS_AS(history.Execute(std::make_unique<IntegerCommand>(value, 1, true)), std::runtime_error);
    CHECK(history.RetainedCount() == retained);
    CHECK(value == 1);
    REQUIRE_THROWS_AS(CommandHistory(0u), std::invalid_argument);
}

TEST_CASE("Project descriptors round trip portable bootstrap state", "[KairoEditor][Project]")
{
    const ProjectDescriptor original{ "Kairo City", "Project/Assets.kassets", "Scenes/City.kscene" };
    const std::string encoded = SerializeProjectDescriptor(original);
    CHECK(encoded ==
        "kairo-project 1\n"
        "name \"Kairo City\"\n"
        "assets \"Project/Assets.kassets\"\n"
        "startup-scene \"Scenes/City.kscene\"\n");
    CHECK(ParseProjectDescriptor(encoded) == original);
}

TEST_CASE("Project descriptors reject malformed and unsafe input with locations", "[KairoEditor][Project]")
{
    const std::string duplicate =
        "kairo-project 1\nname \"First\"\nname \"Second\"\nassets \"Assets.kassets\"\nstartup-scene \"Main.kscene\"\n";
    try
    {
        (void)ParseProjectDescriptor(duplicate);
        FAIL("Expected a located project parse failure");
    }
    catch (const ProjectFormatError& error)
    {
        CHECK(error.Line == 3u);
        CHECK(error.Column == 1u);
    }

    try
    {
        (void)ParseProjectDescriptor(
            "kairo-project 1\nname \"Unsafe\"\nassets \"../outside\"\nstartup-scene \"Main.kscene\"\n");
        FAIL("Expected a located unsafe-path failure");
    }
    catch (const ProjectFormatError& error)
    {
        CHECK(error.Line == 3u);
        CHECK(error.Column == 8u);
    }
    ProjectDescriptor invalid{ "", "Assets.kassets", "Main.kscene" };
    REQUIRE_THROWS_AS(SerializeProjectDescriptor(invalid), std::invalid_argument);
    ProjectDescriptor aliasing{ "Alias", "Scenes/../Main.kscene", "Main.kscene" };
    REQUIRE_THROWS_AS(SerializeProjectDescriptor(aliasing), std::invalid_argument);
}

TEST_CASE("Project descriptor files use the validated disk format", "[KairoEditor][Project]")
{
    const auto path = std::filesystem::temp_directory_path() /
        ("kairo-project-test-" + kairo::assets::GenerateAssetID().ToString() + ".kproject");
    const ProjectDescriptor original{ "Saved Project", "Assets.kassets", "Scenes/Main.kscene" };
    SaveProjectDescriptor(path, original);
    CHECK(LoadProjectDescriptor(path) == original);
    std::filesystem::remove(path);
}

TEST_CASE("Project sessions create save and reopen complete projects", "[KairoEditor][Project][Session]")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-session-test-" + kairo::assets::GenerateAssetID().ToString());
    ProjectSession session;
    session.CreateProject(root, "Session Test");
    REQUIRE(session.HasProject());
    CHECK(session.Descriptor().Name == "Session Test");
    CHECK_FALSE(session.HasUnsavedChanges());
    CHECK(std::filesystem::is_regular_file(root / DefaultProjectFileName));

    const auto mesh = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000301");
    const auto material = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000302");
    auto& assets = session.EditAssets();
    assets.Insert({ mesh, kairo::assets::AssetType::Mesh, kairo::assets::AssetOrigin::Builtin,
        "builtin/cube", "kairo.builtin", 1u, {} });
    assets.Insert({ material, kairo::assets::AssetType::Material, kairo::assets::AssetOrigin::Builtin,
        "builtin/default-material", "kairo.builtin", 1u, {} });
    auto& scene = session.EditScene();
    const auto cube = scene.CreateEntityWithID({ 27u }, "Saved Cube");
    scene.SetMeshRenderer(cube, { { mesh }, { material }, true });
    REQUIRE(session.IsSceneDirty());
    REQUIRE(session.AreAssetsDirty());
    session.SaveAll();
    CHECK_FALSE(session.HasUnsavedChanges());

    ProjectSession reopened;
    reopened.OpenProject(root / DefaultProjectFileName);
    CHECK(reopened.Scene().Contains(cube));
    CHECK(reopened.Scene().Name(cube).Value == "Saved Cube");
    CHECK(reopened.Assets().Contains(mesh));
    std::filesystem::remove_all(root);
}

TEST_CASE("Project sessions enforce dirty scene transitions and portable save-as", "[KairoEditor][Project][Session]")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-session-transition-" + kairo::assets::GenerateAssetID().ToString());
    ProjectSession session;
    session.CreateProject(root, "Transitions");
    const auto entity = session.EditScene().CreateEntity("Second Scene Entity");
    REQUIRE_THROWS_AS(session.OpenScene("Scenes/Main.kscene"), std::logic_error);
    session.SaveSceneAs("Scenes/Second.kscene");
    CHECK(session.ActiveScenePath() == std::filesystem::path("Scenes/Second.kscene"));
    CHECK_FALSE(session.IsSceneDirty());
    session.OpenScene("Scenes/Main.kscene");
    CHECK_FALSE(session.Scene().Contains(entity));
    REQUIRE_THROWS_AS(session.SaveSceneAs("../escape.kscene"), std::invalid_argument);

    (void)session.EditScene().CreateEntity("Unsaved");
    REQUIRE_THROWS_AS(session.Close(), std::logic_error);
    session.Close(UnsavedChangesPolicy::Discard);
    CHECK_FALSE(session.HasProject());
    REQUIRE_THROWS_AS(session.EditScene(), std::logic_error);
    std::filesystem::remove_all(root);
}

TEST_CASE("Failed project opens preserve the live session", "[KairoEditor][Project][Session]")
{
    const auto base = std::filesystem::temp_directory_path() /
        ("kairo-session-strong-" + kairo::assets::GenerateAssetID().ToString());
    const auto good = base / "Good";
    const auto broken = base / "Broken";
    ProjectSession session;
    session.CreateProject(good, "Good Project");
    const auto stable = session.EditScene().CreateEntity("Still Here");

    std::filesystem::create_directories(broken);
    SaveProjectDescriptor(broken / DefaultProjectFileName,
        { "Broken Project", "Missing.kassets", "Missing.kscene" });
    REQUIRE_THROWS_AS(session.OpenProject(broken / DefaultProjectFileName), std::logic_error);
    session.SaveScene();
    REQUIRE_THROWS(session.OpenProject(broken / DefaultProjectFileName));
    CHECK(session.Descriptor().Name == "Good Project");
    CHECK(session.Scene().Contains(stable));
    CHECK_FALSE(session.HasUnsavedChanges());
    std::filesystem::remove_all(base);
}

TEST_CASE("Scene commands restore stable entities and merge Inspector edits", "[KairoEditor][Commands][Scene]")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-command-scene-" + kairo::assets::GenerateAssetID().ToString());
    ProjectSession project;
    project.CreateProject(root, "Command Scene");
    CommandHistory history;

    auto create = std::make_unique<CreateEntityCommand>(project, "Commanded");
    auto* created = create.get();
    history.Execute(std::move(create));
    const auto entity = created->CreatedEntity();
    REQUIRE(project.Scene().Contains(entity));
    REQUIRE(project.IsSceneDirty());

    history.Execute(std::make_unique<SetEntityNameCommand>(project, entity, "Commanded A"));
    history.Execute(std::make_unique<SetEntityNameCommand>(project, entity, "Commanded Final"));
    CHECK(history.AppliedCount() == 2u);
    CHECK(project.Scene().Name(entity).Value == "Commanded Final");
    history.Undo();
    CHECK(project.Scene().Name(entity).Value == "Commanded");
    history.Redo();
    CHECK(project.Scene().Name(entity).Value == "Commanded Final");

    auto firstTransform = project.Scene().Transform(entity).Local;
    firstTransform.Translation = { 1.0f, 2.0f, 3.0f };
    history.Execute(std::make_unique<SetEntityTransformCommand>(project, entity, firstTransform));
    auto finalTransform = firstTransform;
    finalTransform.Scale = { 2.0f, 3.0f, 4.0f };
    history.Execute(std::make_unique<SetEntityTransformCommand>(project, entity, finalTransform));
    CHECK(history.AppliedCount() == 3u);
    CHECK(project.Scene().Transform(entity).Local == finalTransform);
    history.Undo();
    CHECK(project.Scene().Transform(entity).Local.Translation == kairo::foundation::math::Vec3f{});
    history.Redo();

    const auto mesh = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000401");
    const auto material = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000402");
    auto& authoredScene = project.EditScene();
    authoredScene.SetMeshRenderer(entity, { { mesh }, { material }, false });
    authoredScene.SetCamera(entity, { 0.9f, 0.2f, 500.0f, true });
    authoredScene.SetRigidBody(entity, { 17u });
    authoredScene.SetCollider(entity, { 23u });

    history.Execute(std::make_unique<DeleteEntityCommand>(project, entity));
    CHECK_FALSE(project.Scene().Contains(entity));
    history.Undo();
    REQUIRE(project.Scene().Contains(entity));
    CHECK(project.Scene().Name(entity).Value == "Commanded Final");
    CHECK(project.Scene().Transform(entity).Local == finalTransform);
    CHECK(project.Scene().MeshRenderer(entity).MeshAsset.ID == mesh);
    CHECK(project.Scene().MeshRenderer(entity).MaterialAsset.ID == material);
    CHECK_FALSE(project.Scene().MeshRenderer(entity).Visible);
    CHECK(project.Scene().Camera(entity).NearPlane == 0.2f);
    CHECK(project.Scene().RigidBody(entity).Body == 17u);
    CHECK(project.Scene().Collider(entity).Collider == 23u);
    history.Redo();
    CHECK_FALSE(project.Scene().Contains(entity));

    history.Clear();
    project.Close(UnsavedChangesPolicy::Discard);
    std::filesystem::remove_all(root);
}

TEST_CASE("Editor state validates scene selection and play transitions", "[KairoEditor][State]")
{
    kairo::engine::Scene scene;
    const auto entity = scene.CreateEntity("Cube");
    EditorState editor(scene);
    editor.Select(entity);
    REQUIRE(editor.SelectedEntity().has_value());
    editor.Play();
    CHECK(editor.Mode() == EditorMode::Play);
    editor.Pause();
    editor.Resume();
    editor.Stop();
    CHECK(editor.Mode() == EditorMode::Edit);
    CHECK_FALSE(editor.SelectedEntity().has_value());
}

TEST_CASE("Editor panel visibility persists independently of a UI backend", "[KairoEditor][Panels]")
{
    kairo::engine::Scene scene;
    EditorState editor(scene);
    REQUIRE(editor.Panels().IsVisible(Panel::Console));
    editor.Panels().Toggle(Panel::Console);
    CHECK_FALSE(editor.Panels().IsVisible(Panel::Console));
}

TEST_CASE("Task workspaces expose focused production tool sets", "[KairoEditor][Workspaces]")
{
    kairo::engine::Scene scene;
    EditorState editor(scene);
    editor.SwitchWorkspace(Workspace::Logic);
    CHECK(editor.Panels().IsVisible(Panel::Viewport));
    CHECK(editor.Panels().IsVisible(Panel::CodeEditor));
    CHECK(editor.Panels().IsVisible(Panel::NodeGraph));
    CHECK_FALSE(editor.Panels().IsVisible(Panel::Timeline));

    editor.SetAuthoringSurface(AuthoringSurface::Graph);
    CHECK_FALSE(editor.Panels().IsVisible(Panel::CodeEditor));
    CHECK(editor.Panels().IsVisible(Panel::NodeGraph));

    editor.SwitchWorkspace(Workspace::Animation);
    CHECK(editor.Panels().IsVisible(Panel::Timeline));
    CHECK(editor.Panels().IsVisible(Panel::CurveEditor));
    CHECK(editor.Panels().IsVisible(Panel::Sequencer));
}
