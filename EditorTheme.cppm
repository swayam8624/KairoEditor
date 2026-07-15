module;

#include <imgui.h>

#include <cmath>

export module Kairo.Editor.Theme;

import Kairo.Editor.UI;

namespace
{
    [[nodiscard]] ImVec4 DisplayColor(float red, float green, float blue, float alpha = 1.0f)
    {
        const auto linear = [](float channel) {
            return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        };
        return { linear(red), linear(green), linear(blue), alpha };
    }

    [[nodiscard]] ImVec4 DisplayColor(kairo::editor::UIColor color)
    {
        return DisplayColor(color.Red, color.Green, color.Blue, color.Alpha);
    }
}

export namespace kairo::editor
{
    /// Task: apply Kairo's compact, neutral production-tool visual language.
    /// The accent is reserved for selection and primary actions; surfaces use
    /// neutral values so scene/media colors remain legible and authoritative.
    void ApplyKairoEditorTheme()
    {
        const KairoUIDesignTokens& d = KairoUIDesign();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowPadding = { 10.0f, 10.0f };
        style.FramePadding = { 8.0f, 5.0f };
        style.CellPadding = { 6.0f, 4.0f };
        style.ItemSpacing = { 8.0f, 6.0f };
        style.ItemInnerSpacing = { 6.0f, 4.0f };
        style.IndentSpacing = 16.0f;
        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 10.0f;
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;
        style.WindowRounding = d.Radius;
        style.ChildRounding = d.CompactRadius;
        style.FrameRounding = d.CompactRadius;
        style.PopupRounding = d.Radius;
        style.ScrollbarRounding = d.CompactRadius;
        style.GrabRounding = d.CompactRadius;
        style.TabRounding = d.CompactRadius;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = DisplayColor(d.Text);
        c[ImGuiCol_TextDisabled] = DisplayColor(d.DisabledText);
        c[ImGuiCol_WindowBg] = DisplayColor(d.Background);
        c[ImGuiCol_ChildBg] = DisplayColor(d.Background);
        c[ImGuiCol_PopupBg] = DisplayColor(d.RaisedSurface);
        c[ImGuiCol_Border] = DisplayColor(d.Border);
        c[ImGuiCol_FrameBg] = DisplayColor(d.Surface);
        c[ImGuiCol_FrameBgHovered] = DisplayColor(d.HoverSurface);
        c[ImGuiCol_FrameBgActive] = DisplayColor(d.RaisedSurface);
        c[ImGuiCol_TitleBg] = DisplayColor(d.Background);
        c[ImGuiCol_TitleBgActive] = DisplayColor(d.Surface);
        c[ImGuiCol_MenuBarBg] = DisplayColor(d.Background);
        c[ImGuiCol_Button] = DisplayColor(d.Surface);
        c[ImGuiCol_ButtonHovered] = DisplayColor(d.HoverSurface);
        c[ImGuiCol_ButtonActive] = DisplayColor(d.RaisedSurface);
        c[ImGuiCol_Header] = DisplayColor(d.Accent);
        c[ImGuiCol_HeaderHovered] = DisplayColor(d.AccentHover);
        c[ImGuiCol_HeaderActive] = DisplayColor(d.AccentPressed);
        c[ImGuiCol_Tab] = DisplayColor(d.Surface);
        c[ImGuiCol_TabHovered] = DisplayColor(d.HoverSurface);
        c[ImGuiCol_TabSelected] = DisplayColor(d.Accent);
        c[ImGuiCol_DockingPreview] = DisplayColor(d.Accent.Red, d.Accent.Green, d.Accent.Blue, 0.65f);
        c[ImGuiCol_DockingEmptyBg] = { 0.0f, 0.0f, 0.0f, 0.0f };
        c[ImGuiCol_Separator] = DisplayColor(d.Border);
        c[ImGuiCol_ResizeGrip] = DisplayColor(d.Accent.Red, d.Accent.Green, d.Accent.Blue, 0.20f);
        c[ImGuiCol_ResizeGripHovered] = DisplayColor(d.Accent.Red, d.Accent.Green, d.Accent.Blue, 0.55f);
        c[ImGuiCol_ResizeGripActive] = DisplayColor(d.Accent);
    }
}
