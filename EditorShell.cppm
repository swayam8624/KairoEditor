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
import Kairo.Editor.UI;
import Kairo.EngineCore;

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
            DrawDocumentLifecyclePopups();
            DrawErrorPopup();
        }

    private:
        EditorState& m_State;
        ProjectSession& m_Project;
        CommandHistory m_History;
        AuthoringWorkspaceState m_AuthoringWorkspace;
        DocumentSchemaRegistry m_Schemas = CreateCoreDocumentSchemaRegistry();
        ImGuiGraphCanvas m_GraphCanvas;
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
                if (ActionButton("Play", UIButtonTone::Primary)) m_State.Play();
            }
            else
            {
                if (ActionButton("Stop", UIButtonTone::Destructive)) m_State.Stop();
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
            const auto& scene = m_Project.Scene();
            const auto& name = scene.Name(*selected).Value;
            std::array<char, 256> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%s", name.c_str());
            if (ImGui::InputText("Name", buffer.data(), buffer.size()))
            {
                const std::string editedName(buffer.data());
                if (editedName != name)
                    RunCommand([this, entity = *selected, editedName]
                    {
                        m_History.Execute(std::make_unique<SetEntityNameCommand>(m_Project, entity, editedName));
                    });
            }
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
                if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
                    m_DocumentPanelFocused = true;
                const auto active = DrawDocumentTabs(panel);
                if (active.has_value()) DrawDocumentSummary(panel, *active);
                else
                {
                    ImGui::TextDisabled("Open a document asset or create a typed document.");
                    if (ImGui::Button("New Document")) RequestNewDocument();
                }
            }
            else if (panel == Panel::Console) ImGui::TextDisabled("No engine messages.");
            else if (panel == Panel::ContentBrowser) DrawContentBrowser();
            else ImGui::TextDisabled("No active document for this workspace.");
            ImGui::End();
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
            if (ImGui::SmallButton("Save")) RunCommand([this, id] { SaveDocumentWithDraft(id); });
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
                if (ImGui::Button("Create", { 110.0f, 0.0f }))
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
                if (ImGui::Button("Cancel", { 110.0f, 0.0f })) ImGui::CloseCurrentPopup();
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
                if (ImGui::Button("Save and Close", { 130.0f, 0.0f }) && m_PendingDocumentClose.has_value())
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
                if (ImGui::Button("Discard", { 100.0f, 0.0f }) && m_PendingDocumentClose.has_value())
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
                if (ImGui::Button("Cancel", { 100.0f, 0.0f }))
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
            if (!canApply) ImGui::BeginDisabled();
            if (ImGui::Button("Apply")) RunCommand([this, id] { ApplyTextDraft(id); });
            if (!canApply) ImGui::EndDisabled();
            ImGui::SameLine();
            const bool canRevert = view.IsTextDirty() || view.HasExternalConflict();
            if (!canRevert) ImGui::BeginDisabled();
            if (ImGui::Button("Revert")) view.ResetText(m_Project.Document(id));
            if (!canRevert) ImGui::EndDisabled();
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
                if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
    };
}
