#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <cmath>

import Kairo.Editor.UI;
import Kairo.Editor.Theme;
import Kairo.Editor.TransformGizmo;
import Kairo.Foundation.Math;

using namespace kairo::editor;

TEST_CASE("Kairo UI design tokens reserve accent and semantic state colors", "[KairoEditor][UI]")
{
    const KairoUIDesignTokens& design = KairoUIDesign();
    CHECK(design.Accent.Blue > design.Accent.Green);
    CHECK(design.Accent.Green > design.Accent.Red);
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

namespace
{
    double Luminance(UIColor c)
    {
        const auto channel=[](double x){return x<=0.04045?x/12.92:std::pow((x+0.055)/1.055,2.4);};
        return .2126*channel(c.Red)+.7152*channel(c.Green)+.0722*channel(c.Blue);
    }
    double Contrast(UIColor foreground, UIColor background)
    {
        return (Luminance(foreground)+.05)/(Luminance(background)+.05);
    }
    struct Context
    {
        ImGuiContext* Value=ImGui::CreateContext();
        ~Context(){ImGui::DestroyContext(Value);}
    };
}

TEST_CASE("Studio theme preserves readable text and actual native widget states", "[KairoEditor][UI]")
{
    Context context;
    const auto& d=KairoUIDesign();
    CHECK(Contrast(d.Text,d.Background)>=7.0);
    CHECK(Contrast(d.Text,d.Selection)>=4.5);
    CHECK(Contrast(d.Text,d.Accent)>=4.5);
    CHECK(Contrast(d.MutedText,d.Surface)>=4.5);
    ApplyKairoEditorTheme();
    const auto& style=ImGui::GetStyle();
    CHECK(style.FrameBorderSize==1.f);
    CHECK(style.Colors[ImGuiCol_Header].z>style.Colors[ImGuiCol_Header].x);
    CHECK(style.Colors[ImGuiCol_CheckMark].z>style.Colors[ImGuiCol_CheckMark].x);
    CHECK(style.Colors[ImGuiCol_TabSelected].z>style.Colors[ImGuiCol_TabSelected].x);
    CHECK(style.Colors[ImGuiCol_TableRowBg].x!=style.Colors[ImGuiCol_TableRowBgAlt].x);
}

TEST_CASE("World gizmo preserves untouched local channels under a rotated scaled parent",
    "[KairoEditor][Gizmo]")
{
    using namespace kairo::foundation::math;
    Transformf parent;
    parent.Translation={8,2,-3};
    parent.Rotation=LookRotation(Vec3f{1,0,0},Vec3f{0,1,0});
    parent.Scale={2,3,4};
    Transformf original;
    original.Translation={1,2,3};original.Scale={1,2,3};
    auto world=parent*original;
    world.Translation={4,5,6};
    const auto moved=LocalGizmoChannel(parent,original,world,TransformGizmoOperation::Translate);
    CHECK(NearlyEqual((parent*moved).Translation,world.Translation,1e-4f));
    CHECK(moved.Rotation==original.Rotation);CHECK(moved.Scale==original.Scale);
    world.Rotation=LookRotation(Vec3f{0,1,1},Vec3f{0,1,0});
    const auto rotated=LocalGizmoChannel(parent,original,world,TransformGizmoOperation::Rotate);
    CHECK(NearlyEqual((parent*rotated).Forward(),world.Forward(),1e-4f));
    CHECK(rotated.Translation==original.Translation);CHECK(rotated.Scale==original.Scale);
    world.Scale={4,9,16};
    const auto scaled=LocalGizmoChannel(parent,original,world,TransformGizmoOperation::Scale);
    CHECK(NearlyEqual((parent*scaled).Scale,world.Scale,1e-4f));
    CHECK(scaled.Translation==original.Translation);CHECK(scaled.Rotation==original.Rotation);
}
