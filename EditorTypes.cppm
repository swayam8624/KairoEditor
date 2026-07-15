module;

#include <cstdint>
#include <string_view>

export module Kairo.Editor.Types;

export namespace kairo::editor
{
    /// High-level editor mode. Play keeps the authored scene state separate
    /// from runtime execution; Pause only has meaning after play is active.
    enum class EditorMode : std::uint8_t { Edit, Play, Pause };
    enum class Panel : std::uint8_t { Hierarchy, Inspector, Viewport, ContentBrowser, Console, Statistics };

    [[nodiscard]] constexpr std::string_view Name(Panel panel) noexcept
    {
        switch (panel)
        {
        case Panel::Hierarchy: return "Hierarchy";
        case Panel::Inspector: return "Inspector";
        case Panel::Viewport: return "Viewport";
        case Panel::ContentBrowser: return "Content Browser";
        case Panel::Console: return "Console";
        case Panel::Statistics: return "Statistics";
        }
        return "Unknown";
    }
}
