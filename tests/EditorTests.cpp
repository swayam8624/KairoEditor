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
