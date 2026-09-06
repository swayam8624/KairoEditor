module;

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.Editor.ImGuiShell;

import Kairo.Editor;
import Kairo.AI;
import Kairo.Editor.ImGuiGraphCanvas;
import Kairo.Editor.TransformGizmo;
import Kairo.Editor.ImGuiReflectionInspector;
import Kairo.Editor.UI;
import Kairo.Editor.PhysicsPreview;
import Kairo.EngineCore;
import Kairo.EngineCore.Reflection;
import Kairo.Reflection;
import Kairo.Foundation.Math;
import Kairo.Renderer.DebugDraw;
import Kairo.Renderer.Types;

export namespace kairo::editor
{
    /// Draws the production editor shell from backend-neutral EditorState and
    /// Scene data. Docking/layout are ImGui concerns; selection and workspace
    /// transitions remain in KairoEditor core and are independently testable.
    class EditorShell final
    {
    public:
        EditorShell(EditorState& state, ProjectSession& project, bool rebuildLayout = true,
            EditorKeymapSettings keymapSettings = {},
            std::filesystem::path keymapSettingsPath = {},
            NavigationSettings navigationSettings = {},
            std::filesystem::path navigationSettingsPath = {},
            std::shared_ptr<kairo::ai::Provider> aiProvider = {}, std::string aiModel = {},
            std::shared_ptr<OfflineRenderService> offlineRenderService = {},
            const kairo::engine::NativeGameplayRegistry* nativeGameplayRegistry = nullptr,
            const kairo::assets::ImportDatabase* assetImports = nullptr)
            : m_State(state), m_Project(project), m_GraphCanvas(m_Schemas),
              m_InputRouter(keymapSettings.Profile, keymapSettings.Overrides),
              m_RebuildLayout(rebuildLayout), m_KeymapSettings(std::move(keymapSettings)),
              m_KeymapSettingsPath(std::move(keymapSettingsPath)),
              m_NavigationSettings(navigationSettings),
              m_NavigationSettingsPath(std::move(navigationSettingsPath))
        {
            ValidateNavigationSettings(m_NavigationSettings);
            m_AssetImports = assetImports;
            if (static_cast<bool>(aiProvider) != !aiModel.empty())
                throw std::invalid_argument("AI editor provider and model must be configured together.");
            if (aiProvider)
                m_AISession = std::make_unique<AIEditorSession>(
                    m_Project, m_History, std::move(aiProvider), std::move(aiModel));
            if (offlineRenderService)
                m_OfflineRender = std::make_unique<OfflineRenderAuthoringController>(
                    std::move(offlineRenderService));
            if (nativeGameplayRegistry != nullptr)
            {
                m_NativeGameplay = std::make_unique<NativeGameplayAuthoringWorkspace>(
                    m_Project.Scene(), *nativeGameplayRegistry);
                m_NativeGameplayPath = m_Project.ProjectRoot() /
                    kairo::engine::DefaultNativeGameplayManifestPath;
                if (std::filesystem::is_regular_file(m_NativeGameplayPath))
                    m_NativeGameplay->Load(m_NativeGameplayPath);
            }
            kairo::engine::RegisterEngineCoreReflection(m_Reflection);
            m_NextAutosave = std::chrono::steady_clock::now() + AutosaveInterval;
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
            m_ViewportFlyCursorCaptured = false;
            m_State.ValidateSelection();
            m_GraphCanvas.BeginFrame();
            RunAutosaveIfDue();
            if (m_AISession) (void)m_AISession->Poll();
            if (m_OfflineRender)
            {
                const auto status = m_OfflineRender->State().Status();
                if (status == OfflineRenderWorkspaceStatus::Queued ||
                    status == OfflineRenderWorkspaceStatus::Running)
                    RunCommand([this] { m_OfflineRender->Refresh(); });
            }
            DrawMainBar();
            DrawStatusBar();
            const ImGuiID dockspace = ImGui::GetID("KairoEditorDockspace");
            BuildDefaultLayout(dockspace);
            ImGui::DockSpaceOverViewport(dockspace, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
            DrawVisiblePanels();
            DrawCommandPalette();
            RouteAndDispatchInput();
            if (m_State.Mode() == EditorMode::Play && m_RuntimeScene.has_value())
                RunCommand([this] { m_PhysicsPreview.Step(*m_RuntimeScene, ImGui::GetIO().DeltaTime); });
            DrawDocumentLifecyclePopups();
            DrawProjectLifecyclePopups();
            DrawNavigationPreferences();
            DrawNavigationHelp();
            DrawKeymapEditor();
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

        void SetViewportShading(kairo::renderer::ViewportShadingMode mode) noexcept { m_ViewportShading = mode; }
        [[nodiscard]] kairo::renderer::ViewportShadingMode ViewportShading() const noexcept
        {
            return m_ViewportShading;
        }

        [[nodiscard]] bool ViewportCursorCaptured() const noexcept
        {
            return m_ViewportFlyCursorCaptured;
        }

        [[nodiscard]] std::uint64_t ViewportRenderLayers() const noexcept
        {
            return m_ViewportRenderLayers;
        }

        [[nodiscard]] kairo::renderer::DebugDrawList PhysicsDebugDraw() const
        {
            auto debug = m_PhysicsPreview.Active()
                ? m_PhysicsPreview.DebugDraw(m_ShowPhysicsBroadphase)
                : kairo::renderer::DebugDrawList{};
            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value() || !m_Project.Scene().Contains(*selected)) return debug;
            const auto& scene = m_Project.Scene();
            const auto world = scene.WorldTransform(*selected);
            if (scene.HasCamera(*selected))
            {
                debug.AddAxes(world.Translation, 0.35f);
                debug.AddLine(world.Translation,
                    world.Translation + world.Forward() * 1.5f,
                    { 0.35f, 0.8f, 1.0f, 1.0f });
            }
            if (scene.HasLight(*selected))
            {
                const auto& light = scene.Light(*selected);
                constexpr kairo::renderer::DebugColor color{ 1.0f, 0.78f, 0.18f, 1.0f };
                if (light.Type == kairo::engine::LightType::Point)
                    debug.AddWireSphere(world.Translation, std::min(light.Range, 2.0f), 20u, color);
                else if (light.Type == kairo::engine::LightType::RectangleArea)
                    debug.AddOBB(world.Translation,
                        { light.AreaWidth * 0.5f, light.AreaHeight * 0.5f, 0.01f },
                        world.Rotation, color);
                else
                {
                    debug.AddLine(world.Translation,
                        world.Translation + world.Forward() * 1.5f, color);
                    if (light.Type == kairo::engine::LightType::Spot)
                        debug.AddWireSphere(world.Translation + world.Forward(),
                            std::tan(light.OuterConeRadians), 16u, color);
                }
            }
            return debug;
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

        /// Output: a validated project descriptor requiring a host-level reload.
        /// Task: prevent renderer asset handles from surviving a project switch.
        [[nodiscard]] std::optional<std::filesystem::path> TakeProjectTransitionRequest() noexcept
        {
            return std::exchange(m_ProjectTransitionRequest, std::nullopt);
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

        /// Output: visible visual assets whose revision-keyed previews are not yet
        /// scheduled by the host. The Editor never owns backend GPU resources.
        [[nodiscard]] std::vector<AssetThumbnailRequest> TakeAssetThumbnailRequests() noexcept
        {
            return m_AssetThumbnailScheduler.TakePending();
        }

        /// Output: explicit browser requests such as reimport. The native host
        /// performs them at a safe frame boundary and may restart/rebind runtime
        /// resources without coupling the ImGui shell to an importer backend.
        [[nodiscard]] std::vector<AssetBrowserRequest> TakeAssetBrowserRequests() noexcept
        {
            return m_AssetBrowserRequests.Take();
        }

        /// Rehydrates non-authoritative text buffers after ProjectSession has
        /// restored canonical files and reopened documents. Invalid drafts stay
        /// editable in the Code surface and never enter graph/runtime state
        /// until the user fixes and explicitly applies them.
        void RestoreRecoveryDrafts(const RecoverySnapshot& snapshot)
        {
            for (const RecoveryFile& file : snapshot.Files)
            {
                if (file.Role != RecoveryFileRole::TextDraft) continue;
                const auto metadata = m_Project.Assets().FindByPath(file.TargetPath);
                if (!metadata.has_value() ||
                    metadata->Type != kairo::assets::AssetType::Document ||
                    !m_Project.Documents().Contains(metadata->ID))
                    throw std::runtime_error("Recovery text draft no longer resolves to an open document.");
                (void)m_AuthoringWorkspace.Open(m_Project.Document(metadata->ID));
                m_AuthoringWorkspace.At(metadata->ID).SetTextDraft(
                    LoadRecoveryPayload(snapshot, file));
            }
            m_RecoveryStatus = "Recovered snapshot";
        }

    private:
        EditorState& m_State;
        ProjectSession& m_Project;
        CommandHistory m_History;
        kairo::reflection::ReflectionRegistry m_Reflection;
        AuthoringWorkspaceState m_AuthoringWorkspace;
        DocumentSchemaRegistry m_Schemas = CreateCoreDocumentSchemaRegistry();
        ImGuiGraphCanvas m_GraphCanvas;
        EditorInputRouter m_InputRouter;
        DiagnosticStore m_Diagnostics;
        TransformGizmo m_TransformGizmo;
        TransformGizmoSpace m_GizmoSpace = TransformGizmoSpace::World;
        std::optional<kairo::foundation::math::Transformf> m_GizmoBefore;
        std::optional<kairo::engine::Entity> m_GizmoEntity;
        ViewportController m_ViewportController;
        PhysicsPreview m_PhysicsPreview;
        std::optional<kairo::engine::Scene> m_RuntimeScene;
        std::unique_ptr<AIEditorSession> m_AISession;
        std::unique_ptr<OfflineRenderAuthoringController> m_OfflineRender;
        std::unique_ptr<NativeGameplayAuthoringWorkspace> m_NativeGameplay;
        std::filesystem::path m_NativeGameplayPath;
        std::uint64_t m_NextOfflineRenderJob = 1u;
        int m_OfflineRenderWidth = 1280;
        int m_OfflineRenderHeight = 720;
        int m_OfflineRenderPasses = 64;
        std::array<char, 256> m_OfflineRenderOutput{ 'R', 'e', 'n', 'd', 'e', 'r', 's', '/',
            'O', 'f', 'f', 'l', 'i', 'n', 'e', '.', 'p', 'p', 'm', '\0' };
        int m_NativeGameplayType = 0;
        EditorAction m_ActiveTool = EditorAction::SelectTool;
        bool m_ViewportFocused = false;
        bool m_ShowPhysicsBroadphase = false;
        kairo::renderer::ViewportShadingMode m_ViewportShading = kairo::renderer::ViewportShadingMode::Lit;
        bool m_LayoutBuilt = false;
        bool m_RebuildLayout = true;
        bool m_DocumentPanelFocused = false;
        std::array<char, 256> m_AssetFilter{};
        const kairo::assets::ImportDatabase* m_AssetImports = nullptr;
        AssetThumbnailScheduler m_AssetThumbnailScheduler;
        AssetBrowserRequestQueue m_AssetBrowserRequests;
        std::array<char, 256> m_NewDocumentName{};
        std::array<char, 512> m_NewDocumentPath{};
        std::array<char, 4096> m_AIPrompt{};
        std::array<char, 256> m_NewProjectName{};
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
        static constexpr std::chrono::seconds AutosaveInterval{ 30 };
        std::chrono::steady_clock::time_point m_NextAutosave{};
        std::string m_RecoveryStatus;
        EditorKeymapSettings m_KeymapSettings;
        std::filesystem::path m_KeymapSettingsPath;
        NavigationSettings m_NavigationSettings;
        std::filesystem::path m_NavigationSettingsPath;
        bool m_ViewportNavigationActive = false;
        bool m_ViewportNavigationCancelled = false;
        bool m_ViewportFlyCursorCaptured = false;
        std::uint64_t m_ViewportRenderLayers = kairo::engine::AllRenderLayers;
        bool m_RequestNavigationPreferences = false;
        bool m_RequestNavigationHelp = false;
        bool m_RequestCommandPalette = false;
        std::array<char, 256> m_CommandFilter{};
        bool m_RequestKeymapEditor = false;
        std::array<char, 96> m_KeymapChord{};
        int m_KeymapAction = 0;
        int m_KeymapContext = 0;
        enum class ProjectOperation : std::uint8_t { None, Open, Create, Clone, Restore };
        ProjectOperation m_ProjectOperation = ProjectOperation::None;
        std::filesystem::path m_ProjectOperationPath;
        std::string m_ProjectOperationName;
        bool m_RequestNewProjectPopup = false;
        bool m_RequestUnsavedProjectPopup = false;
        std::optional<std::filesystem::path> m_ProjectTransitionRequest;
        RecentProjects m_RecentProjects;
        bool m_RecentProjectsLoaded = false;

        void SetKeymapProfile(KeymapProfile profile)
        {
            EditorKeymapSettings candidate = m_KeymapSettings;
            candidate.Profile = profile;
            (void)BuildInputBindings(candidate.Profile, candidate.Overrides);
            if (!m_KeymapSettingsPath.empty()) SaveKeymapSettings(m_KeymapSettingsPath, candidate);
            m_InputRouter.SetProfile(profile);
            m_KeymapSettings = std::move(candidate);
        }

        void ResetKeymapOverrides()
        {
            EditorKeymapSettings candidate = m_KeymapSettings;
            candidate.Overrides.clear();
            if (!m_KeymapSettingsPath.empty()) SaveKeymapSettings(m_KeymapSettingsPath, candidate);
            m_InputRouter.SetOverrides({});
            m_KeymapSettings = std::move(candidate);
        }

        void DrawMainBar()
        {
            if (!ImGui::BeginMainMenuBar()) return;
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Project...")) m_RequestNewProjectPopup = true;
                if (ImGui::MenuItem("Open Project...")) RunCommand([this]
                {
                    if (const auto path = ChooseProjectFile(); path.has_value())
                        QueueProjectOperation(ProjectOperation::Open, *path);
                });
                if (ImGui::BeginMenu("Open Recent"))
                {
                    if (m_RecentProjects.Entries().empty()) ImGui::MenuItem("No recent projects", nullptr, false, false);
                    for (const auto& path : m_RecentProjects.Entries())
                        if (ImGui::MenuItem(path.generic_string().c_str()))
                            QueueProjectOperation(ProjectOperation::Open, path);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Save Project As...", nullptr, false, m_Project.HasProject()))
                    m_RequestNewProjectPopup = true, m_ProjectOperation = ProjectOperation::Clone;
                ImGui::Separator();
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
                if (ImGui::MenuItem("Create Recovery Point", nullptr, false, m_Project.HasProject()))
                    RunCommand([this] { CreateRecoveryNow(); });
                if (ImGui::BeginMenu("Restore Recovery Point", m_Project.HasProject()))
                {
                    std::vector<std::filesystem::path> snapshots;
                    std::error_code error;
                    const auto recoveryRoot = m_Project.ProjectRoot() / ".kairo" / "recovery";
                    for (std::filesystem::directory_iterator iterator(recoveryRoot, error), end;
                        !error && iterator != end; iterator.increment(error))
                        if (iterator->is_directory()) snapshots.push_back(iterator->path());
                    std::ranges::sort(snapshots, std::greater{});
                    if (snapshots.empty()) ImGui::MenuItem("No recovery points", nullptr, false, false);
                    for (const auto& snapshot : snapshots)
                        if (ImGui::MenuItem(snapshot.filename().string().c_str()))
                            QueueProjectOperation(ProjectOperation::Restore, snapshot);
                    ImGui::EndMenu();
                }
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
                ImGui::Separator();
                if (ImGui::BeginMenu("Keymap Profile"))
                {
                    for (const auto profile : { KeymapProfile::Kairo, KeymapProfile::Blender,
                        KeymapProfile::Unreal, KeymapProfile::Unity })
                    {
                        const bool selected = m_InputRouter.Profile() == profile;
                        const std::string label(Name(profile));
                        if (ImGui::MenuItem(label.c_str(), nullptr, selected))
                            RunCommand([this, profile] { SetKeymapProfile(profile); });
                    }
                    if (!m_KeymapSettings.Overrides.empty())
                    {
                        ImGui::Separator();
                        if (ImGui::MenuItem("Reset Custom Bindings"))
                            RunCommand([this] { ResetKeymapOverrides(); });
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Navigation Preferences..."))
                    m_RequestNavigationPreferences = true;
                if (ImGui::MenuItem("Keyboard Shortcuts...")) m_RequestKeymapEditor = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Command Palette...", "Cmd+Shift+P"))
                    m_RequestCommandPalette = true;
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
            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Navigation Controls")) m_RequestNavigationHelp = true;
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

        }

        void QueueProjectOperation(ProjectOperation operation, std::filesystem::path path,
            std::string name = {})
        {
            m_ProjectOperation = operation;
            m_ProjectOperationPath = std::move(path);
            m_ProjectOperationName = std::move(name);
            if (m_Project.HasUnsavedChanges() || m_AuthoringWorkspace.HasDirtyTextDrafts())
                m_RequestUnsavedProjectPopup = true;
            else ExecuteProjectOperation();
        }

        void ExecuteProjectOperation()
        {
            if (m_ProjectOperation == ProjectOperation::Open)
                m_Project.OpenProject(m_ProjectOperationPath, UnsavedChangesPolicy::Discard);
            else if (m_ProjectOperation == ProjectOperation::Create)
                m_Project.CreateProject(m_ProjectOperationPath, m_ProjectOperationName,
                    UnsavedChangesPolicy::Discard);
            else if (m_ProjectOperation == ProjectOperation::Clone)
            {
                const auto descriptorName = m_Project.ProjectFile().filename();
                CloneProjectDirectory(m_Project.ProjectFile(), m_ProjectOperationPath);
                m_Project.OpenProject(m_ProjectOperationPath / descriptorName,
                    UnsavedChangesPolicy::Discard);
            }
            else if (m_ProjectOperation == ProjectOperation::Restore)
                m_Project.RestoreRecoveryPoint(m_ProjectOperationPath,
                    UnsavedChangesPolicy::Discard);
            else return;
            m_RecentProjects.Touch(m_Project.ProjectFile());
            const auto recentPath = DefaultRecentProjectsPath();
            if (!recentPath.empty()) m_RecentProjects.Save(recentPath);
            m_ProjectTransitionRequest = m_Project.ProjectFile();
            m_ProjectOperation = ProjectOperation::None;
        }

        void DrawProjectLifecyclePopups()
        {
            if (!m_RecentProjectsLoaded)
            {
                m_RecentProjectsLoaded = true;
                const auto recentPath = DefaultRecentProjectsPath();
                if (!recentPath.empty())
                {
                    RunCommand([this, recentPath]
                    {
                        m_RecentProjects = RecentProjects::Load(recentPath);
                        m_RecentProjects.PruneMissing();
                    });
                }
            }
            if (m_RequestNewProjectPopup)
            {
                if (m_ProjectOperation != ProjectOperation::Clone) m_ProjectOperation = ProjectOperation::Create;
                const std::string initial = m_ProjectOperation == ProjectOperation::Clone
                    ? m_Project.Descriptor().Name + " Copy" : "Untitled Project";
                std::snprintf(m_NewProjectName.data(), m_NewProjectName.size(), "%s", initial.c_str());
                ImGui::OpenPopup(m_ProjectOperation == ProjectOperation::Clone ? "Save Project As" : "New Project");
                m_RequestNewProjectPopup = false;
            }
            const char* title = m_ProjectOperation == ProjectOperation::Clone ? "Save Project As" : "New Project";
            if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::InputText("Name", m_NewProjectName.data(), m_NewProjectName.size());
                if (ImGui::Button("Choose Parent Folder...")) RunCommand([this]
                {
                    if (const auto parent = ChooseProjectParentDirectory(); parent.has_value())
                    {
                        const std::string name(m_NewProjectName.data());
                        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string::npos)
                            throw std::invalid_argument("Project name must be a non-empty folder name without '/'.");
                        const auto operation = m_ProjectOperation;
                        ImGui::CloseCurrentPopup();
                        QueueProjectOperation(operation, *parent / name, name);
                    }
                });
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_ProjectOperation = ProjectOperation::None;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            if (m_RequestUnsavedProjectPopup)
            {
                ImGui::OpenPopup("Unsaved project changes");
                m_RequestUnsavedProjectPopup = false;
            }
            if (ImGui::BeginPopupModal("Unsaved project changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Save changes before switching projects?");
                if (ImGui::Button("Save and Continue")) RunCommand([this]
                {
                    SaveAllWithDrafts();
                    ExecuteProjectOperation();
                    ImGui::CloseCurrentPopup();
                });
                ImGui::SameLine();
                if (ImGui::Button("Discard")) RunCommand([this]
                {
                    ExecuteProjectOperation();
                    ImGui::CloseCurrentPopup();
                });
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    m_ProjectOperation = ProjectOperation::None;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
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
                if (!m_RecoveryStatus.empty())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("%s", m_RecoveryStatus.c_str());
                }
                ImGui::EndMenuBar();
            }
            ImGui::End();
        }

        void BuildDefaultLayout(ImGuiID dockspace)
        {
            if (m_LayoutBuilt) return;
            if (!m_RebuildLayout && ImGui::DockBuilderGetNode(dockspace) != nullptr)
            {
                m_LayoutBuilt = true;
                return;
            }
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
            ImGui::DockBuilderDockWindow("Kairo AI", right);
            ImGui::DockBuilderDockWindow("Console", bottom);
            ImGui::DockBuilderDockWindow("Timeline", bottom);
            ImGui::DockBuilderDockWindow("Code", bottom);
            ImGui::DockBuilderDockWindow("Render Results", bottom);
            ImGui::DockBuilderDockWindow("Native Gameplay", right);
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
            if (ActionButton("+ Add", UIButtonTone::Primary)) ImGui::OpenPopup("Add Scene Object");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add scene object (Shift+A)");
            DrawAddSceneObjectPopup();
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
            if (ImGui::Button("Drop here to make root", ImVec2(-1.0f, 0.0f))) {}
            AcceptHierarchyDrop(std::nullopt);
            for (const auto entity : scene.RootEntities())
                DrawHierarchyEntity(scene, entity);
            ImGui::End();
        }

        void DrawHierarchyEntity(const kairo::engine::Scene& scene, kairo::engine::Entity entity)
        {
            const bool selected = m_State.SelectedEntity().has_value() &&
                *m_State.SelectedEntity() == entity;
            const bool leaf = scene.Children(entity).empty();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (selected) flags |= ImGuiTreeNodeFlags_Selected;
            if (leaf) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            const char* marker = scene.HasCamera(entity) ? "[C] " : scene.HasLight(entity) ? "[L] " :
                scene.HasEnvironment(entity) ? "[W] " : scene.HasSceneInstance(entity) ? "[S] " :
                scene.HasMeshRenderer(entity) ? "[M] " : "[ ] ";
            const std::string label = std::string(marker) + scene.Name(entity).Value +
                "##" + std::to_string(entity.Value);
            const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) m_State.Select(entity);
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("KAIRO_SCENE_ENTITY", &entity.Value, sizeof(entity.Value));
                ImGui::TextUnformatted(scene.Name(entity).Value.c_str());
                ImGui::EndDragDropSource();
            }
            AcceptHierarchyDrop(entity);
            if (open && !leaf)
            {
                for (const auto child : scene.Children(entity)) DrawHierarchyEntity(scene, child);
                ImGui::TreePop();
            }
        }

        void AcceptHierarchyDrop(std::optional<kairo::engine::Entity> parent)
        {
            if (!ImGui::BeginDragDropTarget()) return;
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KAIRO_SCENE_ENTITY"))
            {
                if (payload->DataSize == sizeof(std::uint32_t))
                {
                    const auto child = kairo::engine::Entity{
                        *static_cast<const std::uint32_t*>(payload->Data) };
                    if (!parent.has_value() || child != *parent)
                        RunCommand([this, child, parent]
                        {
                            m_History.Execute(std::make_unique<SetEntityParentCommand>(
                                m_Project, child, parent));
                        });
                }
            }
            ImGui::EndDragDropTarget();
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
            DrawRenderingComponentEditors(*selected);
            SectionHeader("Gameplay Logic");
            const char* logicPreview = "None";
            std::string logicPath;
            if (scene.HasLogic(*selected))
            {
                logicPath = m_Project.Assets().Resolve(scene.Logic(*selected).Document).Path.generic_string();
                logicPreview = logicPath.c_str();
            }
            if (ImGui::BeginCombo("Document##GameplayLogic", logicPreview))
            {
                for (const auto& metadata : m_Project.Assets().Snapshot())
                {
                    if (metadata.Type != kairo::assets::AssetType::Document) continue;
                    try
                    {
                        if (LoadDocument(m_Project.ProjectRoot() / metadata.Path).Kind() != DocumentKind::Logic)
                            continue;
                    }
                    catch (const std::exception&) { continue; }
                    const bool active = scene.HasLogic(*selected) &&
                        scene.Logic(*selected).Document.ID == metadata.ID;
                    const std::string label = metadata.Path.generic_string();
                    if (ImGui::Selectable(label.c_str(), active))
                    {
                        RunCommand([this, entity = *selected, id = metadata.ID]
                        {
                            m_History.Execute(std::make_unique<SetLogicDocumentCommand>(
                                m_Project, entity, kairo::assets::DocumentAssetHandle{ id }));
                        });
                    }
                }
                ImGui::EndCombo();
            }
            if (scene.HasLogic(*selected) && ActionButton("Remove Logic", UIButtonTone::Destructive))
            {
                RunCommand([this, entity = *selected]
                {
                    m_History.Execute(std::make_unique<SetLogicDocumentCommand>(
                        m_Project, entity, std::nullopt));
                });
            }
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

        void DrawRenderingComponentEditors(kairo::engine::Entity entity)
        {
            const auto& scene = m_Project.Scene();
            SectionHeader("Rendering");
            if (ImGui::Button("Add Component", { -1.0f, 0.0f }))
                ImGui::OpenPopup("Add Rendering Component");
            if (ImGui::BeginPopup("Add Rendering Component"))
            {
                if (!scene.HasCamera(entity) && ImGui::MenuItem("Camera"))
                    RunCommand([this, entity] { m_History.Execute(
                        std::make_unique<SetCameraComponentCommand>(m_Project, entity,
                            kairo::engine::CameraComponent{})); });
                if (!scene.HasLight(entity) && ImGui::MenuItem("Light"))
                    RunCommand([this, entity] { m_History.Execute(
                        std::make_unique<SetLightComponentCommand>(m_Project, entity,
                            kairo::engine::LightComponent{})); });
                if (!scene.HasEnvironment(entity) && ImGui::MenuItem("Environment"))
                    RunCommand([this, entity] { m_History.Execute(
                        std::make_unique<SetEnvironmentComponentCommand>(m_Project, entity,
                            kairo::engine::EnvironmentComponent{})); });
                if (!scene.HasSceneInstance(entity))
                {
                    std::optional<kairo::assets::SceneAssetHandle> firstScene;
                    for (const auto& metadata : m_Project.Assets().Snapshot())
                        if (metadata.Type == kairo::assets::AssetType::Scene)
                        {
                            firstScene = kairo::assets::SceneAssetHandle{ metadata.ID };
                            break;
                        }
                    ImGui::BeginDisabled(!firstScene.has_value());
                    if (ImGui::MenuItem("Imported Scene") && firstScene.has_value())
                    {
                        kairo::engine::SceneInstanceComponent component;
                        component.SceneAsset = *firstScene;
                        RunCommand([this, entity, component] { m_History.Execute(
                            std::make_unique<SetSceneInstanceComponentCommand>(
                                m_Project, entity, component)); });
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndPopup();
            }

            if (scene.HasMeshRenderer(entity))
            {
                auto renderer = scene.MeshRenderer(entity);
                if (ImGui::TreeNodeEx("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const auto currentMesh = m_Project.Assets().Resolve(renderer.MeshAsset);
                    if (ImGui::BeginCombo("Mesh", currentMesh.Path.generic_string().c_str()))
                    {
                        for (const auto& metadata : m_Project.Assets().Snapshot())
                        {
                            if (metadata.Type != kairo::assets::AssetType::Mesh) continue;
                            const bool selected = metadata.ID == renderer.MeshAsset.ID;
                            if (ImGui::Selectable(metadata.Path.generic_string().c_str(), selected))
                            {
                                renderer.MeshAsset = { metadata.ID };
                                RunCommand([this, entity, renderer] { m_History.Execute(
                                    std::make_unique<SetMeshRendererComponentCommand>(
                                        m_Project, entity, renderer)); });
                            }
                        }
                        ImGui::EndCombo();
                    }
                    const auto current = m_Project.Assets().Resolve(renderer.MaterialAsset);
                    if (ImGui::BeginCombo("Material", current.Path.generic_string().c_str()))
                    {
                        for (const auto& metadata : m_Project.Assets().Snapshot())
                        {
                            if (metadata.Type != kairo::assets::AssetType::Material) continue;
                            const bool selected = metadata.ID == renderer.MaterialAsset.ID;
                            const std::string label = metadata.Path.generic_string();
                            if (ImGui::Selectable(label.c_str(), selected))
                            {
                                renderer.MaterialAsset = { metadata.ID };
                                RunCommand([this, entity, renderer] { m_History.Execute(
                                    std::make_unique<SetMeshRendererComponentCommand>(
                                        m_Project, entity, renderer)); });
                            }
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SeparatorText("Additional Material Slots");
                    std::optional<std::size_t> removeMaterialSlot;
                    for (std::size_t slot = 0u; slot < renderer.AdditionalMaterialSlots.size(); ++slot)
                    {
                        ImGui::PushID(static_cast<int>(slot));
                        const auto slotMetadata = m_Project.Assets().Resolve(
                            renderer.AdditionalMaterialSlots[slot]);
                        if (ImGui::BeginCombo("Slot", slotMetadata.Path.generic_string().c_str()))
                        {
                            for (const auto& metadata : m_Project.Assets().Snapshot())
                            {
                                if (metadata.Type != kairo::assets::AssetType::Material) continue;
                                const bool selected = metadata.ID ==
                                    renderer.AdditionalMaterialSlots[slot].ID;
                                if (ImGui::Selectable(metadata.Path.generic_string().c_str(), selected))
                                {
                                    renderer.AdditionalMaterialSlots[slot] = { metadata.ID };
                                    RunCommand([this, entity, renderer] { m_History.Execute(
                                        std::make_unique<SetMeshRendererComponentCommand>(
                                            m_Project, entity, renderer)); });
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) removeMaterialSlot = slot;
                        ImGui::PopID();
                    }
                    if (removeMaterialSlot.has_value())
                    {
                        renderer.AdditionalMaterialSlots.erase(
                            renderer.AdditionalMaterialSlots.begin() +
                            static_cast<std::ptrdiff_t>(*removeMaterialSlot));
                        RunCommand([this, entity, renderer] { m_History.Execute(
                            std::make_unique<SetMeshRendererComponentCommand>(
                                m_Project, entity, renderer)); });
                    }
                    std::optional<kairo::assets::MaterialAssetHandle> firstMaterial;
                    for (const auto& metadata : m_Project.Assets().Snapshot())
                        if (metadata.Type == kairo::assets::AssetType::Material)
                        {
                            firstMaterial = kairo::assets::MaterialAssetHandle{ metadata.ID };
                            break;
                        }
                    const bool canAddMaterialSlot = firstMaterial.has_value() &&
                        renderer.MaterialSlotCount() < kairo::engine::MaximumMaterialSlots;
                    ImGui::BeginDisabled(!canAddMaterialSlot);
                    if (ImGui::SmallButton("+ Add Material Slot") && firstMaterial.has_value())
                    {
                        renderer.AdditionalMaterialSlots.push_back(*firstMaterial);
                        RunCommand([this, entity, renderer] { m_History.Execute(
                            std::make_unique<SetMeshRendererComponentCommand>(
                                m_Project, entity, renderer)); });
                    }
                    ImGui::EndDisabled();
                    bool visible = renderer.Visible;
                    bool cast = renderer.CastShadows;
                    bool receive = renderer.ReceiveShadows;
                    bool changed = ImGui::Checkbox("Visible", &visible);
                    changed |= ImGui::Checkbox("Cast Shadows", &cast);
                    changed |= ImGui::Checkbox("Receive Shadows", &receive);
                    changed |= ImGui::InputScalar("Render Layers", ImGuiDataType_U64,
                        &renderer.RenderLayers, nullptr, nullptr, "%016llX",
                        ImGuiInputTextFlags_CharsHexadecimal);
                    if (changed)
                    {
                        renderer.Visible = visible;
                        renderer.CastShadows = cast;
                        renderer.ReceiveShadows = receive;
                        RunCommand([this, entity, renderer] { m_History.Execute(
                            std::make_unique<SetMeshRendererComponentCommand>(
                                m_Project, entity, renderer)); });
                    }
                    ImGui::TreePop();
                }
            }

            if (scene.HasSceneInstance(entity))
            {
                auto instance = scene.SceneInstance(entity);
                if (ImGui::TreeNodeEx("Imported Scene", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const auto current = m_Project.Assets().Resolve(instance.SceneAsset);
                    if (ImGui::BeginCombo("Scene Asset", current.Path.generic_string().c_str()))
                    {
                        for (const auto& metadata : m_Project.Assets().Snapshot())
                        {
                            if (metadata.Type != kairo::assets::AssetType::Scene) continue;
                            const bool selected = metadata.ID == instance.SceneAsset.ID;
                            const std::string label = metadata.Path.generic_string();
                            if (ImGui::Selectable(label.c_str(), selected))
                            {
                                instance.SceneAsset = { metadata.ID };
                                RunCommand([this, entity, instance] { m_History.Execute(
                                    std::make_unique<SetSceneInstanceComponentCommand>(
                                        m_Project, entity, instance)); });
                            }
                        }
                        ImGui::EndCombo();
                    }
                    bool visible = instance.Visible;
                    bool cast = instance.CastShadows;
                    bool receive = instance.ReceiveShadows;
                    bool changed = ImGui::Checkbox("Visible##SceneInstance", &visible);
                    changed |= ImGui::Checkbox("Cast Shadows##SceneInstance", &cast);
                    changed |= ImGui::Checkbox("Receive Shadows##SceneInstance", &receive);
                    changed |= ImGui::InputScalar("Render Layers##SceneInstance",
                        ImGuiDataType_U64, &instance.RenderLayers, nullptr, nullptr,
                        "%016llX", ImGuiInputTextFlags_CharsHexadecimal);
                    if (changed)
                    {
                        instance.Visible = visible;
                        instance.CastShadows = cast;
                        instance.ReceiveShadows = receive;
                        RunCommand([this, entity, instance] { m_History.Execute(
                            std::make_unique<SetSceneInstanceComponentCommand>(
                                m_Project, entity, instance)); });
                    }
                    if (ActionButton("Remove##SceneInstance", UIButtonTone::Destructive))
                        RunCommand([this, entity] { m_History.Execute(
                            std::make_unique<SetSceneInstanceComponentCommand>(
                                m_Project, entity, std::nullopt)); });
                    ImGui::TreePop();
                }
            }

            if (scene.HasCamera(entity))
            {
                auto camera = scene.Camera(entity);
                if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool changed = false;
                    int projection = static_cast<int>(camera.Projection);
                    const char* projections[] = { "Perspective", "Orthographic" };
                    if (ImGui::Combo("Projection", &projection, projections, 2))
                    {
                        camera.Projection = static_cast<kairo::engine::CameraProjection>(projection);
                        changed = true;
                    }
                    constexpr float RadiansToDegrees = 57.2957795131f;
                    constexpr float DegreesToRadians = 0.01745329252f;
                    float fovDegrees = camera.VerticalFovRadians * RadiansToDegrees;
                    if (camera.Projection == kairo::engine::CameraProjection::Perspective &&
                        ImGui::SliderFloat("Vertical FOV", &fovDegrees, 1.0f, 179.0f, "%.1f deg"))
                    {
                        camera.VerticalFovRadians = fovDegrees * DegreesToRadians;
                        changed = true;
                    }
                    if (camera.Projection == kairo::engine::CameraProjection::Orthographic)
                        changed |= ImGui::DragFloat("Orthographic Size", &camera.OrthographicSize,
                            0.05f, 0.01f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Near", &camera.NearPlane, 0.01f,
                        0.001f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Far", &camera.FarPlane, 1.0f,
                        0.01f, 100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Exposure EV100", &camera.ExposureEV100, 0.1f);
                    int clearMode = static_cast<int>(camera.ClearMode);
                    const char* clearModes[] = { "Environment", "Solid Color", "Depth Only", "Nothing" };
                    if (ImGui::Combo("Clear Mode", &clearMode, clearModes, 4))
                    {
                        camera.ClearMode = static_cast<kairo::engine::CameraClearMode>(clearMode);
                        changed = true;
                    }
                    if (camera.ClearMode == kairo::engine::CameraClearMode::SolidColor)
                        changed |= ImGui::ColorEdit4("Clear Color", &camera.ClearColor.x,
                            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                    changed |= ImGui::InputScalar("Render Layers", ImGuiDataType_U64,
                        &camera.RenderLayers, nullptr, nullptr, "%016llX",
                        ImGuiInputTextFlags_CharsHexadecimal);
                    const auto primary = scene.PrimaryCamera();
                    const bool anotherPrimary = primary.has_value() && *primary != entity;
                    ImGui::BeginDisabled(anotherPrimary);
                    changed |= ImGui::Checkbox("Primary", &camera.Primary);
                    ImGui::EndDisabled();
                    if (anotherPrimary && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip("Another active camera is primary.");
                    if (changed) RunCommand([this, entity, camera] { m_History.Execute(
                        std::make_unique<SetCameraComponentCommand>(m_Project, entity, camera)); });

                    if (ImGui::Button("View Through Camera"))
                    {
                        const auto world = scene.WorldTransform(entity);
                        m_ViewportController.SetPose({ world.Translation,
                            world.Translation + world.Forward(), world.Up() });
                        m_ViewportRenderLayers = camera.RenderLayers;
                    }
                    ImGui::SameLine();
                    if (ActionButton("Remove##Camera", UIButtonTone::Destructive))
                        RunCommand([this, entity] { m_History.Execute(
                            std::make_unique<SetCameraComponentCommand>(
                                m_Project, entity, std::nullopt)); });
                    ImGui::TreePop();
                }
            }

            if (scene.HasLight(entity))
            {
                auto light = scene.Light(entity);
                if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool changed = false;
                    int type = static_cast<int>(light.Type);
                    const char* types[] = { "Directional", "Point", "Spot", "Rectangle Area" };
                    if (ImGui::Combo("Type", &type, types, 4))
                    {
                        light.Type = static_cast<kairo::engine::LightType>(type);
                        light.Unit = light.Type == kairo::engine::LightType::Directional
                            ? kairo::engine::PhotometricUnit::Lux
                            : light.Type == kairo::engine::LightType::RectangleArea
                            ? kairo::engine::PhotometricUnit::Nit
                            : kairo::engine::PhotometricUnit::Candela;
                        changed = true;
                    }
                    changed |= ImGui::ColorEdit3("Color", &light.Color.x,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                    changed |= ImGui::DragFloat("Intensity", &light.Intensity, 5.0f,
                        0.0f, 1'000'000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    if (light.Type != kairo::engine::LightType::Directional)
                        changed |= ImGui::DragFloat("Range", &light.Range, 0.1f,
                            0.01f, 100000.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
                    if (light.Type == kairo::engine::LightType::Spot)
                    {
                        changed |= ImGui::SliderFloat("Inner Cone", &light.InnerConeRadians,
                            0.0f, light.OuterConeRadians, "%.3f rad");
                        changed |= ImGui::SliderFloat("Outer Cone", &light.OuterConeRadians,
                            std::max(light.InnerConeRadians, 0.001f), 1.569f, "%.3f rad");
                    }
                    if (light.Type == kairo::engine::LightType::RectangleArea)
                    {
                        changed |= ImGui::DragFloat("Area Width", &light.AreaWidth,
                            0.05f, 0.01f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                        changed |= ImGui::DragFloat("Area Height", &light.AreaHeight,
                            0.05f, 0.01f, 10000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    }
                    int shadows = static_cast<int>(light.Shadows);
                    const char* shadowModes[] = { "Disabled", "Hard", "Soft" };
                    if (ImGui::Combo("Shadows", &shadows, shadowModes, 3))
                    {
                        light.Shadows = static_cast<kairo::engine::ShadowPolicy>(shadows);
                        changed = true;
                    }
                    if (light.Shadows != kairo::engine::ShadowPolicy::Disabled)
                    {
                        changed |= ImGui::DragFloat("Shadow Bias", &light.ShadowBias,
                            0.0001f, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
                        changed |= ImGui::DragFloat("Normal Bias", &light.ShadowNormalBias,
                            0.0001f, 0.0f, 1.0f, "%.5f", ImGuiSliderFlags_AlwaysClamp);
                    }
                    changed |= ImGui::InputScalar("Render Layers", ImGuiDataType_U64,
                        &light.RenderLayers, nullptr, nullptr, "%016llX",
                        ImGuiInputTextFlags_CharsHexadecimal);
                    if (changed) RunCommand([this, entity, light] { m_History.Execute(
                        std::make_unique<SetLightComponentCommand>(m_Project, entity, light)); });
                    if (ActionButton("Remove##Light", UIButtonTone::Destructive))
                        RunCommand([this, entity] { m_History.Execute(
                            std::make_unique<SetLightComponentCommand>(
                                m_Project, entity, std::nullopt)); });
                    ImGui::TreePop();
                }
            }

            if (scene.HasEnvironment(entity))
            {
                auto environment = scene.Environment(entity);
                if (ImGui::TreeNodeEx("Environment", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool changed = ImGui::Checkbox("Enabled", &environment.Enabled);
                    changed |= ImGui::InputInt("Priority", &environment.Priority);
                    changed |= ImGui::ColorEdit3("Background", &environment.BackgroundColor.x,
                        ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                    changed |= ImGui::DragFloat("Ambient", &environment.AmbientIntensity,
                        0.01f, 0.0f, 1000.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Environment Strength",
                        &environment.EnvironmentIntensity, 0.01f, 0.0f, 1000.0f,
                        "%.3f", ImGuiSliderFlags_AlwaysClamp);
                    changed |= ImGui::DragFloat("Exposure", &environment.ExposureEV100, 0.1f);
                    const std::string environmentTexture = environment.EnvironmentTexture.has_value()
                        ? m_Project.Assets().Resolve(*environment.EnvironmentTexture).Path.generic_string()
                        : "None";
                    if (ImGui::BeginCombo("Environment Texture", environmentTexture.c_str()))
                    {
                        if (ImGui::Selectable("None", !environment.EnvironmentTexture.has_value()))
                        {
                            environment.EnvironmentTexture.reset();
                            changed = true;
                        }
                        for (const auto& metadata : m_Project.Assets().Snapshot())
                        {
                            if (metadata.Type != kairo::assets::AssetType::Texture2D) continue;
                            const bool selected = environment.EnvironmentTexture.has_value() &&
                                environment.EnvironmentTexture->ID == metadata.ID;
                            if (ImGui::Selectable(metadata.Path.generic_string().c_str(), selected))
                            {
                                environment.EnvironmentTexture =
                                    kairo::assets::TextureAssetHandle{ metadata.ID };
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    int fog = static_cast<int>(environment.Fog);
                    const char* fogModes[] = { "Disabled", "Linear", "Exponential" };
                    if (ImGui::Combo("Fog", &fog, fogModes, 3))
                    {
                        environment.Fog = static_cast<kairo::engine::FogMode>(fog);
                        changed = true;
                    }
                    if (environment.Fog != kairo::engine::FogMode::Disabled)
                    {
                        changed |= ImGui::ColorEdit3("Fog Color", &environment.FogColor.x,
                            ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
                        if (environment.Fog == kairo::engine::FogMode::Linear)
                        {
                            changed |= ImGui::DragFloat("Fog Near", &environment.FogNear,
                                0.1f, 0.0f, environment.FogFar, "%.2f");
                            changed |= ImGui::DragFloat("Fog Far", &environment.FogFar,
                                0.1f, std::max(environment.FogNear + 0.001f, 0.001f),
                                100000.0f, "%.2f");
                        }
                        else changed |= ImGui::DragFloat("Fog Density", &environment.FogDensity,
                            0.001f, 0.0f, 1000.0f, "%.4f", ImGuiSliderFlags_AlwaysClamp);
                    }
                    int toneMap = static_cast<int>(environment.ToneMap);
                    const char* toneMaps[] = { "None", "Reinhard", "ACES" };
                    if (ImGui::Combo("Tone Mapping", &toneMap, toneMaps, 3))
                    {
                        environment.ToneMap = static_cast<kairo::engine::ToneMapping>(toneMap);
                        changed = true;
                    }
                    if (changed) RunCommand([this, entity, environment] { m_History.Execute(
                        std::make_unique<SetEnvironmentComponentCommand>(
                            m_Project, entity, environment)); });
                    if (ActionButton("Remove##Environment", UIButtonTone::Destructive))
                        RunCommand([this, entity] { m_History.Execute(
                            std::make_unique<SetEnvironmentComponentCommand>(
                                m_Project, entity, std::nullopt)); });
                    ImGui::TreePop();
                }
            }
        }

        void DrawViewport()
        {
            if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse))
            {
                m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const auto camera = m_ViewportController.Pose();
                ImGui::TextDisabled("Perspective");
                ImGui::SameLine();
                if (ImGui::BeginCombo("##ViewportShading", kairo::renderer::Name(m_ViewportShading).data(),
                    ImGuiComboFlags_WidthFitPreview))
                {
                    for (const auto mode : { kairo::renderer::ViewportShadingMode::Lit,
                        kairo::renderer::ViewportShadingMode::Unlit,
                        kairo::renderer::ViewportShadingMode::Normals,
                        kairo::renderer::ViewportShadingMode::Lighting })
                    {
                        const bool selected = m_ViewportShading == mode;
                        if (ImGui::Selectable(kairo::renderer::Name(mode).data(), selected)) m_ViewportShading = mode;
                        if (selected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                ImGui::TextDisabled("|  %s", m_State.Mode() == EditorMode::Edit ? "Edit" : "Runtime");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::SelectTool, "Q");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::TranslateTool, "W");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::RotateTool, "E");
                ImGui::SameLine();
                DrawViewportToolButton(EditorAction::ScaleTool, "R");
                ImGui::SameLine();
                if (ToolbarButton(m_GizmoSpace == TransformGizmoSpace::World ? "World" : "Local", false))
                    m_GizmoSpace = m_GizmoSpace == TransformGizmoSpace::World
                        ? TransformGizmoSpace::Local : TransformGizmoSpace::World;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Transform orientation");
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
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("KAIRO_ASSET_ID"))
                    {
                        if (payload->DataSize == static_cast<int>(AssetDragPayload{}.Bytes.size()))
                        {
                            AssetDragPayload assetPayload;
                            std::memcpy(assetPayload.Bytes.data(), payload->Data,
                                assetPayload.Bytes.size());
                            RunCommand([this, id = assetPayload.Asset()] { PlaceDroppedAsset(id); });
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                const bool navigationClick = ImGui::GetIO().KeyAlt;
                if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !navigationClick)
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
                const bool gizmoOwnsPointer = DrawTransformGizmo(viewportMin, viewportSize);
                DrawOrientationGizmo(viewportMin, viewportSize);
                HandleViewportNavigation(hovered && !gizmoOwnsPointer);

                const ImVec2 overlay = { viewportMin.x + 12.0f, viewportMin.y + 12.0f };
                ImGui::GetWindowDrawList()->AddText(overlay, IM_COL32(210, 225, 238, 210),
                    m_ActiveTool == EditorAction::SelectTool ? "SELECT" :
                    m_ActiveTool == EditorAction::TranslateTool ? "MOVE" :
                    m_ActiveTool == EditorAction::RotateTool ? "ROTATE" : "SCALE");
                ImGui::GetWindowDrawList()->AddText({ overlay.x, overlay.y + 18.0f }, IM_COL32(135, 165, 184, 190),
                    "Option+LMB orbit  Shift+Option+LMB pan  Ctrl+Option+LMB dolly  RMB+WASD fly");
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

        void PlaceDroppedAsset(kairo::assets::AssetID asset)
        {
            const auto metadata = m_Project.Assets().At(asset);
            if (!CanPlaceAssetInScene(metadata.Type))
                throw std::invalid_argument("Only mesh and imported-scene assets can be placed directly in the viewport.");
            auto command = std::make_unique<PlaceAssetInSceneCommand>(m_Project, asset);
            auto* placed = command.get();
            m_History.Execute(std::move(command));
            const auto entity = placed->CreatedEntity();
            m_State.Select(entity);
            m_ViewportController.Focus(m_Project.Scene().Transform(entity).Local.Translation);
        }

        void DrawViewportToolButton(EditorAction tool, const char* label)
        {
            if (ToolbarButton(label, m_ActiveTool == tool)) m_ActiveTool = tool;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)",
                BindingFor(tool).DisplayName.data(), BindingFor(tool).Shortcut.data());
        }

        void OpenAddPrimitivePopup()
        {
            ImGui::OpenPopup("Add Scene Object");
        }

        void DrawPrimitivePopup()
        {
            DrawAddSceneObjectPopup();
        }

        void DrawAddSceneObjectPopup()
        {
            if (!ImGui::BeginPopup("Add Scene Object")) return;
            if (ImGui::MenuItem("Empty Entity")) CreateSceneObject(SceneObjectKind::Empty);
            if (ImGui::MenuItem("Camera")) CreateSceneObject(SceneObjectKind::Camera);
            if (ImGui::BeginMenu("Light"))
            {
                if (ImGui::MenuItem("Directional")) CreateSceneObject(SceneObjectKind::DirectionalLight);
                if (ImGui::MenuItem("Point")) CreateSceneObject(SceneObjectKind::PointLight);
                if (ImGui::MenuItem("Spot")) CreateSceneObject(SceneObjectKind::SpotLight);
                if (ImGui::MenuItem("Rectangle Area")) CreateSceneObject(SceneObjectKind::RectangleAreaLight);
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Environment")) CreateSceneObject(SceneObjectKind::Environment);
            ImGui::Separator();
            for (const PrimitiveKind kind : { PrimitiveKind::Cube, PrimitiveKind::Plane,
                PrimitiveKind::UVSphere, PrimitiveKind::Cylinder })
            {
                if (ImGui::MenuItem(Name(kind).data())) CreatePrimitive(kind);
            }
            ImGui::EndPopup();
        }

        void CreateSceneObject(SceneObjectKind kind)
        {
            auto command = std::make_unique<CreateSceneObjectCommand>(m_Project, kind);
            auto* created = command.get();
            RunCommand([this, &command] { m_History.Execute(std::move(command)); });
            if (command == nullptr)
            {
                m_State.Select(created->CreatedEntity());
                m_ViewportController.Focus(
                    m_Project.Scene().Transform(created->CreatedEntity()).Local.Translation);
            }
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

        void DispatchSceneActions()
        {
            if (m_InputRouter.Consume(EditorAction::AddPrimitive)) OpenAddPrimitivePopup();
            if (m_InputRouter.Consume(EditorAction::SelectTool)) m_ActiveTool = EditorAction::SelectTool;
            if (m_InputRouter.Consume(EditorAction::TranslateTool)) m_ActiveTool = EditorAction::TranslateTool;
            if (m_InputRouter.Consume(EditorAction::RotateTool)) m_ActiveTool = EditorAction::RotateTool;
            if (m_InputRouter.Consume(EditorAction::ScaleTool)) m_ActiveTool = EditorAction::ScaleTool;
            if (m_InputRouter.Consume(EditorAction::FocusSelection)) FocusSelection();
            const auto selected = m_State.SelectedEntity();
            if (selected.has_value() && m_InputRouter.Consume(EditorAction::DeleteSelection))
            {
                RunCommand([this, entity = *selected]
                {
                    m_History.Execute(std::make_unique<DeleteEntityCommand>(m_Project, entity));
                    m_State.ClearSelection();
                });
            }
            if (selected.has_value() && m_InputRouter.Consume(EditorAction::Duplicate))
            {
                auto command = std::make_unique<DuplicateEntityCommand>(m_Project, *selected);
                auto* duplicate = command.get();
                RunCommand([this, &command, duplicate]
                {
                    m_History.Execute(std::move(command));
                    m_State.Select(duplicate->DuplicatedRoot());
                });
            }
        }

        void RouteAndDispatchInput()
        {
            const ImGuiIO& io = ImGui::GetIO();
            const InputContext context = io.WantTextInput ? InputContext::Text :
                m_GraphCanvas.Focused() ? InputContext::Graph :
                m_ViewportFocused ? InputContext::Scene : InputContext::Global;
            m_InputRouter.BeginFrame();
            m_InputRouter.SetContext(context);
            const KeyModifiers modifiers =
                (io.KeyShift ? KeyModifiers::Shift : KeyModifiers::None) |
                ((io.KeySuper || io.KeyCtrl) ? KeyModifiers::Shortcut : KeyModifiers::None) |
                (io.KeyAlt ? KeyModifiers::Alt : KeyModifiers::None);
            constexpr std::array keys{
                std::pair{ EditorKey::A, ImGuiKey_A }, std::pair{ EditorKey::C, ImGuiKey_C },
                std::pair{ EditorKey::D, ImGuiKey_D }, std::pair{ EditorKey::E, ImGuiKey_E },
                std::pair{ EditorKey::F, ImGuiKey_F }, std::pair{ EditorKey::G, ImGuiKey_G },
                std::pair{ EditorKey::N, ImGuiKey_N }, std::pair{ EditorKey::Q, ImGuiKey_Q },
                std::pair{ EditorKey::R, ImGuiKey_R }, std::pair{ EditorKey::S, ImGuiKey_S },
                std::pair{ EditorKey::V, ImGuiKey_V }, std::pair{ EditorKey::W, ImGuiKey_W },
                std::pair{ EditorKey::X, ImGuiKey_X }, std::pair{ EditorKey::Z, ImGuiKey_Z },
                std::pair{ EditorKey::Space, ImGuiKey_Space }, std::pair{ EditorKey::Home, ImGuiKey_Home },
                std::pair{ EditorKey::Backspace, ImGuiKey_Backspace },
                std::pair{ EditorKey::Delete, ImGuiKey_Delete }, std::pair{ EditorKey::F5, ImGuiKey_F5 }
            };
            for (const auto [key, native] : keys)
                if (ImGui::IsKeyPressed(native, false)) (void)m_InputRouter.Route({ { key, modifiers } });

            if (!m_Project.HasProject()) return;
            if (m_InputRouter.Consume(EditorAction::NewDocument)) RequestNewDocument();
            if (m_InputRouter.Consume(EditorAction::CloseDocument))
                if (const auto active = m_Project.Documents().ActiveID(); active.has_value()) RequestCloseDocument(*active);
            if (m_InputRouter.Consume(EditorAction::SaveAll)) RunCommand([this] { SaveAllWithDrafts(); });
            if (m_InputRouter.Consume(EditorAction::Save))
            {
                const auto active = m_Project.Documents().ActiveID();
                if (m_DocumentPanelFocused && active.has_value())
                    RunCommand([this, id = *active] { SaveDocumentWithDraft(id); });
                else RunCommand([this] { m_Project.SaveScene(); });
            }
            CommandHistory& history = ActiveHistory();
            if (m_InputRouter.Consume(EditorAction::Redo) && history.CanRedo()) RunCommand([&history] { history.Redo(); });
            else if (m_InputRouter.Consume(EditorAction::Undo) && history.CanUndo()) RunCommand([&history] { history.Undo(); });
            if (m_InputRouter.Consume(EditorAction::TogglePlay))
            {
                if (m_State.Mode() == EditorMode::Edit) StartPlay(); else StopPlay();
            }
            if (context == InputContext::Scene) DispatchSceneActions();
            if (context == InputContext::Graph)
                for (const EditorAction action : { EditorAction::GraphAddNode, EditorAction::GraphDelete,
                    EditorAction::GraphDuplicate, EditorAction::GraphCopy, EditorAction::GraphPaste,
                    EditorAction::GraphFrameSelection, EditorAction::GraphFrameAll })
                    if (m_InputRouter.Consume(action)) m_GraphCanvas.QueueAction(action);
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
            const ImGuiIO& io = ImGui::GetIO();
            const bool optionLeft = io.KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            const bool rightMouse = ImGui::IsMouseDown(ImGuiMouseButton_Right);
            const bool middleMouse = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
            const bool requested = optionLeft || rightMouse || middleMouse;
            if (!requested)
            {
                m_ViewportNavigationActive = false;
                m_ViewportNavigationCancelled = false;
            }
            if (hovered && requested && !m_ViewportNavigationCancelled)
                m_ViewportNavigationActive = true;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                m_ViewportNavigationActive = false;
                m_ViewportNavigationCancelled = true;
            }
            m_ViewportFlyCursorCaptured = m_ViewportNavigationActive && rightMouse;
            if (!hovered && !m_ViewportNavigationActive) return;
            ViewportInput input;
            input.MouseDeltaX = io.MouseDelta.x;
            input.MouseDeltaY = io.MouseDelta.y;
            input.WheelDelta = hovered && m_NavigationSettings.ScrollBehavior == ViewportScrollBehavior::Dolly
                ? io.MouseWheel : 0.0f;
            input.DeltaSeconds = io.DeltaTime;
            input.Orbit = (middleMouse && !io.KeyShift) || (optionLeft && !io.KeyShift && !io.KeyCtrl);
            input.Pan = (middleMouse && io.KeyShift) || (optionLeft && io.KeyShift);
            input.Dolly = optionLeft && io.KeyCtrl && !io.KeyShift;
            input.Fly = rightMouse;
            if (hovered && m_NavigationSettings.ScrollBehavior == ViewportScrollBehavior::Pan && io.MouseWheel != 0.0f)
            {
                input.Pan = true;
                input.MouseDeltaX = io.MouseWheelH * 24.0f;
                input.MouseDeltaY = io.MouseWheel * 24.0f;
            }
            if (rightMouse)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_None);
                input.MoveForward = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
                input.MoveRight = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
                input.MoveUp = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.0f : 0.0f) -
                    (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.0f : 0.0f);
            }
            m_ViewportController.Update(input, m_NavigationSettings);

        }

        void PersistNavigationSettings()
        {
            ValidateNavigationSettings(m_NavigationSettings);
            if (!m_NavigationSettingsPath.empty())
                SaveNavigationSettings(m_NavigationSettingsPath, m_NavigationSettings);
        }

        void DrawNavigationPreferences()
        {
            if (m_RequestNavigationPreferences)
            {
                ImGui::OpenPopup("Navigation Preferences");
                m_RequestNavigationPreferences = false;
            }
            if (!ImGui::BeginPopupModal("Navigation Preferences", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("Viewport");
            ImGui::DragFloat("Orbit sensitivity", &m_NavigationSettings.OrbitSensitivity, 0.0001f, 0.0001f, 0.1f, "%.4f");
            ImGui::DragFloat("Pan sensitivity", &m_NavigationSettings.PanSensitivity, 0.0001f, 0.0001f, 0.1f, "%.4f");
            ImGui::DragFloat("Dolly sensitivity", &m_NavigationSettings.DollySensitivity, 0.0001f, 0.0001f, 0.2f, "%.4f");
            ImGui::DragFloat("Fly speed", &m_NavigationSettings.FlySpeed, 0.05f, 0.05f, 50.0f, "%.2f");
            ImGui::Checkbox("Invert orbit X", &m_NavigationSettings.InvertOrbitX);
            ImGui::Checkbox("Invert orbit Y", &m_NavigationSettings.InvertOrbitY);
            int scroll = m_NavigationSettings.ScrollBehavior == ViewportScrollBehavior::Dolly ? 0 : 1;
            if (ImGui::Combo("Two-finger scroll", &scroll, "Dolly\0Pan\0"))
                m_NavigationSettings.ScrollBehavior = scroll == 0
                    ? ViewportScrollBehavior::Dolly : ViewportScrollBehavior::Pan;
            ImGui::Separator();
            if (ImGui::Button("Save")) RunCommand([this]
            {
                PersistNavigationSettings();
                ImGui::CloseCurrentPopup();
            });
            ImGui::SameLine();
            if (ImGui::Button("Reset")) m_NavigationSettings = {};
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void DrawNavigationHelp()
        {
            if (m_RequestNavigationHelp)
            {
                ImGui::OpenPopup("Navigation Controls");
                m_RequestNavigationHelp = false;
            }
            if (!ImGui::BeginPopupModal("Navigation Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::TextUnformatted("3D viewport");
            ImGui::BulletText("Option + left drag: orbit");
            ImGui::BulletText("Shift + Option + left drag: pan");
            ImGui::BulletText("Control + Option + left drag: dolly");
            ImGui::BulletText("Right drag + W/A/S/D/Q/E: fly and look");
            ImGui::BulletText("Wheel / two-finger scroll: configured pan or dolly");
            ImGui::BulletText("F: frame selection; Escape: cancel navigation");
            ImGui::Separator();
            ImGui::TextUnformatted("2D canvases");
            ImGui::BulletText("Space + left drag or middle drag: pan");
            ImGui::BulletText("Wheel / pinch gesture: zoom at pointer");
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void DrawCommandPalette()
        {
            const ImGuiIO& io = ImGui::GetIO();
            if ((io.KeySuper || io.KeyCtrl) && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P, false))
                m_RequestCommandPalette = true;
            if (m_RequestCommandPalette)
            {
                m_CommandFilter.fill('\0');
                ImGui::OpenPopup("Command Palette");
                m_RequestCommandPalette = false;
            }
            if (!ImGui::BeginPopupModal("Command Palette", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::SetNextItemWidth(440.0f);
            ImGui::InputTextWithHint("##command-search", "Search actions...",
                m_CommandFilter.data(), m_CommandFilter.size());
            const std::string filter = Lower(m_CommandFilter.data());
            if (ImGui::BeginChild("##commands", { 440.0f, 300.0f }, ImGuiChildFlags_Borders))
            {
                for (const auto& binding : DefaultEditorActionBindings())
                {
                    if (!filter.empty() && Lower(binding.DisplayName).find(filter) == std::string::npos) continue;
                    const std::string label = std::string(binding.DisplayName) + "\t" +
                        std::string(binding.Shortcut);
                    if (ImGui::Selectable(label.c_str()))
                    {
                        m_InputRouter.Trigger(binding.Action);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::EndChild();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        void ApplyKeymapEdit(bool unbind)
        {
            const auto action = static_cast<EditorAction>(m_KeymapAction);
            constexpr std::array contexts{ InputContext::Global, InputContext::Scene,
                InputContext::Graph, InputContext::Modeling, InputContext::Code, InputContext::Play };
            const auto context = contexts.at(static_cast<std::size_t>(m_KeymapContext));
            EditorKeymapSettings candidate = m_KeymapSettings;
            std::erase_if(candidate.Overrides, [&](const KeymapOverride& value)
            { return value.Action == action && value.Context == context; });
            KeymapOverride replacement{ action, context, {} };
            if (!unbind) replacement.Chords.push_back(ParseInputChord(m_KeymapChord.data()));
            candidate.Overrides.push_back(std::move(replacement));
            (void)BuildInputBindings(candidate.Profile, candidate.Overrides);
            if (!m_KeymapSettingsPath.empty()) SaveKeymapSettings(m_KeymapSettingsPath, candidate);
            m_InputRouter.SetOverrides(candidate.Overrides);
            m_KeymapSettings = std::move(candidate);
        }

        void DrawKeymapEditor()
        {
            if (m_RequestKeymapEditor)
            {
                m_KeymapChord.fill('\0');
                ImGui::OpenPopup("Keyboard Shortcuts");
                m_RequestKeymapEditor = false;
            }
            if (!ImGui::BeginPopupModal("Keyboard Shortcuts", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
            ImGui::Text("Profile: %s", std::string(Name(m_KeymapSettings.Profile)).c_str());
            if (ImGui::BeginChild("##effective-bindings", { 620.0f, 260.0f }, ImGuiChildFlags_Borders))
                for (const auto& binding : m_InputRouter.Bindings())
                    ImGui::BulletText("%-18s  %-8s  %s", BindingFor(binding.Action).DisplayName.data(),
                        Name(binding.Context).data(), FormatInputChord(binding.Chord).c_str());
            ImGui::EndChild();
            constexpr auto actions = DefaultEditorActionBindings();
            if (ImGui::BeginCombo("Action", actions.at(static_cast<std::size_t>(m_KeymapAction)).DisplayName.data()))
            {
                for (std::size_t index = 0u; index < actions.size(); ++index)
                    if (ImGui::Selectable(actions[index].DisplayName.data(), m_KeymapAction == static_cast<int>(index)))
                        m_KeymapAction = static_cast<int>(index);
                ImGui::EndCombo();
            }
            constexpr std::array contexts{ InputContext::Global, InputContext::Scene,
                InputContext::Graph, InputContext::Modeling, InputContext::Code, InputContext::Play };
            if (ImGui::BeginCombo("Context", Name(contexts.at(static_cast<std::size_t>(m_KeymapContext))).data()))
            {
                for (std::size_t index = 0u; index < contexts.size(); ++index)
                    if (ImGui::Selectable(Name(contexts[index]).data(), m_KeymapContext == static_cast<int>(index)))
                        m_KeymapContext = static_cast<int>(index);
                ImGui::EndCombo();
            }
            ImGui::InputTextWithHint("Chord", "shortcut+shift+p", m_KeymapChord.data(), m_KeymapChord.size());
            if (ImGui::Button("Replace Binding")) RunCommand([this] { ApplyKeymapEdit(false); });
            ImGui::SameLine();
            if (ImGui::Button("Unbind")) RunCommand([this] { ApplyKeymapEdit(true); });
            ImGui::SameLine();
            if (ImGui::Button("Reset All")) RunCommand([this] { ResetKeymapOverrides(); });
            ImGui::SameLine();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        [[nodiscard]] bool DrawTransformGizmo(ImVec2 viewportMin, ImVec2 viewportSize)
        {
            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value() || m_ActiveTool == EditorAction::SelectTool ||
                m_State.Mode() != EditorMode::Edit) return false;
            using namespace kairo::foundation::math;
            const auto camera = m_ViewportController.Pose();
            const Mat4f view = LookAt(camera.Position, camera.Target, camera.Up);
            Mat4f projection = Perspective(1.0471975512f,
                viewportSize.x / std::max(viewportSize.y, 1.0f), 0.1f, 100.0f);
            projection(1u, 1u) *= -1.0f;
            const TransformGizmoOperation operation = m_ActiveTool == EditorAction::TranslateTool
                ? TransformGizmoOperation::Translate : m_ActiveTool == EditorAction::RotateTool
                ? TransformGizmoOperation::Rotate : TransformGizmoOperation::Scale;
            const float snap = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper
                ? (operation == TransformGizmoOperation::Rotate ? 15.0f : 0.5f) : 0.0f;
            const auto result = m_TransformGizmo.Draw({ view, projection,
                m_Project.Scene().Transform(*selected).Local, viewportMin.x, viewportMin.y,
                viewportSize.x, viewportSize.y, snap, operation, m_GizmoSpace });

            if (result.Active && !m_GizmoBefore.has_value())
            {
                m_GizmoBefore = m_Project.Scene().Transform(*selected).Local;
                m_GizmoEntity = *selected;
            }
            if (result.Changed)
            {
                const auto target = m_GizmoEntity.value_or(*selected);
                if (m_Project.Scene().Contains(target)) m_Project.EditScene().Transform(target).Local = result.Transform;
            }
            if (m_GizmoBefore.has_value() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
            {
                m_Project.EditScene().Transform(*m_GizmoEntity).Local = *m_GizmoBefore;
                m_GizmoBefore.reset();
                m_GizmoEntity.reset();
            }
            else if (!result.Active && m_GizmoBefore.has_value())
            {
                const auto entity = *m_GizmoEntity;
                const auto before = *m_GizmoBefore;
                const auto after = m_Project.Scene().Transform(entity).Local;
                m_GizmoBefore.reset();
                m_GizmoEntity.reset();
                if (before != after) RunCommand([this, entity, before, after]
                {
                    m_History.Execute(std::make_unique<SetEntityTransformCommand>(
                        m_Project, entity, before, after));
                });
            }
            return result.Active || result.Hovered;
        }

        void DrawOrientationGizmo(ImVec2 viewportMin, ImVec2 viewportSize)
        {
            constexpr float button = 26.0f;
            ImGui::SetCursorScreenPos({ viewportMin.x + viewportSize.x - button * 3.0f - 16.0f,
                viewportMin.y + 12.0f });
            ImGui::PushID("ViewportOrientation");
            if (ImGui::Button("X", { button, button })) m_ViewportController.SnapToAxis(ViewportAxis::Right);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right view");
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button("Y", { button, button })) m_ViewportController.SnapToAxis(ViewportAxis::Top);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Top view");
            ImGui::SameLine(0.0f, 2.0f);
            if (ImGui::Button("Z", { button, button })) m_ViewportController.SnapToAxis(ViewportAxis::Front);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Front view");
            ImGui::PopID();
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
            else if (panel == Panel::Console)
            {
                const auto& diagnostics = m_Diagnostics.Snapshot();
                if (diagnostics.empty()) ImGui::TextDisabled("No diagnostics.");
                for (std::size_t index = 0u; index < diagnostics.size(); ++index)
                {
                    const auto& diagnostic = diagnostics[index];
                    const ImVec4 color = diagnostic.Severity == DiagnosticSeverity::Error
                        ? ImVec4{ 0.95f, 0.35f, 0.35f, 1.0f }
                        : diagnostic.Severity == DiagnosticSeverity::Warning
                        ? ImVec4{ 0.95f, 0.72f, 0.25f, 1.0f }
                        : ImVec4{ 0.55f, 0.75f, 0.95f, 1.0f };
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TextColored(color, "%s", diagnostic.Code.c_str());
                    ImGui::SameLine();
                    if (ImGui::Selectable(diagnostic.Message.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns)) NavigateDiagnostic(diagnostic.Target);
                    ImGui::PopID();
                }
            }
            else if (panel == Panel::PhysicsDebug) DrawPhysicsDebug();
            else if (panel == Panel::ContentBrowser) DrawContentBrowser();
            else if (panel == Panel::RenderResults) DrawOfflineRenderResults();
            else if (panel == Panel::NativeGameplay) DrawNativeGameplay();
            else if (panel == Panel::AIAssistant) DrawAIAssistant();
            else ImGui::TextDisabled("No active document for this workspace.");
            ImGui::End();
        }

        void DrawOfflineRenderResults()
        {
            if (!m_OfflineRender)
            {
                ImGui::TextDisabled("Offline renderer unavailable in this build.");
                return;
            }

            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Width", &m_OfflineRenderWidth, 64, 256);
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Height", &m_OfflineRenderHeight, 64, 256);
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputInt("Passes", &m_OfflineRenderPasses, 1, 16);
            ImGui::InputText("Project output", m_OfflineRenderOutput.data(),
                m_OfflineRenderOutput.size());

            const auto status = m_OfflineRender->State().Status();
            const bool active = status == OfflineRenderWorkspaceStatus::Queued ||
                status == OfflineRenderWorkspaceStatus::Running;
            if (ActionButton("Render", UIButtonTone::Primary, !active))
                RunCommand([this]
                {
                    OfflineRenderRequest request;
                    request.JobID = m_NextOfflineRenderJob++;
                    request.Width = static_cast<std::uint32_t>(m_OfflineRenderWidth);
                    request.Height = static_cast<std::uint32_t>(m_OfflineRenderHeight);
                    request.Passes = static_cast<std::uint32_t>(m_OfflineRenderPasses);
                    request.ProjectRoot = m_Project.ProjectRoot();
                    request.RelativeOutput = m_OfflineRenderOutput.data();
                    m_OfflineRender->Submit(std::move(request));
                });
            if (active)
            {
                ImGui::SameLine();
                if (ActionButton("Cancel", UIButtonTone::Destructive))
                    RunCommand([this] { m_OfflineRender->Cancel(); });
            }

            const auto& state = m_OfflineRender->State();
            const char* statusName = "Idle";
            switch (state.Status())
            {
                case OfflineRenderWorkspaceStatus::Idle: statusName = "Idle"; break;
                case OfflineRenderWorkspaceStatus::Queued: statusName = "Queued"; break;
                case OfflineRenderWorkspaceStatus::Running: statusName = "Rendering"; break;
                case OfflineRenderWorkspaceStatus::Completed: statusName = "Completed"; break;
                case OfflineRenderWorkspaceStatus::Cancelled: statusName = "Cancelled"; break;
                case OfflineRenderWorkspaceStatus::Failed: statusName = "Failed"; break;
            }
            const float progress = static_cast<float>(state.Progress());
            ImGui::ProgressBar(progress, { -1.0f, 0.0f }, statusName);
            if (state.Output().has_value())
            {
                ImGui::SeparatorText("Latest result");
                ImGui::TextWrapped("%s", state.Output()->generic_string().c_str());
                ImGui::TextDisabled("Metadata: %s.kairo-render",
                    state.Output()->generic_string().c_str());
            }
            for (const auto& diagnostic : state.Diagnostics())
                ImGui::TextWrapped("%s", diagnostic.c_str());
        }

        void PersistNativeGameplay()
        {
            if (!m_NativeGameplay) return;
            m_NativeGameplay->Save(m_NativeGameplayPath);
        }

        void DrawNativeGameplay()
        {
            if (!m_NativeGameplay)
            {
                ImGui::TextDisabled("Native gameplay registry unavailable in this host.");
                return;
            }
            const auto selected = m_State.SelectedEntity();
            if (!selected.has_value())
            {
                ImGui::TextDisabled("Select an entity to author native gameplay.");
                return;
            }

            const auto types = m_NativeGameplay->AvailableTypes();
            if (!types.empty())
            {
                m_NativeGameplayType = std::clamp(m_NativeGameplayType, 0,
                    static_cast<int>(types.size() - 1u));
                if (ImGui::BeginCombo("Type", types[static_cast<std::size_t>(m_NativeGameplayType)].TypeName.c_str()))
                {
                    for (std::size_t index = 0u; index < types.size(); ++index)
                        if (ImGui::Selectable(types[index].TypeName.c_str(),
                            static_cast<int>(index) == m_NativeGameplayType))
                            m_NativeGameplayType = static_cast<int>(index);
                    ImGui::EndCombo();
                }
                if (ActionButton("Attach", UIButtonTone::Primary))
                    RunCommand([this, entity = *selected, type =
                        types[static_cast<std::size_t>(m_NativeGameplayType)].TypeName]
                    {
                        m_NativeGameplay->Attach(entity, type);
                        PersistNativeGameplay();
                    });
            }
            else ImGui::TextDisabled("No native gameplay types are linked into this Editor host.");

            for (const auto& section : m_NativeGameplay->Inspect(*selected))
            {
                ImGui::PushID(section.TypeName.c_str());
                if (ImGui::CollapsingHeader(section.TypeName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    bool enabled = section.Enabled;
                    if (ImGui::Checkbox("Enabled", &enabled)) RunCommand([this, entity = *selected,
                        type = section.TypeName, enabled]
                    {
                        m_NativeGameplay->SetEnabled(entity, type, enabled);
                        PersistNativeGameplay();
                    });
                    for (const auto& property : section.Properties)
                    {
                        if (!property.Exposed) continue;
                        kairo::engine::NativeGameplayValue value = property.Value;
                        bool changed = false;
                        if (auto* flag = std::get_if<bool>(&value))
                            changed = ImGui::Checkbox(property.Name.c_str(), flag);
                        else if (auto* number = std::get_if<double>(&value))
                        {
                            const double minimum = property.Minimum.value_or(0.0);
                            const double maximum = property.Maximum.value_or(0.0);
                            changed = ImGui::InputScalar(property.Name.c_str(), ImGuiDataType_Double,
                                number, nullptr, nullptr, "%.6f",
                                property.Minimum.has_value() && property.Maximum.has_value()
                                    ? ImGuiInputTextFlags_CharsDecimal : 0);
                            if (changed && property.Minimum.has_value() && property.Maximum.has_value())
                                *number = std::clamp(*number, minimum, maximum);
                        }
                        else if (auto* vector = std::get_if<kairo::foundation::math::Vec3d>(&value))
                            changed = ImGui::InputScalarN(property.Name.c_str(), ImGuiDataType_Double,
                                &vector->x, 3);
                        else if (auto* entity = std::get_if<kairo::engine::Entity>(&value))
                        {
                            std::uint32_t id = entity->Value;
                            changed = ImGui::InputScalar(property.Name.c_str(), ImGuiDataType_U32, &id);
                            if (changed) *entity = kairo::engine::Entity{ id };
                        }
                        else if (auto* text = std::get_if<std::string>(&value))
                        {
                            std::array<char, 256> buffer{};
                            std::snprintf(buffer.data(), buffer.size(), "%s", text->c_str());
                            changed = ImGui::InputText(property.Name.c_str(), buffer.data(), buffer.size());
                            if (changed) *text = buffer.data();
                        }
                        if (changed) RunCommand([this, entity = *selected, type = section.TypeName,
                            name = property.Name, value = std::move(value)]() mutable
                        {
                            m_NativeGameplay->SetProperty(entity, type, std::move(name), std::move(value));
                            PersistNativeGameplay();
                        });
                    }
                    if (ActionButton("Remove", UIButtonTone::Destructive))
                        RunCommand([this, entity = *selected, type = section.TypeName]
                        {
                            m_NativeGameplay->Remove(entity, type);
                            PersistNativeGameplay();
                        });
                }
                ImGui::PopID();
            }
        }

        void DrawAIAssistant()
        {
            if (!m_AISession)
            {
                ImGui::TextDisabled("AI provider unavailable");
                ImGui::TextWrapped("Enable the AI cloud provider build option and set "
                    "KAIRO_AI_API_KEY plus KAIRO_AI_MODEL in the editor process environment.");
                return;
            }

            const auto drawMode = [this](const char* label, kairo::ai::InteractionMode mode)
            {
                const bool active = m_AISession->Mode() == mode;
                if (active) ImGui::BeginDisabled();
                if (ImGui::Button(label) && !m_AISession->Busy())
                    RunCommand([this, mode] { m_AISession->SetMode(mode); });
                if (active) ImGui::EndDisabled();
            };
            drawMode("Ask", kairo::ai::InteractionMode::Ask);
            ImGui::SameLine();
            drawMode("Plan", kairo::ai::InteractionMode::Plan);
            ImGui::SameLine();
            drawMode("Agent", kairo::ai::InteractionMode::Agent);

            const float composerHeight = 112.0f;
            if (ImGui::BeginChild("##AIConversation", { 0.0f,
                std::max(80.0f, ImGui::GetContentRegionAvail().y - composerHeight) },
                ImGuiChildFlags_Borders))
            {
                for (const AIConversationEntry& entry : m_AISession->Conversation())
                {
                    ImGui::TextDisabled("%s", entry.Role == kairo::ai::MessageRole::User
                        ? "You" : "Kairo AI");
                    ImGui::TextWrapped("%s", entry.Text.c_str());
                    ImGui::Spacing();
                }
                if (m_AISession->Busy())
                {
                    const std::string streamed = m_AISession->StreamedText();
                    ImGui::TextDisabled("Kairo AI");
                    if (streamed.empty()) ImGui::TextDisabled("Working...");
                    else ImGui::TextWrapped("%s", streamed.c_str());
                }
                for (const AIPendingEditorCall& pending : m_AISession->PendingCalls())
                {
                    ImGui::PushID(pending.Call.ID.c_str());
                    ImGui::SeparatorText(pending.Call.Name.c_str());
                    ImGui::TextWrapped("%s", pending.Preview.Summary.c_str());
                    if (!pending.Resolved)
                    {
                        if (ActionButton("Approve", UIButtonTone::Primary))
                            RunCommand([this, id = pending.Call.ID] { (void)m_AISession->Approve(id); });
                        ImGui::SameLine();
                        if (ActionButton("Reject", UIButtonTone::Destructive))
                            RunCommand([this, id = pending.Call.ID] { (void)m_AISession->Reject(id); });
                    }
                    else ImGui::TextDisabled("%s", pending.Result.c_str());
                    ImGui::PopID();
                }
                if (!m_AISession->LastError().empty())
                {
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", m_AISession->LastError().data());
                }
            }
            ImGui::EndChild();

            ImGui::InputTextMultiline("##AIPrompt", m_AIPrompt.data(), m_AIPrompt.size(),
                { -1.0f, 58.0f });
            const bool canSend = !m_AISession->Busy() && m_AIPrompt[0] != '\0';
            if (!canSend) ImGui::BeginDisabled();
            if (ActionButton("Send", UIButtonTone::Primary))
            {
                const std::string prompt(m_AIPrompt.data());
                RunCommand([this, prompt] { m_AISession->Submit(prompt); });
                m_AIPrompt.fill('\0');
            }
            if (!canSend) ImGui::EndDisabled();
            if (m_AISession->Busy())
            {
                ImGui::SameLine();
                if (ActionButton("Cancel", UIButtonTone::Destructive)) m_AISession->Cancel();
            }
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
            const auto statusName = [](AssetWorkspaceStatus status) -> const char*
            {
                switch (status)
                {
                    case AssetWorkspaceStatus::Current: return "Current";
                    case AssetWorkspaceStatus::Changed: return "Changed";
                    case AssetWorkspaceStatus::MissingSource: return "Missing";
                    case AssetWorkspaceStatus::Generated: return "Generated";
                    case AssetWorkspaceStatus::Builtin: return "Builtin";
                    case AssetWorkspaceStatus::Unknown: return "Untracked";
                }
                return "Unknown";
            };
            const auto previewName = [](kairo::assets::AssetType type) -> const char*
            {
                using kairo::assets::AssetType;
                switch (type)
                {
                    case AssetType::Mesh: return "[MESH]";
                    case AssetType::Material: return "[MAT]";
                    case AssetType::Texture2D: return "[TEX]";
                    case AssetType::Scene: return "[SCN]";
                    case AssetType::Shader: return "[SHD]";
                    case AssetType::Audio: return "[AUD]";
                    case AssetType::Script: return "[SCR]";
                    case AssetType::Document: return "[DOC]";
                    case AssetType::Other: return "[---]";
                }
                return "[---]";
            };

            std::size_t visible = 0u;
            if (ImGui::BeginTable("AssetRegistry", 5,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 82.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 76.0f);
                ImGui::TableSetupColumn("Path");
                ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthFixed, 72.0f);
                ImGui::TableHeadersRow();
                for (const auto& asset : records)
                {
                    const std::string path = asset.Path.generic_string();
                    const std::string type(kairo::assets::NameOfAssetType(asset.Type));
                    if (!filter.empty() && Lower(path).find(filter) == std::string::npos &&
                        Lower(type).find(filter) == std::string::npos &&
                        Lower(asset.Importer).find(filter) == std::string::npos) continue;

                    AssetWorkspaceStatus status = asset.Origin == kairo::assets::AssetOrigin::Generated
                        ? AssetWorkspaceStatus::Generated
                        : asset.Origin == kairo::assets::AssetOrigin::Builtin
                        ? AssetWorkspaceStatus::Builtin : AssetWorkspaceStatus::Unknown;
                    if (m_AssetImports != nullptr)
                        status = StatusForAsset(m_Project.ProjectRoot(), asset, *m_AssetImports);
                    const AssetDeletePlan deletePlan = PlanAssetDeletion(m_Project.Assets(), asset.ID);
                    ++visible;
                    m_AssetThumbnailScheduler.Request(asset);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", previewName(asset.Type));
                    if (SupportsAssetThumbnail(asset.Type) && ImGui::IsItemHovered())
                        ImGui::SetTooltip("Thumbnail requested for revision %llu",
                            static_cast<unsigned long long>(asset.Revision));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(statusName(status));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(type.c_str());
                    ImGui::TableSetColumnIndex(3);
                    const std::string row = path + "##" + asset.ID.ToString();
                    ImGui::Selectable(row.c_str(), false,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                    if (asset.Type == kairo::assets::AssetType::Document &&
                        ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        OpenDocument(asset.Path);
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        const AssetDragPayload payload = AssetDragPayload::FromAsset(asset.ID);
                        ImGui::SetDragDropPayload("KAIRO_ASSET_ID",
                            payload.Bytes.data(), payload.Bytes.size());
                        ImGui::TextUnformatted(path.c_str());
                        ImGui::TextDisabled("%s | %s", type.c_str(), asset.ID.ToString().c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginPopupContextItem())
                    {
                        ImGui::TextUnformatted(path.c_str());
                        ImGui::TextDisabled("%s", asset.ID.ToString().c_str());
                        ImGui::Separator();
                        ImGui::Text("Importer: %s", asset.Importer.c_str());
                        ImGui::Text("Origin: %s", kairo::assets::NameOfAssetOrigin(asset.Origin).data());
                        ImGui::Text("Revision: %llu", static_cast<unsigned long long>(asset.Revision));
                        if (m_AssetImports != nullptr)
                        {
                            if (const auto record = m_AssetImports->Find(asset.ID); record.has_value())
                            {
                                ImGui::Text("Importer version: %s", record->ImporterVersion.c_str());
                                if (!record->CanonicalSettings.empty())
                                    ImGui::TextWrapped("Settings: %s", record->CanonicalSettings.c_str());
                            }
                        }
                        ImGui::Separator();
                        if (asset.Type == kairo::assets::AssetType::Document && ImGui::MenuItem("Open"))
                            OpenDocument(asset.Path);
                        if (asset.Origin == kairo::assets::AssetOrigin::SourceFile)
                        {
                            const bool canReimport = status != AssetWorkspaceStatus::MissingSource;
                            ImGui::BeginDisabled(!canReimport);
                            if (ImGui::MenuItem("Reimport"))
                                m_AssetBrowserRequests.Push({ AssetBrowserRequestKind::Reimport, asset.ID });
                            ImGui::EndDisabled();
                            if (!canReimport)
                                ImGui::TextDisabled("Source file is missing; reimport is unavailable.");
                        }
                        ImGui::Separator();
                        if (deletePlan.DirectDependents.empty())
                            ImGui::TextDisabled("No registered asset depends on this asset.");
                        else
                        {
                            ImGui::TextDisabled("Delete blocked by %zu direct dependent%s:",
                                deletePlan.DirectDependents.size(),
                                deletePlan.DirectDependents.size() == 1u ? "" : "s");
                            for (const auto& dependent : deletePlan.DirectDependents)
                                ImGui::BulletText("%s", dependent.Path.generic_string().c_str());
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s\n%s", path.c_str(),
                        asset.ID.ToString().c_str(), kairo::assets::NameOfAssetOrigin(asset.Origin).data(),
                        statusName(status));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("r%llu", static_cast<unsigned long long>(asset.Revision));
                    if (!deletePlan.DirectDependents.empty())
                        ImGui::TextDisabled("%zu deps", deletePlan.DirectDependents.size());
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%zu of %zu assets | drag mesh/scene assets into the viewport",
                visible, records.size());
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

        [[nodiscard]] std::vector<RecoveryDocumentDraft> RecoveryDrafts() const
        {
            std::vector<RecoveryDocumentDraft> drafts;
            for (const auto id : m_AuthoringWorkspace.DocumentIDs())
            {
                const auto& view = m_AuthoringWorkspace.At(id);
                if (!view.IsTextDirty()) continue;
                drafts.push_back({ id, m_Project.Documents().RelativePath(id),
                    view.TextDraft(), m_Project.Documents().ActiveID() == id });
            }
            return drafts;
        }

        void CreateRecoveryNow()
        {
            const RecoverySnapshot snapshot = m_Project.CreateRecoveryPoint(RecoveryDrafts());
            m_RecoveryStatus = "Recovery current";
            m_NextAutosave = std::chrono::steady_clock::now() + AutosaveInterval;
            (void)snapshot;
        }

        void RunAutosaveIfDue() noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            if (now < m_NextAutosave) return;
            m_NextAutosave = now + AutosaveInterval;
            if (!m_Project.HasUnsavedChanges() && !m_AuthoringWorkspace.HasDirtyTextDrafts()) return;
            try
            {
                (void)m_Project.CreateRecoveryPoint(RecoveryDrafts());
                m_RecoveryStatus = "Autosave recovery current";
            }
            catch (const std::exception& error)
            {
                m_RecoveryStatus = "Autosave failed";
                m_LastError = std::string("Autosave recovery failed: ") + error.what();
            }
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

        void NavigateDiagnostic(const DiagnosticTarget& target)
        {
            if (const auto* entity = std::get_if<EntityDiagnosticTarget>(&target))
            {
                if (m_Project.Scene().Contains(entity->Entity)) m_State.Select(entity->Entity);
            }
            else if (const auto* asset = std::get_if<AssetDiagnosticTarget>(&target))
            {
                const auto metadata = m_Project.Assets().Find(asset->Asset);
                if (metadata.has_value() && metadata->Type == kairo::assets::AssetType::Document)
                    OpenDocument(metadata->Path);
            }
            else if (const auto* graph = std::get_if<GraphDiagnosticTarget>(&target))
            {
                if (m_Project.Documents().Contains(graph->Document))
                {
                    m_Project.ActivateDocument(graph->Document);
                    m_State.SwitchWorkspace(Workspace::Logic);
                }
            }
        }

        template<class Command>
        void RunCommand(Command&& command) noexcept
        {
            try { std::forward<Command>(command)(); }
            catch (const std::exception& error)
            {
                m_LastError = error.what();
                m_Diagnostics.ReplaceProducer("editor", { {
                    "editor", "EDITOR_OPERATION", DiagnosticSeverity::Error,
                    m_LastError, std::monostate{} } });
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
