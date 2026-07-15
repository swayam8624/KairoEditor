#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

import Kairo.Editor;
import Kairo.EngineCore;
import Kairo.Foundation.Math;

using namespace kairo::editor;

namespace
{
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
