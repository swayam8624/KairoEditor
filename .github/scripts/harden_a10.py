from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        raise SystemExit(f"{label} marker not found")
    return text.replace(old, new, 1)


shell = Path("EditorShell.cppm")
text = shell.read_text()

text = replace_once(
    text,
    "        AssetThumbnailScheduler m_AssetThumbnailScheduler;\n        AssetBrowserRequestQueue m_AssetBrowserRequests;\n",
    "        AssetThumbnailScheduler m_AssetThumbnailScheduler;\n"
    "        AssetBrowserRequestQueue m_AssetBrowserRequests;\n"
    "        std::optional<AssetWorkspace> m_AssetWorkspaceSnapshot;\n"
    "        bool m_RefreshAssetWorkspace = true;\n",
    "asset workspace cache fields",
)

start = text.find("        void DrawContentBrowser()\n        {\n")
end_marker = "        [[nodiscard]] std::optional<kairo::assets::AssetID> DrawDocumentTabs(Panel panel)\n"
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("DrawContentBrowser boundaries not found")

browser = r'''        void DrawContentBrowser()
        {
            (void)SearchField("##AssetFilter", "Filter assets", m_AssetFilter.data(), m_AssetFilter.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Refresh")) m_RefreshAssetWorkspace = true;

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

            // Fingerprinting is deliberately snapshot-driven rather than a
            // per-frame operation. ImportDatabase::Evaluate hashes source bytes;
            // doing that for every row every ImGui frame would make browser cost
            // scale with total asset bytes instead of visible UI work.
            if (m_AssetImports != nullptr)
            {
                bool stale = m_RefreshAssetWorkspace || !m_AssetWorkspaceSnapshot.has_value();
                if (!stale)
                {
                    const auto& entries = m_AssetWorkspaceSnapshot->Entries();
                    stale = entries.size() != records.size();
                    if (!stale)
                    {
                        for (const auto& asset : records)
                        {
                            const auto found = std::ranges::find(entries, asset.ID,
                                [](const AssetWorkspaceEntry& entry) { return entry.Metadata.ID; });
                            if (found == entries.end() || found->Metadata.Revision != asset.Revision)
                            {
                                stale = true;
                                break;
                            }
                        }
                    }
                }
                if (stale)
                {
                    m_AssetWorkspaceSnapshot = AssetWorkspace::Build(
                        m_Project.ProjectRoot(), m_Project.Assets(), *m_AssetImports);
                    m_RefreshAssetWorkspace = false;
                }
            }
            else m_AssetWorkspaceSnapshot.reset();

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
                    std::optional<kairo::assets::ImportRecord> importRecord;
                    std::vector<kairo::assets::AssetReference> blockers;
                    if (m_AssetWorkspaceSnapshot.has_value())
                    {
                        const auto& entry = m_AssetWorkspaceSnapshot->At(asset.ID);
                        status = entry.Status;
                        importRecord = entry.Import;
                        blockers = entry.Dependents;
                    }
                    else
                    {
                        const auto deletePlan = PlanAssetDeletion(m_Project.Assets(), asset.ID);
                        blockers.reserve(deletePlan.DirectDependents.size());
                        for (const auto& dependent : deletePlan.DirectDependents)
                            blockers.push_back({ dependent.ID, dependent.Type });
                    }
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
                        if (importRecord.has_value())
                        {
                            ImGui::Text("Importer version: %s", importRecord->ImporterVersion.c_str());
                            if (!importRecord->CanonicalSettings.empty())
                                ImGui::TextWrapped("Settings: %s", importRecord->CanonicalSettings.c_str());
                        }
                        ImGui::Separator();
                        if (asset.Type == kairo::assets::AssetType::Document && ImGui::MenuItem("Open"))
                            OpenDocument(asset.Path);
                        if (asset.Origin == kairo::assets::AssetOrigin::SourceFile)
                        {
                            const bool sourceAvailable = status != AssetWorkspaceStatus::MissingSource;
                            const bool cleanProject = !m_Project.HasUnsavedChanges();
                            const bool canReimport = sourceAvailable && cleanProject;
                            ImGui::BeginDisabled(!canReimport);
                            if (ImGui::MenuItem("Reimport"))
                                m_AssetBrowserRequests.Push({ AssetBrowserRequestKind::Reimport, asset.ID });
                            ImGui::EndDisabled();
                            if (!sourceAvailable)
                                ImGui::TextDisabled("Source file is missing; reimport is unavailable.");
                            else if (!cleanProject)
                                ImGui::TextDisabled("Save all project changes before reimporting.");
                        }
                        ImGui::Separator();
                        if (blockers.empty())
                            ImGui::TextDisabled("No registered asset depends on this asset.");
                        else
                        {
                            ImGui::TextDisabled("Delete blocked by %zu direct dependent%s:",
                                blockers.size(), blockers.size() == 1u ? "" : "s");
                            for (const auto& dependent : blockers)
                            {
                                const auto metadata = m_Project.Assets().At(dependent.ID);
                                ImGui::BulletText("%s", metadata.Path.generic_string().c_str());
                            }
                        }
                        ImGui::EndPopup();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s\n%s\n%s", path.c_str(),
                        asset.ID.ToString().c_str(), kairo::assets::NameOfAssetOrigin(asset.Origin).data(),
                        statusName(status));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("r%llu", static_cast<unsigned long long>(asset.Revision));
                    if (!blockers.empty()) ImGui::TextDisabled("%zu deps", blockers.size());
                }
                ImGui::EndTable();
            }
            ImGui::TextDisabled("%zu of %zu assets | drag mesh/scene assets into the viewport",
                visible, records.size());
        }

'''
text = text[:start] + browser + text[end:]
shell.write_text(text)

app = Path("examples/editor_main.cpp")
text = app.read_text()
text = replace_once(
    text,
    "#if !defined(_WIN32)\n#include <unistd.h>\n#endif\n",
    "#if defined(_WIN32)\n#include <process.h>\n#else\n#include <unistd.h>\n#endif\n",
    "platform process include",
)
old_transition = '''        if (projectTransition.has_value())
        {
#if defined(_WIN32)
            throw std::runtime_error("Project switching requires restarting KairoEditorApp on this platform build.");
#else
            const std::string executable = std::filesystem::absolute(argv[0]).string();
            const std::string projectPath = projectTransition->string();
            execl(executable.c_str(), executable.c_str(), "--project", projectPath.c_str(),
                static_cast<char*>(nullptr));
            throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#endif
        }
'''
new_transition = '''        if (projectTransition.has_value())
        {
            const std::string executable = std::filesystem::absolute(argv[0]).string();
            const std::string projectPath = projectTransition->string();
#if defined(_WIN32)
            const intptr_t result = _spawnl(_P_OVERLAY, executable.c_str(), executable.c_str(),
                "--project", projectPath.c_str(), static_cast<char*>(nullptr));
            if (result == -1)
                throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#else
            execl(executable.c_str(), executable.c_str(), "--project", projectPath.c_str(),
                static_cast<char*>(nullptr));
            throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#endif
        }
'''
text = replace_once(text, old_transition, new_transition, "cross-platform project restart")
app.write_text(text)

Path(".github/workflows/harden-a10.yml").unlink(missing_ok=True)
Path(".github/scripts/harden_a10.py").unlink(missing_ok=True)
