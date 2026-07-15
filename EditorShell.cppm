module;

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

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
        EditorShell(EditorState& state, ProjectSession& project) : m_State(state), m_Project(project) {}

        void Draw()
        {
            m_State.ValidateSelection();
            DrawMainBar();
            const ImGuiID dockspace = ImGui::GetID("KairoEditorDockspace");
            BuildDefaultLayout(dockspace);
            ImGui::DockSpaceOverViewport(dockspace, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
            DrawVisiblePanels();
            DrawErrorPopup();
        }

    private:
        EditorState& m_State;
        ProjectSession& m_Project;
        bool m_LayoutBuilt = false;
        std::array<char, 256> m_AssetFilter{};
        std::string m_LastError;
        bool m_RequestErrorPopup = false;

        void DrawMainBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Scene", "Cmd+S", false, m_Project.HasProject()))
                    RunCommand([this] { m_Project.SaveScene(); });
                if (ImGui::MenuItem("Save All", "Cmd+Option+S", false, m_Project.HasProject()))
                    RunCommand([this] { m_Project.SaveAll(); });
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
                if (isActive) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_Header));
                if (ImGui::Button(Name(workspace).data())) m_State.SwitchWorkspace(workspace);
                if (isActive) ImGui::PopStyleColor();
                ImGui::SameLine();
            }
            ImGui::Separator();
            DrawPlayControls();
            if (m_Project.HasProject())
            {
                ImGui::Separator();
                ImGui::TextDisabled("%s%s", m_Project.Descriptor().Name.c_str(),
                    m_Project.HasUnsavedChanges() ? " *" : "");
            }
            ImGui::EndMainMenuBar();

            if (m_Project.HasProject() && ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_S))
                RunCommand([this] { m_Project.SaveScene(); });
            if (m_Project.HasProject() &&
                ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiMod_Alt | ImGuiKey_S))
                RunCommand([this] { m_Project.SaveAll(); });
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
            if (ImGui::Button("+ Entity"))
                m_State.Select(m_Project.EditScene().CreateEntity("Entity"));
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
            const auto& scene = m_Project.Scene();
            const auto& name = scene.Name(*selected).Value;
            std::array<char, 256> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", name.c_str());
            if (ImGui::InputText("Name", buffer.data(), buffer.size()))
                m_Project.EditScene().Name(*selected).Value = buffer.data();
            auto transform = scene.Transform(*selected).Local;
            ImGui::SeparatorText("Transform");
            bool changed = ImGui::DragFloat3("Position", &transform.Translation.x, 0.05f);
            changed |= ImGui::DragFloat3("Scale", &transform.Scale.x, 0.02f, 0.001f, 1000.0f,
                "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (changed) m_Project.EditScene().Transform(*selected).Local = transform;
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
            else if (panel == Panel::ContentBrowser) DrawContentBrowser();
            else ImGui::TextDisabled("No active document for this workspace.");
            ImGui::End();
        }

        void DrawContentBrowser()
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##AssetFilter", "Filter assets", m_AssetFilter.data(), m_AssetFilter.size());
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
                    ImGui::TextUnformatted(path.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s", path.c_str(),
                        asset.ID.ToString().c_str(), kairo::assets::NameOfAssetOrigin(asset.Origin).data());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", static_cast<unsigned long long>(asset.Revision));
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%zu of %zu assets", visible, records.size());
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
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    };
}
