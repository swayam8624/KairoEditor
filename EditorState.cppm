module;

#include <optional>
#include <stdexcept>

export module Kairo.Editor.State;

import Kairo.EngineCore.Entity;
import Kairo.EngineCore.Scene;
import Kairo.Editor.Types;
import Kairo.Editor.PanelState;

export namespace kairo::editor
{
    /// Backend-independent editor session state.
    /// Input: a non-owning active Scene supplied by the application host.
    /// Output: selection, play/edit state, and panel workspace settings.
    /// Task: hold authoring decisions independently from ImGui, Vulkan, or a
    /// future retained UI toolkit. The host owns scene persistence and runtime
    /// cloning; this class enforces only immediate selection validity.
    class EditorState final
    {
    public:
        explicit EditorState(kairo::engine::Scene& scene) : m_Scene(scene) {}

        [[nodiscard]] EditorMode Mode() const noexcept { return m_Mode; }
        [[nodiscard]] Workspace ActiveWorkspace() const noexcept { return m_Workspace; }
        [[nodiscard]] AuthoringSurface ActiveAuthoringSurface() const noexcept { return m_AuthoringSurface; }
        [[nodiscard]] PanelState& Panels() noexcept { return m_Panels; }
        [[nodiscard]] const PanelState& Panels() const noexcept { return m_Panels; }
        [[nodiscard]] std::optional<kairo::engine::Entity> SelectedEntity() const noexcept { return m_Selected; }

        /// Task: switch the tool layout without changing authored data or play
        /// state. Workspace presets are starting points, not locked layouts.
        void SwitchWorkspace(Workspace workspace)
        {
            m_Workspace = workspace;
            m_Panels.ApplyWorkspacePreset(workspace);
            if (workspace == Workspace::Logic || workspace == Workspace::Materials)
                m_Panels.ApplyAuthoringSurface(m_AuthoringSurface);
        }

        /// Task: choose code, graph, or synchronized split authoring. This is
        /// intentionally state-only until the shared graph/code IR lands.
        void SetAuthoringSurface(AuthoringSurface surface)
        {
            m_AuthoringSurface = surface;
            m_Panels.ApplyAuthoringSurface(surface);
        }

        /// Precondition: entity belongs to the scene supplied at construction.
        /// Task: select an entity for hierarchy, inspector, and viewport tools.
        void Select(kairo::engine::Entity entity)
        {
            if (!m_Scene.Contains(entity)) throw std::out_of_range("KairoEditor cannot select an entity outside the active scene.");
            m_Selected = entity;
        }

        void ClearSelection() noexcept { m_Selected.reset(); }

        /// Task: transition edit -> play. Runtime-scene cloning is explicitly
        /// a host responsibility so KairoEditor never owns physics/render data.
        void Play()
        {
            if (m_Mode != EditorMode::Edit) throw std::logic_error("KairoEditor can enter play mode only from edit mode.");
            m_Mode = EditorMode::Play;
        }
        void Pause()
        {
            if (m_Mode != EditorMode::Play) throw std::logic_error("KairoEditor can pause only while playing.");
            m_Mode = EditorMode::Pause;
        }
        void Resume()
        {
            if (m_Mode != EditorMode::Pause) throw std::logic_error("KairoEditor can resume only while paused.");
            m_Mode = EditorMode::Play;
        }
        void Stop() noexcept { m_Mode = EditorMode::Edit; ClearSelection(); }

        /// Task: clear a stale selection after external scene destruction.
        void ValidateSelection() noexcept
        {
            if (m_Selected.has_value() && !m_Scene.Contains(*m_Selected)) m_Selected.reset();
        }

    private:
        kairo::engine::Scene& m_Scene;
        EditorMode m_Mode = EditorMode::Edit;
        Workspace m_Workspace = Workspace::Scene;
        AuthoringSurface m_AuthoringSurface = AuthoringSurface::CodeAndGraph;
        PanelState m_Panels;
        std::optional<kairo::engine::Entity> m_Selected;
    };
}
