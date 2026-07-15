module;

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

export module Kairo.Editor.ImGuiShell;

import Kairo.Editor;
import Kairo.EngineCore;

export namespace kairo::editor
{
    /// Draws the production editor shell from backend-neutral EditorState and
    /// Scene data. Docking/layout are ImGui concerns; selection and workspace
    /// transitions remain in KairoEditor core and are independently testable.
    class EditorShell final
    {
    public:
        EditorShell(EditorState& state, kairo::engine::Scene& scene) : m_State(state), m_Scene(scene) {}

        void Draw()
        {
            DrawMainBar();
            const ImGuiID dockspace = ImGui::GetID("KairoEditorDockspace");
            BuildDefaultLayout(dockspace);
            ImGui::DockSpaceOverViewport(dockspace, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
            DrawVisiblePanels();
        }

    private:
        EditorState& m_State;
        kairo::engine::Scene& m_Scene;
        bool m_LayoutBuilt = false;

        void DrawMainBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;
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
                if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
                if (ImGui::Button(Name(workspace).data())) m_State.SwitchWorkspace(workspace);
                if (isActive) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::Separator();
            DrawPlayControls();
            ImGui::EndMainMenuBar();
        }

        void DrawPlayControls()
        {
            if (m_State.Mode() == EditorMode::Edit)
            {
                if (ImGui::Button("Play")) m_State.Play();
            }
            else
            {
                if (ImGui::Button("Stop")) m_State.Stop();
                ImGui::SameLine();
                if (m_State.Mode() == EditorMode::Play && ImGui::Button("Pause")) m_State.Pause();
                else if (m_State.Mode() == EditorMode::Pause && ImGui::Button("Resume")) m_State.Resume();
            }
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
            if (ImGui::Button("+ Entity")) m_State.Select(m_Scene.CreateEntity("Entity"));
            ImGui::Separator();
            for (const auto entity : m_Scene.Entities())
            {
                const bool selected = m_State.SelectedEntity().has_value() && *m_State.SelectedEntity() == entity;
                const std::string label = m_Scene.Name(entity).Value + "##" + std::to_string(entity.Value);
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
            auto& name = m_Scene.Name(*selected).Value;
            std::array<char, 256> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", name.c_str());
            if (ImGui::InputText("Name", buffer.data(), buffer.size())) name = buffer.data();
            auto& transform = m_Scene.Transform(*selected).Local;
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat3("Position", &transform.Translation.x, 0.05f);
            ImGui::DragFloat3("Scale", &transform.Scale.x, 0.02f, 0.001f, 1000.0f);
            ImGui::TextDisabled("Rotation is stored as a normalized quaternion.");
            ImGui::End();
        }

        void DrawViewport()
        {
            ImGui::SetNextWindowBgAlpha(0.0f);
            if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoBackground))
            {
                ImGui::TextDisabled("Perspective  |  Lit  |  %s", m_State.Mode() == EditorMode::Edit ? "Edit" : "Runtime");
            }
            ImGui::End();
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
                ImGui::TextDisabled("No authored document is open.");
                ImGui::Separator();
                if (ImGui::RadioButton("Code", m_State.ActiveAuthoringSurface() == AuthoringSurface::Code)) m_State.SetAuthoringSurface(AuthoringSurface::Code);
                ImGui::SameLine();
                if (ImGui::RadioButton("Graph", m_State.ActiveAuthoringSurface() == AuthoringSurface::Graph)) m_State.SetAuthoringSurface(AuthoringSurface::Graph);
                ImGui::SameLine();
                if (ImGui::RadioButton("Split", m_State.ActiveAuthoringSurface() == AuthoringSurface::CodeAndGraph)) m_State.SetAuthoringSurface(AuthoringSurface::CodeAndGraph);
            }
            else if (panel == Panel::Console) ImGui::TextDisabled("No engine messages.");
            else if (panel == Panel::ContentBrowser) ImGui::TextDisabled("No project assets indexed.");
            else ImGui::TextDisabled("No active document for this workspace.");
            ImGui::End();
        }
    };
}
