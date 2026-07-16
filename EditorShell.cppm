module;

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

export module Kairo.Editor.ImGuiShell;

import Kairo.Editor;
import Kairo.Editor.ImGuiGraphCanvas;
import Kairo.Editor.ImGuiReflectionInspector;
import Kairo.Editor.UI;
import Kairo.Editor.PhysicsPreview;
import Kairo.EngineCore;
import Kairo.EngineCore.Reflection;
import Kairo.Reflection;
import Kairo.Foundation.Math.Quaternion;
import Kairo.Renderer.DebugDraw;

export namespace kairo::editor
{
    /// Draws the production editor shell from backend-neutral EditorState and
    /// Scene data. Docking/layout are ImGui concerns; selection and workspace
    /// transitions remain in KairoEditor core and are independently testable.
    class EditorShell final
    {
    public:
        EditorShell(EditorState& state, ProjectSession& project)
            : m_State(state), m_Project(project), m_GraphCanvas(m_Schemas)
        {
            kairo::engine::RegisterEngineCoreReflection(m_Reflection);
            if (const auto active = m_Project.Documents().ActiveID(); active.has_value())
            {
                const auto& document = m_Project.Document(*active);
                (void)m_AuthoringWorkspace.Open(document);
                m_State.SwitchWorkspace(WorkspaceFor(document.Kind()));
                m_State.SetAuthoringSurface(AuthoringSurface::CodeAndGraph);
            }
        }

        void Draw()
        {
            m_State.ValidateSelection();
            DrawMainBar();
            DrawStatusBar();
            const ImGuiID dockspace = ImGui::GetID("KairoEditorDockspace");
            BuildDefaultLayout(dockspace);
            ImGui::DockSpaceOverViewport(dockspace, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
            DrawVisiblePanels();
            if (m_Project.HasProject() && m_ViewportFocused && !ImGui::GetIO().WantTextInput)
                HandleViewportShortcuts();
            if (m_State.Mode() == EditorMode::Play && m_RuntimeScene.has_value())
                RunCommand([this] { m_PhysicsPreview.Step(*m_RuntimeScene, ImGui::GetIO().DeltaTime); });
            DrawDocumentLifecyclePopups();
            DrawErrorPopup();
        }

        /// Output: current editor-owned navigation pose. The application host
        /// adapts this to KairoRenderer after UI construction, keeping ImGui
        /// input and Vulkan camera uploads in separate modules.
        [[nodiscard]] ViewportCameraPose ViewportCamera() const noexcept
        {
            return m_ViewportController.Pose();
        }

        [[nodiscard]] const kairo::engine::Scene& RenderScene() const noexcept
        {
            return m_RuntimeScene.has_value() ? *m_RuntimeScene : m_Project.Scene();
        }

        [[nodiscard]] kairo::renderer::DebugDrawList PhysicsDebugDraw() const
        {
            return m_PhysicsPreview.Active() ? m_PhysicsPreview.DebugDraw(m_ShowPhysicsBroadphase)
                : kairo::renderer::DebugDrawList{};
        }

        void SetViewportTexture(ImTextureID texture) noexcept { m_ViewportTexture = texture; }

        [[nodiscard]] std::pair<std::uint32_t, std::uint32_t> RequestedViewportExtent() const noexcept
        {
            return { m_RequestedViewportWidth, m_RequestedViewportHeight };
        }

        /// Output: newest physical-pixel click in the rendered scene panel.
        [[nodiscard]] std::optional<std::pair<std::uint32_t, std::uint32_t>> TakeViewportPickRequest() noexcept
        {
            return std::exchange(m_ViewportPickRequest, std::nullopt);
        }

        /// Input: stable renderer object ID, where zero denotes background.
        /// Task: apply GPU picking only when the ID still belongs to this scene.
        void ApplyViewportPick(std::uint32_t objectID)
        {
            if (objectID == 0u) { m_State.ClearSelection(); return; }
            const kairo::engine::Entity entity{ objectID };
            if (m_Project.Scene().Contains(entity)) m_State.Select(entity);
            else m_State.ClearSelection();
        }

    private:
        EditorState& m_State;
        ProjectSession& m_Project;
        CommandHistory m_History;
        kairo::reflection::ReflectionRegistry m_Reflection;
        AuthoringWorkspaceState m_AuthoringWorkspace;
        DocumentSchemaRegistry m_Schemas = CreateCoreDocumentSchemaRegistry();
        ImGuiGraphCanvas m_GraphCanvas;
        ViewportController m_ViewportController;
        PhysicsPreview m_PhysicsPreview;
        std::optional<kairo::engine::Scene> m_RuntimeScene;
        EditorAction m_ActiveTool = EditorAction::SelectTool;
        bool m_ViewportFocused = false;
        bool m_ShowPhysicsBroadphase = false;
        bool m_LayoutBuilt = false;
        bool m_DocumentPanelFocused = false;
        std::array<char, 256> m_AssetFilter{};
        std::array<char, 256> m_NewDocumentName{};
        std::array<char, 512> m_NewDocumentPath{};
        int m_NewDocumentKind = 0;
        bool m_RequestNewDocumentPopup = false;
        bool m_RequestCloseDocumentPopup = false;
        std::optional<kairo::assets::AssetID> m_PendingDocumentClose;
        std::string m_LastError;
        bool m_RequestErrorPopup = false;
        ImTextureID m_ViewportTexture = ImTextureID_Invalid;
        std::uint32_t m_RequestedViewportWidth = 1u;
        std::uint32_t m_RequestedViewportHeight = 1u;
        std::optional<std::pair<std::uint32_t, std::uint32_t>> m_ViewportPickRequest;

        void DrawMainBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Document...", "Cmd+N", false, m_Project.HasProject()))
                    RequestNewDocument();
                ImGui::Separator();
                const auto activeDocument = m_Project.HasProject()
                    ? m_Project.Documents().ActiveID() : std::nullopt;
                if (ImGui::MenuItem("Save Active Document", "Cmd+S", false, activeDocument.has_value()))
                    RunCommand([this, id = *activeDocument] { SaveDocumentWithDraft(id); });
                if (ImGui::MenuItem("Close Active Document", "Cmd+W", false, activeDocument.has_value()))
                    RequestCloseDocument(*activeDocument);
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", nullptr, false, m_Project.HasProject()))
                    RunCommand([this] { m_Project.SaveScene(); });
                if (ImGui::MenuItem("Save All", "Cmd+Option+S", false, m_Project.HasProject()))
                    RunCommand([this] { SaveAllWithDrafts(); });
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit"))
            {
                CommandHistory& history = ActiveHistory();
                const std::string undo = history.CanUndo()
                    ? "Undo " + std::string(history.UndoName()) : "Undo";
                const std::string redo = history.CanRedo()
                    ? "Redo " + std::string(history.RedoName()) : "Redo";
                if (ImGui::MenuItem(undo.c_str(), "Cmd+Z", false, history.CanUndo()))
                    RunCommand([&history] { history.Undo(); });
                if (ImGui::MenuItem(redo.c_str(), "Cmd+Shift+Z", false, history.CanRedo()))
                    RunCommand([&history] { history.Redo(); });
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                for (std::uint8_t value = 0u; value < static_cast<std::uint8_t>(Panel::Count); ++value)
                {
                    const Panel panel = static_cast<Panel>(value);
                    bool visible = m_State.Panels().IsVisible(panel);
                    if (ImGui::MenuItem(Name(panel).data(), nullptr, &visible)) m_State.Panels().SetVisible(panel, visible);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            constexpr std::array workspaces{ Workspace::Scene, Workspace::World, Workspace::Logic, Workspace::Materials,
                Workspace::Animation, Workspace::Simulation, Workspace::Audio, Workspace::Profiling };
            for (const Workspace workspace : workspaces)
            {
                const bool isActive = workspace == m_State.ActiveWorkspace();
                if (ToolbarButton(Name(workspace), isActive)) m_State.SwitchWorkspace(workspace);
                ImGui::SameLine();
            }
            ImGui::Separator();
            DrawPlayControls();
            if (m_Project.HasProject())
            {
                ImGui::Separator();
                ImGui::TextDisabled("%s%s", m_Project.Descriptor().Name.c_str(),
                    (m_Project.HasUnsavedChanges() || m_AuthoringWorkspace.HasDirtyTextDrafts()) ? " *" : "");
            }
            ImGui::EndMainMenuBar();

            if (m_Project.HasProject() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_N))
                RequestNewDocument();
            if (m_Project.HasProject() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_S))
            {
                const auto active = m_Project.Documents().ActiveID();
                if (m_DocumentPanelFocused && active.has_value())
                    RunCommand([this, id = *active] { SaveDocumentWithDraft(id); });
                else RunCommand([this] { m_Project.SaveScene(); });
            }
            if (m_Project.HasProject() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_W))
            {
                const auto active = m_Project.Documents().ActiveID();
                if (m_DocumentPanelFocused && active.has_value()) RequestCloseDocument(*active);
            }
            if (m_Project.HasProject() &&
                ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiMod_Alt | ImGuiKey_S))
            {
                RunCommand([this] { SaveAllWithDrafts(); });
            }
            CommandHistory& history = ActiveHistory();
            if (history.CanRedo() &&
                ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiMod_Shift | ImGuiKey_Z))
                RunCommand([&history] { history.Redo(); });
            else if (history.CanUndo() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_Z))
                RunCommand([&history] { history.Undo(); });

        }

        void DrawPlayControls()
        {
            if (m_State.Mode() == EditorMode::Edit)
            {
                if (ActionButton("Play", UIButtonTone::Primary)) StartPlay();
            }
            else
            {
                if (ActionButton("Stop", UIButtonTone::Destructive)) StopPlay();
                ImGui::SameLine();
                if (m_State.Mode() == EditorMode::Play && ActionButton("Pause")) m_State.Pause();
                else if (m_State.Mode() == EditorMode::Pause && ActionButton("Resume", UIButtonTone::Primary)) m_State.Resume();
            }
        }

        void DrawStatusBar()
        {
            constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
            if (!ImGui::BeginViewportSideBar("##KairoStatusBar", nullptr, ImGuiDir_Down,
                ImGui::GetFrameHeight(), flags))
            {
                ImGui::End();
                return;
            }
            if (ImGui::BeginMenuBar())
            {
                ImGui::TextUnformatted(m_Project.ActiveScenePath().generic_string().c_str());
                ImGui::Separator();
                ImGui::TextDisabled("%zu entities", m_Project.Scene().Size());
                ImGui::Separator();
                ImGui::TextDisabled("%zu assets", m_Project.Assets().Size());
                if (const auto active = m_Project.Documents().ActiveID(); active.has_value())
                {
                    const auto& document = m_Project.Document(*active);
                    ImGui::Separator();
                    ImGui::TextDisabled("%s%s", document.Name().c_str(),
                        (m_Project.Documents().IsDirty(*active) ||
                         (m_AuthoringWorkspace.Contains(*active) &&
                          m_AuthoringWorkspace.At(*active).IsTextDirty())) ? " *" : "");
                }
                if (const auto selected = m_State.SelectedEntity(); selected.has_value())
                {
                    ImGui::Separator();
                    ImGui::Text("Selected: %s", m_Project.Scene().Name(*selected).Value.c_str());
                }
                ImGui::Separator();
                ImGui::TextDisabled("%s", m_State.Mode() == EditorMode::Edit ? "Edit" :
                    (m_State.Mode() == EditorMode::Play ? "Playing" : "Paused"));
                ImGui::EndMenuBar();
            }
            ImGui::End();
        }

        void BuildDefaultLayout(ImGuiID dockspace)
        {
            if (m_LayoutBuilt) return;
            ImGui::DockBuilderRemoveNode(dockspace);
            ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->WorkSize);
            ImGuiID center = dockspace;
            const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, nullptr, &center);
            const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.23f, nullptr, &center);
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);
            ImGui::DockBuilderDockWindow("Hierarchy", left);
            ImGui::DockBuilderDockWindow("Content Browser", left);
            ImGui::DockBuilderDockWindow("Inspector", right);
            ImGui::DockBuilderDockWindow("Statistics", right);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderDockWindow("Timeline", bottom);
            ImGui::DockBuilderDockWindow("Code", bottom);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderDockWindow("Graph", center);
            ImGui::DockBuilderFinish(dockspace);
            m_LayoutBuilt = true;
        }

        void DrawVisiblePanels()
        {
            m_DocumentPanelFocused = false;
            m_ViewportFocused = false;
            if (m_State.Panels().IsVisible(Panel::Hierarchy)) DrawHierarchy();
            if (m_State.Panels().IsVisible(Panel::Inspector)) DrawInspector();
            if (m_State.Panels().IsVisible(Panel::Viewport)) DrawViewport();
            for (std::uint8_t value = static_cast<std::uint8_t>(Panel::ContentBrowser); value < static_cast<std::uint8_t>(Panel::Count); ++value)
            {
                const Panel panel = static_cast<Panel>(value);
                if (panel == Panel::Inspector || panel == Panel::Viewport || !m_State.Panels().IsVisible(panel)) continue;
                DrawToolPanel(panel);
            }
        }

        void DrawHierarchy()
        {
            if (!ImGui::Begin("Hierarchy")) { ImGui::End(); return; }
            if (ActionButton("+ Entity", UIButtonTone::Primary))
            {
                auto command = std::make_unique<CreateEntityCommand>(m_Project, "Entity");
                auto* created = command.get();
                RunCommand([this, &command] { m_History.Execute(std::move(command)); });
                if (command == nullptr) m_State.Select(created->CreatedEntity());
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create entity");
            ImGui::SameLine();
            const auto selectedEntity = m_State.SelectedEntity();
            if (ActionButton("- Entity", UIButtonTone::Destructive, selectedEntity.has_value()) && selectedEntity.has_value())
            {
                RunCommand([this, entity = *selectedEntity]
                {
                    m_History.Execute(std::make_unique<DeleteEntityCommand>(m_Project, entity));
                    m_State.ClearSelection();
                });
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected entity");
            ImGui::Separator();
            const auto& scene = m_Project.Scene();
            for (const auto entity : scene.Entities())
            {
                const bool selected = m_State.SelectedEntity().has_value() && *m_State.SelectedEntity() == entity;
                const std::string label = scene.Name(entity).Value + "##" + std::to_string(entity.Value);
                if (ImGui::Selectable(label.c_str(), selected)) m_State.Select(entity);
            }
            ImGui::End();
        }

        void DrawInspector()
        {
            if (!ImGui::Begin("Inspector")) { ImGui::End(); return; }
            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value())
            {
                ImGui::TextDisabled("Select an entity to inspect it.");
                ImGui::End(); return;
            }
            auto& scene = m_Project.EditScene();
            DrawReflectedInspector(m_Reflection, scene, *selected,
                [this, entity = *selected](std::string_view typeKey, std::string_view propertyKey,
                    const kairo::reflection::PropertyValue& value)
                {
                    RunCommand([this, entity, type = std::string(typeKey), property = std::string(propertyKey), value]
                    {
                        m_History.Execute(std::make_unique<SetReflectedPropertyCommand>(
                            m_Reflection, m_Project, entity, type, property, value));
                    });
                });
            auto transform = scene.Transform(*selected).Local;
            SectionHeader("Transform");
            bool changed = ImGui::DragFloat3("Position", &transform.Translation.x, 0.05f);
            changed |= ImGui::DragFloat3("Scale", &transform.Scale.x, 0.02f, 0.001f, 1000.0f,
                "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (changed)
            {
                RunCommand([this, entity = *selected, transform]
                {
                    m_History.Execute(std::make_unique<SetEntityTransformCommand>(m_Project, entity, transform));
                });
            }
            ImGui::TextDisabled("Rotation is stored as a normalized quaternion.");
            SectionHeader("Physics Preview");
            const bool physicsEnabled = scene.HasRigidBody(*selected) || scene.HasCollider(*selected);
            if (ActionButton(physicsEnabled ? "Remove Physics" : "Add Dynamic Box Physics",
                physicsEnabled ? UIButtonTone::Destructive : UIButtonTone::Primary))
            {
                RunCommand([this, entity = *selected, enabled = !physicsEnabled]
                {
                    m_History.Execute(std::make_unique<SetPhysicsPreviewCommand>(m_Project, entity, enabled));
                });
            }
            ImGui::TextDisabled(physicsEnabled ? "Dynamic box collider on Play" : "Uses local scale for runtime box bounds");
            ImGui::End();
        }

        void DrawViewport()
        {
            if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse))
            {
                m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const auto camera = m_ViewportController.Pose();
                ImGui::TextDisabled("Perspective  |  Lit  |  %s", m_State.Mode() == EditorMode::Edit ? "Edit" : "Runtime");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::SelectTool, "Q");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::TranslateTool, "W");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::RotateTool, "E");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::ScaleTool, "R");
                ImGui::SameLine();
                if (ActionButton("+", UIButtonTone::Primary, true, 25.0f)) OpenAddPrimitivePopup();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add primitive (Shift+A)");
                DrawPrimitivePopup();

                const ImVec2 viewportMin = ImGui::GetCursorScreenPos();
                const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
                const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
                m_RequestedViewportWidth = static_cast<std::uint32_t>(std::clamp(
                    std::lround(std::max(viewportSize.x, 1.0f) * framebufferScale.x), 1l, 16384l));
                m_RequestedViewportHeight = static_cast<std::uint32_t>(std::clamp(
                    std::lround(std::max(viewportSize.y, 1.0f) * framebufferScale.y), 1l, 16384l));
                if (m_ViewportTexture != ImTextureID_Invalid)
                    ImGui::Image(ImTextureRef(m_ViewportTexture), viewportSize);
                else
                {
                    ImGui::InvisibleButton("##ViewportUnavailable", viewportSize);
                    ImGui::GetWindowDrawList()->AddText(
                        { viewportMin.x + 12.0f, viewportMin.y + 12.0f },
                        IM_COL32(230, 125, 125, 255), "Viewport texture unavailable");
                }
                const bool hovered = ImGui::IsItemHovered();
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    ImGui::SetWindowFocus();
                    if (m_ActiveTool == EditorAction::SelectTool && m_ViewportTexture != ImTextureID_Invalid)
                    {
                        const ImVec2 mouse = ImGui::GetMousePos();
                        const auto x = static_cast<std::uint32_t>(std::clamp(
                            std::floor((mouse.x - viewportMin.x) * framebufferScale.x), 0.0f,
                            static_cast<float>(m_RequestedViewportWidth - 1u)));
                        const auto y = static_cast<std::uint32_t>(std::clamp(
                            std::floor((mouse.y - viewportMin.y) * framebufferScale.y), 0.0f,
                            static_cast<float>(m_RequestedViewportHeight - 1u)));
                        m_ViewportPickRequest = std::pair{ x, y };
                    }
                }
                HandleViewportNavigation(hovered);

                const ImVec2 overlay = { viewportMin.x + 12.0f, viewportMin.y + 12.0f };
                ImGui::GetWindowDrawList()->AddText(overlay, IM_COL32(210, 225, 238, 210),
                    m_ActiveTool == EditorAction::SelectTool ? "SELECT" :
                    m_ActiveTool == EditorAction::TranslateTool ? "MOVE" :
                    m_ActiveTool == EditorAction::RotateTool ? "ROTATE" : "SCALE");
                ImGui::GetWindowDrawList()->AddText({ overlay.x, overlay.y + 18.0f }, IM_COL32(135, 165, 184, 190),
                    "MMB orbit  Shift+MMB pan  RMB+WASD fly");
                const auto selected = m_State.SelectedEntity();
                if (selected.has_value())
                {
                    const auto& position = m_Project.Scene().Transform(*selected).Local.Translation;
                    ImGui::GetWindowDrawList()->AddText({ overlay.x, overlay.y + 36.0f }, IM_COL32(140, 208, 170, 220),
                        ("Focus: " + m_Project.Scene().Name(*selected).Value).c_str());
                    (void)position;
                }
                (void)camera;
            }
            ImGui::End();
        }

        void DrawViewportToolButton(EditorAction tool, const char* label)
        {
            if (ToolbarButton(label, m_ActiveTool == tool)) m_ActiveTool = tool;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)",
                BindingFor(tool).DisplayName.data(), BindingFor(tool).Shortcut.data());
        }

        void OpenAddPrimitivePopup()
        {
            ImGui::OpenPopup("Add Scene Primitive");
        }

        void DrawPrimitivePopup()
        {
            if (!ImGui::BeginPopup("Add Scene Primitive")) return;
            for (const PrimitiveKind kind : { PrimitiveKind::Cube, PrimitiveKind::Plane,
                PrimitiveKind::UVSphere, PrimitiveKind::Cylinder })
            {
                if (ImGui::MenuItem(Name(kind).data())) CreatePrimitive(kind);
            }
            ImGui::EndPopup();
        }

        void CreatePrimitive(PrimitiveKind kind)
        {
            auto command = std::make_unique<CreatePrimitiveCommand>(m_Project, kind);
            auto* created = command.get();
            RunCommand([this, &command] { m_History.Execute(std::move(command)); });
            if (command == nullptr)
            {
                m_State.Select(created->CreatedEntity());
                m_ViewportController.Focus(m_Project.Scene().Transform(created->CreatedEntity()).Local.Translation);
            }
        }

        void HandleViewportShortcuts()
        {
            if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_A)) OpenAddPrimitivePopup();
            if (ImGui::Shortcut(ImGuiKey_Q)) m_ActiveTool = EditorAction::SelectTool;
            if (ImGui::Shortcut(ImGuiKey_W)) m_ActiveTool = EditorAction::TranslateTool;
            if (ImGui::Shortcut(ImGuiKey_E)) m_ActiveTool = EditorAction::RotateTool;
            if (ImGui::Shortcut(ImGuiKey_R)) m_ActiveTool = EditorAction::ScaleTool;
            if (ImGui::Shortcut(ImGuiKey_G)) m_ActiveTool = EditorAction::TranslateTool;
            if (ImGui::Shortcut(ImGuiKey_S)) m_ActiveTool = EditorAction::ScaleTool;
            if (ImGui::Shortcut(ImGuiKey_F)) FocusSelection();
            if (ImGui::Shortcut(ImGuiKey_F5))
            {
                if (m_State.Mode() == EditorMode::Edit) StartPlay();
                else StopPlay();
            }
            const auto selected = m_State.SelectedEntity();
            if (selected.has_value() && (ImGui::Shortcut(ImGuiKey_Delete) || ImGui::Shortcut(ImGuiKey_X)))
            {
                RunCommand([this, entity = *selected]
                {
                    m_History.Execute(std::make_unique<DeleteEntityCommand>(m_Project, entity));
                    m_State.ClearSelection();
                });
            }
            if (selected.has_value() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_D))
            {
                const auto& source = m_Project.Scene();
                auto command = std::make_unique<CreateEntityCommand>(m_Project,
                    source.Name(*selected).Value + " Copy");
                auto* duplicate = command.get();
                RunCommand([this, &command, selected, duplicate]
                {
                    m_History.Execute(std::move(command));
                    const auto created = duplicate->CreatedEntity();
                    auto& scene = m_Project.EditScene();
                    scene.Transform(created).Local = scene.Transform(*selected).Local;
                    if (scene.HasMeshRenderer(*selected)) scene.SetMeshRenderer(created, scene.MeshRenderer(*selected));
                    if (scene.HasCamera(*selected)) scene.SetCamera(created, scene.Camera(*selected));
                    if (scene.HasRigidBody(*selected)) scene.SetRigidBody(created, scene.RigidBody(*selected));
                    if (scene.HasCollider(*selected)) scene.SetCollider(created, scene.Collider(*selected));
                    m_State.Select(created);
                });
            }
        }

        void FocusSelection()
        {
            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value()) return;
            m_ViewportController.Focus(m_Project.Scene().Transform(*selected).Local.Translation);
        }

        void StartPlay()
        {
            m_RuntimeScene = m_Project.Scene();
            m_PhysicsPreview.Start(*m_RuntimeScene);
            m_State.Play();
        }

        void StopPlay() noexcept
        {
            m_PhysicsPreview.Reset();
            m_RuntimeScene.reset();
            m_State.Stop();
        }

        void HandleViewportNavigation(bool hovered)
        {
            if (!hovered) return;
            const ImGuiIO& io = ImGui::GetIO();
            const bool rightMouse = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            ViewportInput input;
            input.MouseDeltaX = io.MouseDelta.x;
            input.MouseDeltaY = io.MouseDelta.y;
            input.WheelDelta = io.MouseWheel;
            input.DeltaSeconds = io.DeltaTime;
            input.Orbit = ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !io.KeyShift;
            input.Pan = ImGui::IsMouseDown(ImGuiMouseButton_Middle) && io.KeyShift;
            input.Fly = rightMouse;
            if (rightMouse)
            {
                input.MoveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
                input.MoveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
                input.MoveUp = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
            }
            m_ViewportController.Update(input);

            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value() || !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) return;
            if (m_ActiveTool == EditorAction::SelectTool) return;
            auto transform = m_Project.Scene().Transform(*selected).Local;
            if (m_ActiveTool == EditorAction::TranslateTool)
            {
                const float sensitivity = std::max(0.0025f, m_ViewportController.Distance() * 0.0035f);
                transform.Translation.x += io.MouseDelta.x * sensitivity;
                transform.Translation.y -= io.MouseDelta.y * sensitivity;
            }
            else if (m_ActiveTool == EditorAction::RotateTool)
            {
                transform.Rotation = (kairo::foundation::math::RotationAroundY(io.MouseDelta.x * 0.0125f) *
                    kairo::foundation::math::RotationAroundX(io.MouseDelta.y * 0.0125f) * transform.Rotation).Normalized();
            }
            else if (m_ActiveTool == EditorAction::ScaleTool)
            {
                const float factor = std::clamp(std::exp(io.MouseDelta.x * 0.0125f), 0.1f, 10.0f);
                transform.Scale *= factor;
            }
            RunCommand([this, entity = *selected, transform]
            {
                m_History.Execute(std::make_unique<SetEntityTransformCommand>(m_Project, entity, transform));
            });
        }

        void DrawToolPanel(Panel panel)
        {
            const char* title = Name(panel).data();
            if (!ImGui::Begin(title)) { ImGui::End(); return; }
            if (panel == Panel::Statistics)
            {
                const ImGuiIO& io = ImGui::GetIO();
                ImGui::Text("Frame %.2f ms", 1000.0f / std::max(io.Framerate, 1.0f));
                ImGui::Text("UI %.0f FPS", io.Framerate);
            }
            else if (panel == Panel::CodeEditor || panel == Panel::NodeGraph)
            {
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                    m_DocumentPanelFocused = true;
                const auto active = DrawDocumentTabs(panel);
                if (active.has_value()) DrawDocumentSummary(panel, *active);
                else
                {
                    ImGui::TextDisabled("Open a document asset or create a typed document.");
                    if (ActionButton("New Document", UIButtonTone::Primary)) RequestNewDocument();
                }
            }
            else if (panel == Panel::Console) ImGui::TextDisabled("No engine messages.");
            else if (panel == Panel::PhysicsDebug) DrawPhysicsDebug();
            else if (panel == Panel::ContentBrowser) DrawContentBrowser();
            else ImGui::TextDisabled("No active document for this workspace.");
            ImGui::End();
        }

        void DrawPhysicsDebug()
        {
            if (!m_PhysicsPreview.Active())
            {
                ImGui::TextDisabled("Enter Play to build the physics preview.");
                return;
            }
            const auto& world = m_PhysicsPreview.World();
            ImGui::Text("Bodies %zu  Colliders %zu  Contacts %zu", world.Bodies().size(),
                world.Colliders().size(), world.Contacts().size());
            ImGui::Checkbox("Broadphase AABBs", &m_ShowPhysicsBroadphase);
            ImGui::TextDisabled("Collider outlines and contact normals are drawn in the viewport.");
        }

        void DrawContentBrowser()
        {
            (void)SearchField("##AssetFilter", "Filter assets", m_AssetFilter.data(), m_AssetFilter.size());
            const auto records = m_Project.Assets().Snapshot();
            const std::string filter = Lower(m_AssetFilter.data());
            std::size_t visible = 0u;
            if (ImGui::BeginTable("AssetRegistry", 3,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Path");
                ImGui::TableSetupColumn("Rev", ImGuiTableColumnFlags_WidthFixed, 42.0f);
                ImGui::TableHeadersRow();
                for (const auto& asset : records)
                {
                    const std::string path = asset.Path.generic_string();
                    const std::string type(kairo::assets::NameOfAssetType(asset.Type));
                    if (!filter.empty() && Lower(path).find(filter) == std::string::npos &&
                        Lower(type).find(filter) == std::string::npos) continue;
                    ++visible;
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(type.c_str());
                    ImGui::TableSetColumnIndex(1);
                    const std::string row = path + "##" + asset.ID.ToString();
                    const bool isDocument = asset.Type == kairo::assets::AssetType::Document;
                    if (isDocument)
                    {
                        ImGui::Selectable(row.c_str(), false,
                            ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            OpenDocument(asset.Path);
                    }
                    else ImGui::TextUnformatted(path.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s", path.c_str(),
                        asset.ID.ToString().c_str(), kairo::assets::NameOfAssetOrigin(asset.Origin).data());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(asset.Revision));
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%zu of %zu assets", visible, records.size());
        }

        [[nodiscard]] std::optional<kairo::assets::AssetID> DrawDocumentTabs(Panel panel)
        {
            const auto documents = m_Project.Documents().Snapshot();
            if (documents.empty()) return std::nullopt;
            const std::string tabBarID = panel == Panel::CodeEditor ? "##CodeDocuments" : "##GraphDocuments";
            if (ImGui::BeginTabBar(tabBarID.c_str(), ImGuiTabBarFlags_Reorderable |
                ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_FittingPolicyScroll))
            {
                for (const auto& info : documents)
                {
                    bool open = true;
                    const bool draftDirty = m_AuthoringWorkspace.Contains(info.ID) &&
                        m_AuthoringWorkspace.At(info.ID).IsTextDirty();
                    std::string title = info.Name + ((info.Dirty || draftDirty) ? " *" : "") + "###" +
                        info.ID.ToString() + (panel == Panel::CodeEditor ? "-code" : "-graph");
                    const ImGuiTabItemFlags flags = info.Active ? ImGuiTabItemFlags_SetSelected : 0;
                    if (ImGui::BeginTabItem(title.c_str(), &open, flags))
                    {
                        if (!info.Active) m_Project.ActivateDocument(info.ID);
                        (void)m_AuthoringWorkspace.Open(m_Project.Document(info.ID));
                        ImGui::EndTabItem();
                    }
                    if (!open) RequestCloseDocument(info.ID);
                }
                ImGui::EndTabBar();
            }
            return m_Project.Documents().ActiveID();
        }

        void DrawDocumentSummary(Panel panel, kairo::assets::AssetID id)
        {
            const auto& document = m_Project.Document(id);
            (void)m_AuthoringWorkspace.Open(document);
            ImGui::TextDisabled("%s  |  %s  |  %zu nodes  |  %zu connections",
                document.Name().c_str(), Name(document.Kind()).data(), document.NodeCount(),
                document.ConnectionCount());
            ImGui::SameLine();
            if (ActionButton("Save", UIButtonTone::Primary)) RunCommand([this, id] { SaveDocumentWithDraft(id); });
            if (panel == Panel::CodeEditor)
                DrawCodeEditor(id);
            else
            {
                m_GraphCanvas.Draw(m_Project, m_AuthoringWorkspace.At(id), id);
                if (auto error = m_GraphCanvas.TakeError(); error.has_value())
                {
                    m_LastError = std::move(*error);
                    m_RequestErrorPopup = true;
                }
            }
        }

        void RequestNewDocument()
        {
            std::snprintf(m_NewDocumentName.data(), m_NewDocumentName.size(), "%s", "Untitled Logic");
            std::snprintf(m_NewDocumentPath.data(), m_NewDocumentPath.size(), "%s", "Logic/Untitled.kdoc");
            m_NewDocumentKind = 0;
            m_RequestNewDocumentPopup = true;
        }

        void RequestCloseDocument(kairo::assets::AssetID id)
        {
            if (!m_Project.Documents().Contains(id)) return;
            const bool draftDirty = m_AuthoringWorkspace.Contains(id) &&
                m_AuthoringWorkspace.At(id).IsTextDirty();
            if (!m_Project.Documents().IsDirty(id) && !draftDirty)
            {
                RunCommand([this, id]
                {
                    m_Project.CloseDocument(id);
                    m_AuthoringWorkspace.Close(id);
                });
                return;
            }
            m_PendingDocumentClose = id;
            m_RequestCloseDocumentPopup = true;
        }

        void OpenDocument(const std::filesystem::path& path)
        {
            RunCommand([this, path]
            {
                const auto id = m_Project.OpenDocument(path);
                const auto& document = m_Project.Document(id);
                (void)m_AuthoringWorkspace.Open(document);
                m_State.SwitchWorkspace(WorkspaceFor(document.Kind()));
                m_State.SetAuthoringSurface(AuthoringSurface::CodeAndGraph);
            });
        }

        void DrawDocumentLifecyclePopups()
        {
            if (m_RequestNewDocumentPopup)
            {
                ImGui::OpenPopup("Create Authoring Document");
                m_RequestNewDocumentPopup = false;
            }
            if (ImGui::BeginPopupModal("Create Authoring Document", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                constexpr const char* kinds[] = { "Logic", "Material", "Audio", "Animation State", "Simulation" };
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputText("Name", m_NewDocumentName.data(), m_NewDocumentName.size());
                ImGui::SetNextItemWidth(420.0f);
                ImGui::InputText("Project path", m_NewDocumentPath.data(), m_NewDocumentPath.size());
                ImGui::SetNextItemWidth(220.0f);
                ImGui::Combo("Kind", &m_NewDocumentKind, kinds, static_cast<int>(std::size(kinds)));
                if (ActionButton("Create", UIButtonTone::Primary, true, 110.0f))
                {
                    RunCommand([this]
                    {
                        const auto kind = static_cast<DocumentKind>(m_NewDocumentKind);
                        const auto id = m_Project.CreateDocument(kind, m_NewDocumentName.data(),
                            m_NewDocumentPath.data());
                        (void)m_AuthoringWorkspace.Open(m_Project.Document(id));
                        m_State.SwitchWorkspace(WorkspaceFor(kind));
                        m_State.SetAuthoringSurface(AuthoringSurface::CodeAndGraph);
                        ImGui::CloseCurrentPopup();
                    });
                }
                ImGui::SameLine();
                if (ActionButton("Cancel", UIButtonTone::Neutral, true, 110.0f)) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (m_RequestCloseDocumentPopup)
            {
                ImGui::OpenPopup("Unsaved Document");
                m_RequestCloseDocumentPopup = false;
            }
            if (ImGui::BeginPopupModal("Unsaved Document", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("This document has unapplied text or unsaved authored changes.");
                if (ActionButton("Save and Close", UIButtonTone::Primary, m_PendingDocumentClose.has_value(), 130.0f))
                {
                    const auto id = *m_PendingDocumentClose;
                    RunCommand([this, id]
                    {
                        SaveDocumentWithDraft(id);
                        m_Project.CloseDocument(id);
                        m_AuthoringWorkspace.Close(id);
                        m_PendingDocumentClose.reset();
                        ImGui::CloseCurrentPopup();
                    });
                }
                ImGui::SameLine();
                if (ActionButton("Discard", UIButtonTone::Destructive, m_PendingDocumentClose.has_value(), 100.0f))
                {
                    const auto id = *m_PendingDocumentClose;
                    RunCommand([this, id]
                    {
                        m_Project.CloseDocument(id, UnsavedChangesPolicy::Discard);
                        m_AuthoringWorkspace.Close(id);
                        m_PendingDocumentClose.reset();
                        ImGui::CloseCurrentPopup();
                    });
                }
                ImGui::SameLine();
                if (ActionButton("Cancel", UIButtonTone::Neutral, true, 100.0f))
                {
                    m_PendingDocumentClose.reset();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        [[nodiscard]] static Workspace WorkspaceFor(DocumentKind kind) noexcept
        {
            switch (kind)
            {
                case DocumentKind::Logic: return Workspace::Logic;
                case DocumentKind::Material: return Workspace::Materials;
                case DocumentKind::Audio: return Workspace::Audio;
                case DocumentKind::AnimationState: return Workspace::Animation;
                case DocumentKind::Simulation: return Workspace::Simulation;
            }
            return Workspace::Logic;
        }

        [[nodiscard]] CommandHistory& ActiveHistory()
        {
            return m_DocumentPanelFocused && m_Project.HasProject() &&
                m_Project.Documents().ActiveID().has_value()
                ? m_Project.DocumentHistory() : m_History;
        }

        void DrawCodeEditor(kairo::assets::AssetID id)
        {
            auto& view = m_AuthoringWorkspace.At(id);
            view.Synchronize(m_Project.Document(id));
            if (view.HasExternalConflict())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.27f, 1.0f));
                ImGui::TextWrapped("The graph changed after this text draft began. Revert the draft before applying it.");
                ImGui::PopStyleColor();
            }

            const bool canApply = view.IsTextDirty() && !view.HasExternalConflict();
            if (ActionButton("Apply", UIButtonTone::Primary, canApply)) RunCommand([this, id] { ApplyTextDraft(id); });
            ImGui::SameLine();
            const bool canRevert = view.IsTextDirty() || view.HasExternalConflict();
            if (ActionButton("Revert", UIButtonTone::Neutral, canRevert)) view.ResetText(m_Project.Document(id));
            ImGui::SameLine();
            const std::size_t lines = static_cast<std::size_t>(std::ranges::count(view.TextDraft(), '\n')) + 1u;
            ImGui::TextDisabled("%zu lines  |  %zu bytes%s", lines, view.TextDraft().size(),
                view.IsTextDirty() ? "  |  draft" : "");

            ImGui::Separator();
            ImFont* font = ImGui::GetFont();
            const ImGuiIO& io = ImGui::GetIO();
            if (io.Fonts != nullptr && io.Fonts->Fonts.Size > 1) font = io.Fonts->Fonts[1];
            ImGui::PushFont(font);
            ImGui::InputTextMultiline("##DocumentText", view.TextDraftData(),
                view.TextDraftCapacity() + 1u, ImGui::GetContentRegionAvail(),
                ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize,
                ResizeDocumentText, &view);
            ImGui::PopFont();
        }

        void ApplyTextDraft(kairo::assets::AssetID id)
        {
            auto& view = m_AuthoringWorkspace.At(id);
            const auto& current = m_Project.Document(id);
            view.Synchronize(current);
            if (view.HasExternalConflict())
                throw std::logic_error("Cannot apply a stale text draft after the graph changed.");
            if (!view.IsTextDirty()) return;

            // Parse before requesting mutable project access so malformed text
            // cannot mark an unchanged authoritative document dirty.
            (void)ParseDocumentProjection(view.TextDraft(), current.ID(), current.Kind());
            auto& document = m_Project.EditDocument(id);
            m_Project.DocumentHistory().Execute(
                std::make_unique<ApplyDocumentTextCommand>(document, view.TextDraft()));
            view.TextApplySucceeded(document);
        }

        void SaveDocumentWithDraft(kairo::assets::AssetID id)
        {
            if (m_AuthoringWorkspace.Contains(id) && m_AuthoringWorkspace.At(id).IsTextDirty())
                ApplyTextDraft(id);
            m_Project.SaveDocument(id);
        }

        void SaveAllWithDrafts()
        {
            // Validate every draft first so one malformed tab cannot leave a
            // partially applied set before Save All reports its failure.
            for (const auto id : m_AuthoringWorkspace.DocumentIDs())
            {
                auto& view = m_AuthoringWorkspace.At(id);
                const auto& document = m_Project.Document(id);
                view.Synchronize(document);
                if (view.HasExternalConflict())
                    throw std::logic_error("Save All cannot apply a text draft that conflicts with newer graph changes.");
                if (view.IsTextDirty())
                    (void)ParseDocumentProjection(view.TextDraft(), document.ID(), document.Kind());
            }
            for (const auto id : m_AuthoringWorkspace.DocumentIDs())
                if (m_AuthoringWorkspace.At(id).IsTextDirty()) ApplyTextDraft(id);
            m_Project.SaveAll();
        }

        static int ResizeDocumentText(ImGuiInputTextCallbackData* data) noexcept
        {
            auto& view = *static_cast<DocumentViewState*>(data->UserData);
            if (data->EventFlag != ImGuiInputTextFlags_CallbackResize) return 0;
            if (data->BufTextLen < 0 || static_cast<std::size_t>(data->BufTextLen) > MaximumDocumentDraftBytes)
            {
                data->Buf = view.TextDraftData();
                data->BufTextLen = static_cast<int>(view.TextDraft().size());
                data->BufSize = static_cast<int>(view.TextDraftCapacity() + 1u);
                return 0;
            }
            try
            {
                view.ResizeTextDraft(static_cast<std::size_t>(data->BufTextLen));
                data->Buf = view.TextDraftData();
            }
            catch (...) { return 1; }
            return 0;
        }

        [[nodiscard]] static std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
            return result;
        }

        template<class Command>
        void RunCommand(Command&& command) noexcept
        {
            try { std::forward<Command>(command)(); }
            catch (const std::exception& error)
            {
                m_LastError = error.what();
                m_RequestErrorPopup = true;
            }
        }

        void DrawErrorPopup()
        {
            if (m_RequestErrorPopup)
            {
                ImGui::OpenPopup("Operation failed");
                m_RequestErrorPopup = false;
            }
            if (ImGui::BeginPopupModal("Operation failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("%s", m_LastError.c_str());
                if (ActionButton("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    };
}
