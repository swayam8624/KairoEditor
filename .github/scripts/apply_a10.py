from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


# Register new backend-neutral browser modules and tests.
cmake = Path("CMakeLists.txt")
text = cmake.read_text()
text = replace_once(
    text,
    "    AssetBrowserModel.cppm AssetWorkspace.cppm SceneSelection.cppm",
    "    AssetBrowserModel.cppm AssetWorkspace.cppm AssetBrowserRuntime.cppm AssetScenePlacement.cppm SceneSelection.cppm",
    "CMake browser module",
)
text = replace_once(
    text,
    "tests/EditorTests.cpp tests/AssetWorkspaceTests.cpp tests/ProductionAuthoringTests.cpp",
    "tests/EditorTests.cpp tests/AssetWorkspaceTests.cpp tests/AssetBrowserRuntimeTests.cpp tests/ProductionAuthoringTests.cpp",
    "CMake browser tests",
)
cmake.write_text(text)

umbrella = Path("KairoEditor.cppm")
text = umbrella.read_text()
text = replace_once(
    text,
    "export import Kairo.Editor.AssetWorkspace;\n",
    "export import Kairo.Editor.AssetWorkspace;\n"
    "export import Kairo.Editor.AssetBrowserRuntime;\n"
    "export import Kairo.Editor.AssetScenePlacement;\n",
    "umbrella browser exports",
)
umbrella.write_text(text)

shell = Path("EditorShell.cppm")
text = shell.read_text()

# Feed real import provenance into the shell when the native host has it.
text = replace_once(
    text,
    "            const kairo::engine::NativeGameplayRegistry* nativeGameplayRegistry = nullptr)\n",
    "            const kairo::engine::NativeGameplayRegistry* nativeGameplayRegistry = nullptr,\n"
    "            const kairo::assets::ImportDatabase* assetImports = nullptr)\n",
    "shell constructor signature",
)
text = replace_once(
    text,
    "        {\n            ValidateNavigationSettings(m_NavigationSettings);\n",
    "        {\n            ValidateNavigationSettings(m_NavigationSettings);\n"
    "            m_AssetImports = assetImports;\n",
    "shell import database assignment",
)

public_marker = "        /// Rehydrates non-authoritative text buffers after ProjectSession has\n"
public_insert = """        /// Output: visible visual assets whose revision-keyed previews are not yet
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

"""
if public_insert not in text:
    if public_marker not in text:
        raise SystemExit("shell public browser output marker not found")
    text = text.replace(public_marker, public_insert + public_marker, 1)

field_marker = "        std::array<char, 256> m_AssetFilter{};\n"
field_insert = """        std::array<char, 256> m_AssetFilter{};
        const kairo::assets::ImportDatabase* m_AssetImports = nullptr;
        AssetThumbnailScheduler m_AssetThumbnailScheduler;
        AssetBrowserRequestQueue m_AssetBrowserRequests;
"""
text = replace_once(text, field_marker, field_insert, "shell browser fields")

start = text.find("        void DrawContentBrowser()\n        {\n")
end_marker = "        [[nodiscard]] std::optional<kairo::assets::AssetID> DrawDocumentTabs(Panel panel)\n"
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("DrawContentBrowser boundaries not found")

browser = r'''        void DrawContentBrowser()
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

'''
text = text[:start] + browser + text[end:]

viewport_old = """                const bool hovered = ImGui::IsItemHovered();
                const bool navigationClick = ImGui::GetIO().KeyAlt;
"""
viewport_new = """                const bool hovered = ImGui::IsItemHovered();
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
"""
text = replace_once(text, viewport_old, viewport_new, "viewport asset drop")

helper_marker = "        void DrawViewportToolButton(EditorAction tool, const char* label)\n"
helper = """        void PlaceDroppedAsset(kairo::assets::AssetID asset)
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

"""
if helper not in text:
    if helper_marker not in text:
        raise SystemExit("viewport helper marker not found")
    text = text.replace(helper_marker, helper + helper_marker, 1)
shell.write_text(text)

# Native host supplies the live import database and honors reimport at a frame
# boundary by reopening the same project. Startup already owns the typed
# import/cache/GPU binding transaction.
app = Path("examples/editor_main.cpp")
text = app.read_text()
text = replace_once(
    text,
    "            &nativeGameplayRegistry);\n",
    "            &nativeGameplayRegistry, &meshImports);\n",
    "native shell import database",
)
request_marker = """            if (auto transition = shell.TakeProjectTransitionRequest(); transition.has_value())
            {
                projectTransition = std::move(transition);
                break;
            }
"""
request_replacement = """            bool assetReloadRequested = false;
            for (const auto& request : shell.TakeAssetBrowserRequests())
            {
                if (request.Kind != kairo::editor::AssetBrowserRequestKind::Reimport) continue;
                const auto metadata = project.Assets().At(request.Asset);
                if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile)
                    throw std::logic_error("Only source-file assets can be reimported.");
                projectTransition = project.ProjectFile();
                assetReloadRequested = true;
                break;
            }
            if (assetReloadRequested) break;
            if (auto transition = shell.TakeProjectTransitionRequest(); transition.has_value())
            {
                projectTransition = std::move(transition);
                break;
            }
"""
text = replace_once(text, request_marker, request_replacement, "native host reimport requests")
app.write_text(text)

# Self-remove the temporary patch machinery after the generated source changes
# have been committed by the workflow.
Path(".github/workflows/apply-a10-content-browser.yml").unlink(missing_ok=True)
Path(".github/scripts/apply_a10.py").unlink(missing_ok=True)
