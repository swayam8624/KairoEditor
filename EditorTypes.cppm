module;

#include <cstdint>
#include <string_view>

export module Kairo.Editor.Types;

export namespace kairo::editor
{
    /// High-level editor mode. Play keeps the authored scene state separate
    /// from runtime execution; Pause only has meaning after play is active.
    enum class EditorMode : std::uint8_t { Edit, Play, Pause };

    /// Task-oriented workspaces reshape the editor around the current job
    /// while retaining the same scene, selection, undo history, and assets.
    enum class Workspace : std::uint8_t { Scene, World, Logic, Materials, Animation, Simulation, Audio, Profiling };

    /// Code and graph are two projections of one authored model. Split keeps
    /// both visible for users who learn visually but still need exact code.
    enum class AuthoringSurface : std::uint8_t { Code, Graph, CodeAndGraph };

    /// Explicit data-loss policy shared by scenes, assets, and structured
    /// authoring documents. Destructive lifecycle operations reject dirty state
    /// unless the caller has already obtained an intentional discard decision.
    enum class UnsavedChangesPolicy : std::uint8_t { Reject, Discard };

    enum class Panel : std::uint8_t
    {
        Hierarchy,
        Inspector,
        Viewport,
        ContentBrowser,
        Console,
        Statistics,
        CodeEditor,
        NodeGraph,
        Timeline,
        CurveEditor,
        PhysicsDebug,
        RendererDebug,
        WorldTools,
        Sequencer,
        AIAssistant,
        Count
    };

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
        case Panel::CodeEditor: return "Code";
        case Panel::NodeGraph: return "Graph";
        case Panel::Timeline: return "Timeline";
        case Panel::CurveEditor: return "Curves";
        case Panel::PhysicsDebug: return "Physics Debug";
        case Panel::RendererDebug: return "Renderer Debug";
        case Panel::WorldTools: return "World Tools";
        case Panel::Sequencer: return "Sequencer";
        case Panel::AIAssistant: return "Kairo AI";
        case Panel::Count: break;
        }
        return "Unknown";
    }

    [[nodiscard]] constexpr std::string_view Name(Workspace workspace) noexcept
    {
        switch (workspace)
        {
        case Workspace::Scene: return "Scene";
        case Workspace::World: return "World";
        case Workspace::Logic: return "Logic";
        case Workspace::Materials: return "Materials";
        case Workspace::Animation: return "Animation";
        case Workspace::Simulation: return "Simulation";
        case Workspace::Audio: return "Audio";
        case Workspace::Profiling: return "Profiling";
        }
        return "Unknown";
    }
}
