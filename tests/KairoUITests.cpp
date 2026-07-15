#include <catch2/catch_test_macros.hpp>

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
