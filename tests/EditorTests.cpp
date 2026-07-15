#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

import Kairo.Editor;
import Kairo.EngineCore;

using namespace kairo::editor;

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
