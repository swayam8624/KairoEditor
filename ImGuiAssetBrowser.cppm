module;

#include <imgui.h>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

export module Kairo.Editor.ImGuiAssetBrowser;

import Kairo.Assets;
import Kairo.Editor.AssetDragDrop;
import Kairo.Editor.AssetWorkspace;
import Kairo.Editor.AuthoringWorkspaceState;
import Kairo.Editor.ProjectSession;
import Kairo.Editor.UI;

export namespace kairo::editor
{
    /// Production Content Browser state and ImGui rendering.
    ///
    /// Asset metadata and dependency truth remain in KairoAssets. This class
    /// owns only transient UI selection/filter state and emits a document-open
    /// request back to EditorShell so workspace switching stays shell-owned.
    class ImGuiAssetBrowser final
    {
    public:
        void Draw(ProjectSession& project, AuthoringWorkspaceState& authoringWorkspace)
        {
            using kairo::assets::AssetOrigin;
            using kairo::assets::AssetType;

            if (!project.HasProject())
            {
                ImGui::TextDisabled("Open a project to browse assets.");
                return;
            }

            (void)SearchField("##AssetFilter", "Filter assets", m_Search.data(), m_Search.size());
            DrawTypeFilter();
            ImGui::SameLine();
            DrawStatusFilter();

            // ProjectSession does not yet persist KairoAssets ImportDatabase
            // provenance. Until that storage boundary exists, source-file
            // records are truthfully shown as Untracked instead of synthesizing
            // a successful fingerprint from current disk contents.
            const AssetWorkspace workspace = AssetWorkspace::Build(
                project.ProjectRoot(), project.Assets(), m_ImportProvenance);
            const auto records = workspace.Filter(BuildFilter());
            if (m_Selected.has_value() && !project.Assets().Contains(*m_Selected))
                m_Selected.reset();

            DrawTable(project, records);
            ImGui::TextDisabled("%zu of %zu assets", records.size(), workspace.Entries().size());
            if (m_Selected.has_value() && project.Assets().Contains(*m_Selected))
                DrawDetails(project, authoringWorkspace, workspace.At(*m_Selected));
            DrawRemovalPopup(project, authoringWorkspace);
        }

        [[nodiscard]] std::optional<std::filesystem::path> TakeOpenDocumentRequest() noexcept
        {
            return std::exchange(m_OpenDocumentRequest, std::nullopt);
        }

        [[nodiscard]] std::optional<std::string> TakeError() noexcept
        {
            return std::exchange(m_Error, std::nullopt);
        }

    private:
        std::array<char, 256> m_Search{};
        kairo::assets::ImportDatabase m_ImportProvenance;
        std::optional<kairo::assets::AssetID> m_Selected;
        int m_TypeFilter = 0;
        int m_StatusFilter = 0;
        bool m_RequestRemovalPopup = false;
        std::optional<kairo::assets::AssetID> m_PendingRemoval;
        std::optional<std::filesystem::path> m_OpenDocumentRequest;
        std::optional<std::string> m_Error;

        static constexpr std::array AssetTypes{
            kairo::assets::AssetType::Mesh,
            kairo::assets::AssetType::Material,
            kairo::assets::AssetType::Texture2D,
            kairo::assets::AssetType::Scene,
            kairo::assets::AssetType::Shader,
            kairo::assets::AssetType::Audio,
            kairo::assets::AssetType::Script,
            kairo::assets::AssetType::Document,
            kairo::assets::AssetType::Other
        };

        static constexpr std::array Statuses{
            AssetWorkspaceStatus::Current,
            AssetWorkspaceStatus::Changed,
            AssetWorkspaceStatus::MissingSource,
            AssetWorkspaceStatus::Generated,
            AssetWorkspaceStatus::Builtin,
            AssetWorkspaceStatus::Unknown
        };

        void DrawTypeFilter()
        {
            const char* preview = "All Types";
            std::string storage;
            if (m_TypeFilter > 0)
            {
                storage = std::string(kairo::assets::NameOfAssetType(
                    AssetTypes.at(static_cast<std::size_t>(m_TypeFilter - 1))));
                preview = storage.c_str();
            }
            ImGui::SetNextItemWidth(150.0f);
            if (!ImGui::BeginCombo("Type", preview)) return;
            if (ImGui::Selectable("All Types", m_TypeFilter == 0)) m_TypeFilter = 0;
            for (std::size_t index = 0u; index < AssetTypes.size(); ++index)
            {
                const std::string label(kairo::assets::NameOfAssetType(AssetTypes[index]));
                if (ImGui::Selectable(label.c_str(),
                    m_TypeFilter == static_cast<int>(index + 1u)))
                    m_TypeFilter = static_cast<int>(index + 1u);
            }
            ImGui::EndCombo();
        }

        void DrawStatusFilter()
        {
            const char* preview = m_StatusFilter == 0 ? "All Statuses" :
                NameOfAssetWorkspaceStatus(Statuses.at(
                    static_cast<std::size_t>(m_StatusFilter - 1))).data();
            ImGui::SetNextItemWidth(150.0f);
            if (!ImGui::BeginCombo("Status", preview)) return;
            if (ImGui::Selectable("All Statuses", m_StatusFilter == 0)) m_StatusFilter = 0;
            for (std::size_t index = 0u; index < Statuses.size(); ++index)
                if (ImGui::Selectable(NameOfAssetWorkspaceStatus(Statuses[index]).data(),
                    m_StatusFilter == static_cast<int>(index + 1u)))
                    m_StatusFilter = static_cast<int>(index + 1u);
            ImGui::EndCombo();
        }

        [[nodiscard]] AssetWorkspaceFilter BuildFilter() const
        {
            AssetWorkspaceFilter filter;
            filter.Search = m_Search.data();
            if (m_TypeFilter > 0)
                filter.Type = AssetTypes.at(static_cast<std::size_t>(m_TypeFilter - 1));
            if (m_StatusFilter == 0) return filter;

            filter.IncludeCurrent = false;
            filter.IncludeChanged = false;
            filter.IncludeMissing = false;
            filter.IncludeGenerated = false;
            filter.IncludeBuiltin = false;
            filter.IncludeUnknown = false;
            switch (Statuses.at(static_cast<std::size_t>(m_StatusFilter - 1)))
            {
                case AssetWorkspaceStatus::Current: filter.IncludeCurrent = true; break;
                case AssetWorkspaceStatus::Changed: filter.IncludeChanged = true; break;
                case AssetWorkspaceStatus::MissingSource: filter.IncludeMissing = true; break;
                case AssetWorkspaceStatus::Generated: filter.IncludeGenerated = true; break;
                case AssetWorkspaceStatus::Builtin: filter.IncludeBuiltin = true; break;
                case AssetWorkspaceStatus::Unknown: filter.IncludeUnknown = true; break;
            }
            return filter;
        }

        void DrawTable(ProjectSession& project,
            const std::vector<AssetWorkspaceEntry>& records)
        {
            using kairo::assets::AssetType;
            if (!ImGui::BeginTable("AssetRegistry", 5,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp, { 0.0f, 280.0f })) return;

            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 84.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 86.0f);
            ImGui::TableSetupColumn("Path");
            ImGui::TableSetupColumn("Rev", ImGuiTableColumnFlags_WidthFixed, 42.0f);
            ImGui::TableSetupColumn("Uses", ImGuiTableColumnFlags_WidthFixed, 44.0f);
            ImGui::TableHeadersRow();
            for (const AssetWorkspaceEntry& entry : records)
            {
                const auto& asset = entry.Metadata;
                const std::string path = asset.Path.generic_string();
                const std::string type(kairo::assets::NameOfAssetType(asset.Type));
                const bool selected = m_Selected.has_value() && *m_Selected == asset.ID;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(NameOfAssetWorkspaceStatus(entry.Status).data());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(type.c_str());
                ImGui::TableSetColumnIndex(2);
                const std::string row = path + "##asset-" + asset.ID.ToString();
                if (ImGui::Selectable(row.c_str(), selected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    m_Selected = asset.ID;
                if (ImGui::IsItemHovered() &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                    asset.Type == AssetType::Document)
                    m_OpenDocumentRequest = asset.Path;
                if (ImGui::BeginDragDropSource())
                {
                    const AssetDragPayload payload = MakeAssetDragPayload(asset.ID, asset.Type);
                    ImGui::SetDragDropPayload(AssetDragDropPayloadType.data(), &payload, sizeof(payload));
                    ImGui::TextUnformatted(path.c_str());
                    ImGui::TextDisabled("%s", type.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s\n%s\n%s", path.c_str(), asset.ID.ToString().c_str(),
                        kairo::assets::NameOfAssetOrigin(asset.Origin).data());
                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%llu", static_cast<unsigned long long>(asset.Revision));
                ImGui::TableSetColumnIndex(4);
                ImGui::Text("%zu", entry.Dependents.size());
            }
            ImGui::EndTable();
        }

        void DrawDetails(ProjectSession& project, AuthoringWorkspaceState& authoringWorkspace,
            const AssetWorkspaceEntry& selected)
        {
            using kairo::assets::AssetOrigin;
            using kairo::assets::AssetType;
            const auto& metadata = selected.Metadata;
            ImGui::SeparatorText("Asset Details");
            ImGui::Text("%s", metadata.Path.generic_string().c_str());
            ImGui::TextDisabled("ID  %s", metadata.ID.ToString().c_str());
            ImGui::TextDisabled("Type %s   Origin %s   Status %s   Revision %llu",
                kairo::assets::NameOfAssetType(metadata.Type).data(),
                kairo::assets::NameOfAssetOrigin(metadata.Origin).data(),
                NameOfAssetWorkspaceStatus(selected.Status).data(),
                static_cast<unsigned long long>(metadata.Revision));
            ImGui::TextDisabled("Importer %s", metadata.Importer.c_str());

            if (!metadata.Dependencies.empty())
            {
                ImGui::TextUnformatted("Dependencies");
                for (const auto& dependency : metadata.Dependencies)
                {
                    const auto target = project.Assets().Find(dependency.ID);
                    const std::string label = target.has_value()
                        ? target->Path.generic_string() : dependency.ID.ToString();
                    ImGui::BulletText("%s", label.c_str());
                }
            }
            if (!selected.Dependents.empty())
            {
                ImGui::TextUnformatted("Used by");
                for (const auto& dependent : selected.Dependents)
                {
                    const auto owner = project.Assets().Find(dependent.ID);
                    const std::string label = owner.has_value()
                        ? owner->Path.generic_string() : dependent.ID.ToString();
                    ImGui::BulletText("%s", label.c_str());
                }
            }

            const bool openDirtyDocument = metadata.Type == AssetType::Document &&
                project.Documents().Contains(metadata.ID) &&
                project.Documents().IsDirty(metadata.ID);
            const bool builtin = metadata.Origin == AssetOrigin::Builtin;
            const bool canRemove = selected.CanDelete() && !builtin && !openDirtyDocument;
            if (!selected.CanDelete())
                ImGui::TextDisabled("Removal blocked by %zu dependent asset(s).", selected.Dependents.size());
            else if (builtin)
                ImGui::TextDisabled("Builtin assets cannot be removed from the project registry.");
            else if (openDirtyDocument)
                ImGui::TextDisabled("Save or close the dirty document before removing its asset metadata.");
            if (ActionButton("Remove from Project", UIButtonTone::Destructive, canRemove))
            {
                m_PendingRemoval = metadata.ID;
                m_RequestRemovalPopup = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Source files are left untouched.");
            (void)authoringWorkspace;
        }

        void DrawRemovalPopup(ProjectSession& project, AuthoringWorkspaceState& authoringWorkspace)
        {
            using kairo::assets::AssetOrigin;
            using kairo::assets::AssetType;
            if (m_RequestRemovalPopup)
            {
                ImGui::OpenPopup("Remove Asset from Project");
                m_RequestRemovalPopup = false;
            }
            if (!ImGui::BeginPopupModal("Remove Asset from Project", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) return;

            if (!m_PendingRemoval.has_value() || !project.Assets().Contains(*m_PendingRemoval))
            {
                m_PendingRemoval.reset();
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            const auto metadata = project.Assets().At(*m_PendingRemoval);
            ImGui::TextWrapped("Remove '%s' from the project asset registry?",
                metadata.Path.generic_string().c_str());
            ImGui::TextDisabled("This does not delete the source file from disk.");
            if (ActionButton("Remove", UIButtonTone::Destructive, true, 100.0f))
            {
                try
                {
                    const auto id = *m_PendingRemoval;
                    const AssetWorkspace latest = AssetWorkspace::Build(
                        project.ProjectRoot(), project.Assets(), m_ImportProvenance);
                    if (!latest.At(id).CanDelete())
                        throw std::logic_error("Cannot remove an asset that has dependents.");
                    if (metadata.Origin == AssetOrigin::Builtin)
                        throw std::logic_error("Builtin assets cannot be removed from the project registry.");
                    const bool openDocument = metadata.Type == AssetType::Document &&
                        project.Documents().Contains(id);
                    if (openDocument && project.Documents().IsDirty(id))
                        throw std::logic_error("Cannot remove an open document with unsaved changes.");
                    if (openDocument)
                    {
                        project.CloseDocument(id);
                        authoringWorkspace.Close(id);
                    }
                    project.EditAssets().Remove(id);
                    m_Selected.reset();
                    m_PendingRemoval.reset();
                    ImGui::CloseCurrentPopup();
                }
                catch (const std::exception& error)
                {
                    m_Error = error.what();
                }
            }
            ImGui::SameLine();
            if (ActionButton("Cancel", UIButtonTone::Neutral, true, 100.0f))
            {
                m_PendingRemoval.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    };
}
