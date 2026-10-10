#include <catch2/catch_test_macros.hpp>

#include <algorithm>

import Kairo.Editor;
import Kairo.Editor.UI;

using namespace kairo::editor;

TEST_CASE("Kairo UI design tokens reserve accent and semantic state colors", "[KairoEditor][UI]")
{
    const KairoUIDesignTokens& design = KairoUIDesign();
    CHECK(design.Accent.Red > design.Accent.Blue);
    CHECK(design.Accent.Green > design.Accent.Blue);
    CHECK(design.Background.Blue > design.Background.Red);
    CHECK(design.Danger.Red > design.Danger.Green);
    CHECK(design.Success.Green > design.Success.Red);
    CHECK(design.Radius >= design.CompactRadius);
    CHECK(design.SpaceSmall < design.Space);
    CHECK(design.Space < design.SpaceLarge);
}

TEST_CASE("Kairo UI design tokens have stable process lifetime", "[KairoEditor][UI]")
{
    const auto* first = &KairoUIDesign();
    const auto* second = &KairoUIDesign();
    CHECK(first == second);
}

TEST_CASE("Standalone player launch has a stable semantic action and F6 binding",
    "[KairoEditor][Input][Player]")
{
    CHECK(Key(EditorAction::LaunchPlayer) == "launch-player");
    CHECK(BindingFor(EditorAction::LaunchPlayer).DisplayName == "Launch in KairoPlayer");
    CHECK(BindingFor(EditorAction::LaunchPlayer).Shortcut == "F6");
    REQUIRE(ParseEditorAction("launch-player").has_value());
    CHECK(*ParseEditorAction("launch-player") == EditorAction::LaunchPlayer);
    REQUIRE(ParseEditorKey("f6").has_value());
    CHECK(*ParseEditorKey("f6") == EditorKey::F6);

    const auto bindings = DefaultInputBindings(KeymapProfile::Kairo);
    const auto found = std::ranges::find_if(bindings, [](const ContextBinding& binding)
    {
        return binding.Action == EditorAction::LaunchPlayer;
    });
    REQUIRE(found != bindings.end());
    CHECK(found->Context == InputContext::Global);
    CHECK(found->Chord == InputChord{ EditorKey::F6, KeyModifiers::None });
}

TEST_CASE("F6 routes exactly once to standalone player launch", "[KairoEditor][Input][Player]")
{
    EditorInputRouter router;
    router.BeginFrame();
    router.SetContext(InputContext::Scene);
    REQUIRE(router.Route({ { EditorKey::F6, KeyModifiers::None } }));
    CHECK(router.Triggered(EditorAction::LaunchPlayer));
    CHECK(router.Consume(EditorAction::LaunchPlayer));
    CHECK_FALSE(router.Consume(EditorAction::LaunchPlayer));
    CHECK_FALSE(router.Triggered(EditorAction::TogglePlay));
}
