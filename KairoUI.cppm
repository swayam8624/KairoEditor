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
    /// cobalt is intentionally scarce: selection, primary commands, and
    /// active tools only. Panels remain neutral so authored media is dominant.
    struct KairoUIDesignTokens final
    {
        UIColor Background{ 0.102f, 0.110f, 0.122f }; // #1a1c1f
        UIColor Surface{ 0.145f, 0.157f, 0.173f }; // #25282c
        UIColor RaisedSurface{ 0.122f, 0.133f, 0.149f };
        UIColor HoverSurface{ 0.196f, 0.212f, 0.235f };
        UIColor Border{ 0.224f, 0.239f, 0.263f };
        UIColor Text{ 0.933f, 0.941f, 0.957f };
        UIColor MutedText{ 0.678f, 0.706f, 0.749f };
        UIColor DisabledText{ 0.490f, 0.518f, 0.561f };
        UIColor Accent{ 0.141f, 0.353f, 0.753f };
        UIColor AccentHover{ 0.161f, 0.400f, 0.800f };
        UIColor AccentPressed{ 0.110f, 0.282f, 0.620f };
        UIColor Selection{ 0.157f, 0.282f, 0.455f };
        UIColor Success{ 0.400f, 0.749f, 0.525f };
        UIColor Warning{ 0.875f, 0.686f, 0.337f };
        UIColor Danger{ 0.875f, 0.380f, 0.380f };
        UIColor Info{ 0.400f, 0.659f, 0.957f };
        float Radius = 4.0f;
        float CompactRadius = 3.0f;
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
        UIButtonTone tone = UIButtonTone::Neutral, bool enabled = true,
        float width = 0.0f);

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

    bool ActionButton(std::string_view label, UIButtonTone tone, bool enabled, float width)
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
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Text));
        }
        else if (tone == UIButtonTone::Destructive)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ToLinear(design.Surface));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ToLinear(design.HoverSurface));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ToLinear(design.RaisedSurface));
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Danger));
        }

        const bool clicked = ImGui::Button(ownedLabel.c_str(), { width, 0.0f });
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
            ImGui::PushStyleColor(ImGuiCol_Text, ToLinear(design.Text));
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
