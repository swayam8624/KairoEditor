#include <catch2/catch_test_macros.hpp>

import Kairo.Editor;
import Kairo.EngineCore;

using namespace kairo::editor;

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
