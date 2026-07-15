module;

#include <imgui.h>

#include <cmath>
#include <cstddef>
#include <string>
#include <string_view>

export module Kairo.Editor.UI;

export namespace kairo::editor
{
    /// sRGB color token. Input: normalized sRGB channels. Output: an immutable
    /// design-system value. Task: keep visual policy independent from Dear
    /// ImGui so a later Qt or custom backend can preserve the Kairo language.
    struct UIColor final
    {
        float Red = 0.0f;
        float Green = 0.0f;
        float Blue = 0.0f;
        float Alpha = 1.0f;
    };

    /// All product-facing editor colors and compact layout constants. Accent
    /// yellow is intentionally scarce: selection, primary commands, and
    /// active tools only. Panels remain neutral so authored media is dominant.
    struct KairoUIDesignTokens final
    {
        UIColor Background{ 0.043f, 0.051f, 0.063f };
        UIColor Surface{ 0.071f, 0.086f, 0.106f };
        UIColor RaisedSurface{ 0.090f, 0.110f, 0.133f };
        UIColor HoverSurface{ 0.125f, 0.153f, 0.188f };
        UIColor Border{ 0.169f, 0.200f, 0.243f };
        UIColor Text{ 0.961f, 0.969f, 0.980f };
        UIColor MutedText{ 0.620f, 0.659f, 0.714f };
        UIColor DisabledText{ 0.447f, 0.486f, 0.533f };
        UIColor Accent{ 0.949f, 0.788f, 0.298f };
        UIColor AccentHover{ 1.000f, 0.847f, 0.420f };
        UIColor AccentPressed{ 0.710f, 0.557f, 0.094f };
        UIColor Success{ 0.275f, 0.765f, 0.482f };
        UIColor Warning{ 1.000f, 0.690f, 0.125f };
        UIColor Danger{ 1.000f, 0.365f, 0.365f };
        UIColor Info{ 0.302f, 0.639f, 1.000f };
        float Radius = 6.0f;
        float CompactRadius = 4.0f;
        float SpaceSmall = 4.0f;
        float Space = 8.0f;
        float SpaceLarge = 12.0f;
    };

    /// Output: immutable editor visual tokens. Task: establish one source of
    /// truth for the ImGui theme and semantic widgets; feature panels should
    /// never introduce arbitrary visual constants for ordinary states.
    [[nodiscard]] const KairoUIDesignTokens& KairoUIDesign() noexcept;

    enum class UIButtonTone : unsigned char { Neutral, Primary, Destructive };
    enum class UIStatusTone : unsigned char { Neutral, Success, Warning, Danger, Info };

    /// Input: visible label, semantic command tone, and enabled state.
    /// Output: true exactly once for an enabled click.
    /// Task: draw a consistent command control without exposing raw ImGui
    /// styling to feature code. Labels are copied to accept arbitrary views.
    [[nodiscard]] bool ActionButton(std::string_view label,
        UIButtonTone tone = UIButtonTone::Neutral, bool enabled = true);

    /// Input: workspace/tool label, active state, and enabled state.
    /// Output: true exactly once for an enabled click.
    /// Task: provide fixed-height segmented controls for the main toolbar.
    [[nodiscard]] bool ToolbarButton(std::string_view label, bool active, bool enabled = true);

    /// Input: a stable ImGui identifier, placeholder, and mutable UTF-8 buffer.
    /// Output: true when the text changed this frame.
    /// Task: render full-width filtering/search controls with a consistent
    /// field treatment. The caller owns buffer capacity and null termination.
    [[nodiscard]] bool SearchField(std::string_view identifier, std::string_view placeholder,
        char* buffer, std::size_t bufferSize);

    /// Task: draw a compact inspector/document section heading with Kairo's
    /// spacing and border conventions.
    void SectionHeader(std::string_view label);

    /// Task: draw a visually subordinate label for metadata and empty states.
    void MutedText(std::string_view text);

    /// Task: display a compact semantic status label without allowing panels
    /// to invent success/warning/error colors ad hoc.
    void StatusText(std::string_view text, UIStatusTone tone = UIStatusTone::Neutral);
}

namespace kairo::editor::ui_detail
{
    [[nodiscard]] ImVec4 ToLinear(UIColor color)
    {
        const auto channel = [](float value)
        {
            return value <= 0.04045f ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        return { channel(color.Red), channel(color.Green), channel(color.Blue), color.Alpha };
    }

    [[nodiscard]] ImVec4 StatusColor(UIStatusTone tone)
    {
        const auto& design = KairoUIDesign();
        switch (tone)
        {
        case UIStatusTone::Success: return ToLinear(design.Success);
        case UIStatusTone::Warning: return ToLinear(design.Warning);
        case UIStatusTone::Danger: return ToLinear(design.Danger);
        case UIStatusTone::Info: return ToLinear(design.Info);
        case UIStatusTone::Neutral: return ToLinear(design.MutedText);
        }
        return ToLinear(design.MutedText);
    }

    [[nodiscard]] std::string Owned(std::string_view text) { return std::string(text); }
}

namespace kairo::editor
{
    const KairoUIDesignTokens& KairoUIDesign() noexcept
    {
        static constexpr KairoUIDesignTokens design{};
        return design;
    }

    bool ActionButton(std::string_view label, UIButtonTone tone, bool enabled)
    {
        using namespace ui_detail;
        const auto& design = KairoUIDesign();
        const std::string ownedLabel = Owned(label);
        if (!enabled) ImGui::BeginDisabled();

        if (tone == UIButtonTone::Primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ToLinear(design.Accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToLinear(design.AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToLinear(design.AccentPressed));
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Background));
        }
        else if (tone == UIButtonTone::Destructive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ToLinear(design.Danger));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToLinear(design.Warning));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToLinear(design.Danger));
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Background));
        }

        const bool clicked = ImGui::Button(ownedLabel.c_str());
        if (tone != UIButtonTone::Neutral) ImGui::PopStyleColor(4);
        if (!enabled) ImGui::EndDisabled();
        return enabled && clicked;
    }

    bool ToolbarButton(std::string_view label, bool active, bool enabled)
    {
        using namespace ui_detail;
        const auto& design = KairoUIDesign();
        const std::string ownedLabel = Owned(label);
        if (!enabled) ImGui::BeginDisabled();
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ToLinear(design.Accent));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToLinear(design.AccentHover));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToLinear(design.AccentPressed));
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Background));
        }
        const bool clicked = ImGui::Button(ownedLabel.c_str(), { 0.0f, ImGui::GetFrameHeight() });
        if (active) ImGui::PopStyleColor(4);
        if (!enabled) ImGui::EndDisabled();
        return enabled && clicked;
    }

    bool SearchField(std::string_view identifier, std::string_view placeholder,
        char* buffer, std::size_t bufferSize)
    {
        using namespace ui_detail;
        const auto& design = KairoUIDesign();
        const std::string ownedIdentifier = Owned(identifier);
        const std::string ownedPlaceholder = Owned(placeholder);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ToLinear(design.RaisedSurface));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ToLinear(design.HoverSurface));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ToLinear(design.HoverSurface));
        ImGui::SetNextItemWidth(-1.0f);
        const bool changed = ImGui::InputTextWithHint(ownedIdentifier.c_str(), ownedPlaceholder.c_str(), buffer, bufferSize);
        ImGui::PopStyleColor(3);
        return changed;
    }

    void SectionHeader(std::string_view label)
    {
        using namespace ui_detail;
        const std::string ownedLabel = Owned(label);
        ImGui::PushStyleColor(ImGuiCol_Separator, ToLinear(KairoUIDesign().Border));
        ImGui::SeparatorText(ownedLabel.c_str());
        ImGui::PopStyleColor();
    }

    void MutedText(std::string_view text)
    {
        const std::string ownedText = ui_detail::Owned(text);
        ImGui::TextDisabled("%s", ownedText.c_str());
    }

    void StatusText(std::string_view text, UIStatusTone tone)
    {
        const std::string ownedText = ui_detail::Owned(text);
        ImGui::PushStyleColor(ImGuiCol_Text, ui_detail::StatusColor(tone));
        ImGui::TextUnformatted(ownedText.c_str());
        ImGui::PopStyleColor();
    }
}
