module;

#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>

export module Kairo.Editor.PanelState;

import Kairo.Editor.Types;

export namespace kairo::editor
{
    /// Persistent panel visibility used by the eventual docking frontend.
    /// Task: let UI code restore a deliberate workspace without putting any
    /// ImGui types or persistence format into the editor domain model.
    class PanelState final
    {
    public:
        PanelState() { ApplyWorkspacePreset(Workspace::Scene); }

        [[nodiscard]] bool IsVisible(Panel panel) const { return m_Visible.at(Index(panel)); }
        void SetVisible(Panel panel, bool visible) { m_Visible.at(Index(panel)) = visible; }
        void Toggle(Panel panel) { m_Visible.at(Index(panel)) = !m_Visible.at(Index(panel)); }

        /// Input: selected task workspace.
        /// Output: a coherent default set of visible panels.
        /// Task: provide useful layouts before Dear ImGui persists user docking
        /// customizations. A user can still toggle any panel afterward.
        void ApplyWorkspacePreset(Workspace workspace)
        {
            m_Visible.fill(false);
            switch (workspace)
            {
            case Workspace::Scene:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::Viewport, Panel::ContentBrowser,
                    Panel::Console, Panel::AIAssistant }); break;
            case Workspace::World:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::Viewport, Panel::ContentBrowser, Panel::WorldTools }); break;
            case Workspace::Logic:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::Viewport, Panel::CodeEditor,
                    Panel::NodeGraph, Panel::Console, Panel::AIAssistant }); break;
            case Workspace::Materials:
                Show({ Panel::Inspector, Panel::Viewport, Panel::ContentBrowser, Panel::CodeEditor, Panel::NodeGraph }); break;
            case Workspace::Animation:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::Viewport, Panel::Timeline, Panel::CurveEditor, Panel::Sequencer }); break;
            case Workspace::Simulation:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::Viewport, Panel::Statistics, Panel::PhysicsDebug }); break;
            case Workspace::Audio:
                Show({ Panel::Hierarchy, Panel::Inspector, Panel::ContentBrowser, Panel::NodeGraph, Panel::Timeline }); break;
            case Workspace::Profiling:
                Show({ Panel::Viewport, Panel::Console, Panel::Statistics, Panel::PhysicsDebug, Panel::RendererDebug }); break;
            }
        }

        /// Task: map one logical authoring asset to code, graph, or split UI.
        void ApplyAuthoringSurface(AuthoringSurface surface)
        {
            SetVisible(Panel::CodeEditor, surface != AuthoringSurface::Graph);
            SetVisible(Panel::NodeGraph, surface != AuthoringSurface::Code);
        }

    private:
        static constexpr std::size_t PanelCount = static_cast<std::size_t>(Panel::Count);
        std::array<bool, PanelCount> m_Visible{};

        void Show(std::initializer_list<Panel> panels)
        {
            for (const Panel panel : panels) SetVisible(panel, true);
        }

        [[nodiscard]] static constexpr std::size_t Index(Panel panel)
        {
            const auto index = static_cast<std::size_t>(panel);
            if (index >= PanelCount) throw std::out_of_range("Unknown KairoEditor panel.");
            return index;
        }
    };
}
