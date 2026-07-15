module;

#include <imgui.h>

#include <cmath>

export module Kairo.Editor.Theme;

namespace
{
    [[nodiscard]] ImVec4 DisplayColor(float red, float green, float blue, float alpha = 1.0f)
    {
        const auto linear = [](float channel) {
            return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
        };
        return { linear(red), linear(green), linear(blue), alpha };
    }
}

export namespace kairo::editor
{
    /// Task: apply Kairo's compact, neutral production-tool visual language.
    /// The accent is reserved for selection and primary actions; surfaces use
    /// neutral values so scene/media colors remain legible and authoritative.
    void ApplyKairoEditorTheme()
    {
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
        style.WindowRounding = 4.0f;
        style.ChildRounding = 3.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 4.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = DisplayColor(0.88f, 0.90f, 0.93f);
        c[ImGuiCol_TextDisabled] = DisplayColor(0.45f, 0.48f, 0.53f);
        c[ImGuiCol_WindowBg] = DisplayColor(0.075f, 0.080f, 0.090f, 0.98f);
        c[ImGuiCol_ChildBg] = DisplayColor(0.065f, 0.070f, 0.080f);
        c[ImGuiCol_PopupBg] = DisplayColor(0.090f, 0.095f, 0.108f, 0.99f);
        c[ImGuiCol_Border] = DisplayColor(0.19f, 0.20f, 0.23f);
        c[ImGuiCol_FrameBg] = DisplayColor(0.12f, 0.13f, 0.15f);
        c[ImGuiCol_FrameBgHovered] = DisplayColor(0.16f, 0.17f, 0.20f);
        c[ImGuiCol_FrameBgActive] = DisplayColor(0.19f, 0.20f, 0.24f);
        c[ImGuiCol_TitleBg] = DisplayColor(0.055f, 0.060f, 0.070f);
        c[ImGuiCol_TitleBgActive] = DisplayColor(0.080f, 0.085f, 0.098f);
        c[ImGuiCol_MenuBarBg] = DisplayColor(0.050f, 0.055f, 0.064f);
        c[ImGuiCol_Button] = DisplayColor(0.12f, 0.13f, 0.15f);
        c[ImGuiCol_ButtonHovered] = DisplayColor(0.18f, 0.20f, 0.23f);
        c[ImGuiCol_ButtonActive] = DisplayColor(0.16f, 0.43f, 0.68f);
        c[ImGuiCol_Header] = DisplayColor(0.12f, 0.31f, 0.49f, 0.75f);
        c[ImGuiCol_HeaderHovered] = DisplayColor(0.15f, 0.39f, 0.61f, 0.85f);
        c[ImGuiCol_HeaderActive] = DisplayColor(0.17f, 0.46f, 0.72f);
        c[ImGuiCol_Tab] = DisplayColor(0.080f, 0.085f, 0.098f);
        c[ImGuiCol_TabHovered] = DisplayColor(0.14f, 0.36f, 0.56f);
        c[ImGuiCol_TabSelected] = DisplayColor(0.12f, 0.31f, 0.49f);
        c[ImGuiCol_DockingPreview] = DisplayColor(0.18f, 0.48f, 0.75f, 0.65f);
        c[ImGuiCol_DockingEmptyBg] = { 0.0f, 0.0f, 0.0f, 0.0f };
        c[ImGuiCol_Separator] = DisplayColor(0.18f, 0.19f, 0.22f);
        c[ImGuiCol_ResizeGrip] = DisplayColor(0.18f, 0.48f, 0.75f, 0.20f);
        c[ImGuiCol_ResizeGripHovered] = DisplayColor(0.18f, 0.48f, 0.75f, 0.55f);
        c[ImGuiCol_ResizeGripActive] = DisplayColor(0.18f, 0.48f, 0.75f, 0.85f);
    }
}
