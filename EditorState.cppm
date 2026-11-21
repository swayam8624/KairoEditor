// TEACHING MAP: BACKEND-NEUTRAL AUTHORING STATE: Scene belongs to KairoEngineCore, while this class holds selection and workspace/mode state. PanelState applies presets; EditorActions/SceneCommands implement edits; CommandHistory provides undo; EditorShell and ImGuiRuntime display through KairoRenderer. This class must not own a Vulkan/Metal device or scene persistence.
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
        /// TEACHING METHOD: The host supplies a Scene reference and must keep it alive; this object never owns/persists the scene.
        explicit EditorState(const kairo::engine::Scene& scene) : m_Scene(scene) {}

        /// TEACHING METHOD: Read the Play/Edit/Pause state; no simulation execution occurs here.
        [[nodiscard]] EditorMode Mode() const noexcept { return m_Mode; }
        /// TEACHING METHOD: Read the current authoring workspace preset; PanelState owns panel visibility.
        [[nodiscard]] Workspace ActiveWorkspace() const noexcept { return m_Workspace; }
        /// TEACHING METHOD: Read Code, Graph or Split mode; DocumentCompiler does actual content compilation.
        [[nodiscard]] AuthoringSurface ActiveAuthoringSurface() const noexcept { return m_AuthoringSurface; }
        /// TEACHING METHOD: Mutable access to workspace/panel settings only, not underlying scene data.
        [[nodiscard]] PanelState& Panels() noexcept { return m_Panels; }
        /// TEACHING METHOD: Const panel access for read-only UI or tests.
        [[nodiscard]] const PanelState& Panels() const noexcept { return m_Panels; }
        /// TEACHING METHOD: Read selected handle or nullopt; it remains subject to validity against Scene.
        [[nodiscard]] std::optional<kairo::engine::Entity> SelectedEntity() const noexcept { return m_Selected; }

        /// Task: switch the tool layout without changing authored data or play
        /// state. Workspace presets are starting points, not locked layouts.
// Presentation layout transition: switches panel presets without rewriting authored scene
// content.
        /// TEACHING METHOD: Change panel presets without editing authored scene data. Logic/material workspaces retain authoring surface preference.
        void SwitchWorkspace(Workspace workspace)
        {
            m_Workspace = workspace;
            m_Panels.ApplyWorkspacePreset(workspace);
            if (workspace == Workspace::Logic || workspace == Workspace::Materials)
                m_Panels.ApplyAuthoringSurface(m_AuthoringSurface);
        }

        /// Task: choose code, graph, or synchronized split authoring. This is
        /// intentionally state-only until the shared graph/code IR lands.
        /// TEACHING METHOD: Switch among text, graph and synchronized views; forward choice to PanelState.
        void SetAuthoringSurface(AuthoringSurface surface)
        {
            m_AuthoringSurface = surface;
            m_Panels.ApplyAuthoringSurface(surface);
        }

        /// Precondition: entity belongs to the scene supplied at construction.
        /// Task: select an entity for hierarchy, inspector, and viewport tools.
        /// TEACHING METHOD: Only accept an entity that Scene currently owns; throw on stale/out-of-scene selection.
        void Select(kairo::engine::Entity entity)
        {
            if (!m_Scene.Contains(entity)) throw std::out_of_range("KairoEditor cannot select an entity outside the active scene.");
            m_Selected = entity;
        }

        /// TEACHING METHOD: Remove selection without destroying any entity in EngineCore Scene.
        void ClearSelection() noexcept { m_Selected.reset(); }

        /// Task: transition edit -> play. Runtime-scene cloning is explicitly
        /// a host responsibility so KairoEditor never owns physics/render data.
        /// TEACHING METHOD: Enforce Edit->Play transition. Runtime cloning and physics preview startup are performed by other editor modules.
        void Play()
        {
            if (m_Mode != EditorMode::Edit) throw std::logic_error("KairoEditor can enter play mode only from edit mode.");
            m_Mode = EditorMode::Play;
        }
        /// TEACHING METHOD: Enforce Play->Pause transition; host simulation/physics playback is separate.
        void Pause()
        {
            if (m_Mode != EditorMode::Play) throw std::logic_error("KairoEditor can pause only while playing.");
            m_Mode = EditorMode::Pause;
        }
        /// TEACHING METHOD: Enforce Pause->Play transition; this does not advance simulation time.
        void Resume()
        {
            if (m_Mode != EditorMode::Pause) throw std::logic_error("KairoEditor can resume only while paused.");
            m_Mode = EditorMode::Play;
        }
        /// TEACHING METHOD: Restore Edit mode and clear selection; host manages preview-resource shutdown.
        void Stop() noexcept { m_Mode = EditorMode::Edit; ClearSelection(); }

        /// Task: clear a stale selection after external scene destruction.
        /// TEACHING METHOD: Discard stale selected entity when Scene no longer contains it; protects UI inspectors after deletes.
        void ValidateSelection() noexcept
        {
            if (m_Selected.has_value() && !m_Scene.Contains(*m_Selected)) m_Selected.reset();
        }

    private:
        const kairo::engine::Scene& m_Scene;
        EditorMode m_Mode = EditorMode::Edit;
        Workspace m_Workspace = Workspace::Scene;
        AuthoringSurface m_AuthoringSurface = AuthoringSurface::CodeAndGraph;
        PanelState m_Panels;
        std::optional<kairo::engine::Entity> m_Selected;
    };
}
