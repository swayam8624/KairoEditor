module;

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

export module Kairo.Editor.ProjectSession;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.Types;
import Kairo.Editor.ProjectDescriptor;
import Kairo.Editor.ProjectDocuments;
import Kairo.Editor.ProjectPaths;
import Kairo.EngineCore;

export namespace kairo::editor
{
    inline constexpr std::string_view DefaultProjectFileName = "Project.kproject";

    /// Owns one editor project's metadata registry and authored scene.
    ///
    /// Input: create/open/save commands using project-relative paths.
    /// Output: stable in-memory Scene and AssetRegistry objects whose addresses
    /// remain valid across project and scene loads.
    /// Task: centralize project-root containment, dirty state, strong open
    /// replacement, and disk lifecycle independently from Dear ImGui or native
    /// file dialogs. Mutable access is explicit and conservatively marks the
    /// corresponding document dirty before returning it.
    class ProjectSession final
    {
    public:
        ProjectSession() = default;
        ProjectSession(const ProjectSession&) = delete;
        ProjectSession& operator=(const ProjectSession&) = delete;
        ProjectSession(ProjectSession&&) = delete;
        ProjectSession& operator=(ProjectSession&&) = delete;

        [[nodiscard]] bool HasProject() const noexcept { return m_HasProject; }
        [[nodiscard]] bool IsSceneDirty() const noexcept { return m_SceneDirty; }
        [[nodiscard]] bool AreAssetsDirty() const noexcept { return m_AssetsDirty; }
        [[nodiscard]] bool HasUnsavedChanges() const noexcept
        {
            return m_SceneDirty || m_AssetsDirty || m_Documents.HasUnsavedChanges();
        }

        [[nodiscard]] const ProjectDescriptor& Descriptor() const { RequireProject(); return m_Descriptor; }
        [[nodiscard]] const std::filesystem::path& ProjectFile() const { RequireProject(); return m_ProjectFile; }
        [[nodiscard]] const std::filesystem::path& ProjectRoot() const { RequireProject(); return m_ProjectRoot; }
        [[nodiscard]] const std::filesystem::path& ActiveScenePath() const { RequireProject(); return m_ActiveScenePath; }

        [[nodiscard]] const kairo::engine::Scene& Scene() const { RequireProject(); return m_Scene; }
        [[nodiscard]] kairo::engine::Scene& EditScene()
        {
            RequireProject();
            m_SceneDirty = true;
            return m_Scene;
        }

        [[nodiscard]] const kairo::assets::AssetRegistry& Assets() const { RequireProject(); return m_Assets; }
        [[nodiscard]] kairo::assets::AssetRegistry& EditAssets()
        {
            RequireProject();
            m_AssetsDirty = true;
            return m_Assets;
        }

        void MarkSceneDirty() { RequireProject(); m_SceneDirty = true; }
        void MarkAssetsDirty() { RequireProject(); m_AssetsDirty = true; }

        [[nodiscard]] const ProjectDocuments& Documents() const { RequireProject(); return m_Documents; }
        [[nodiscard]] const AuthoringDocument& Document(kairo::assets::AssetID id) const
        {
            RequireProject();
            return m_Documents.Get(id);
        }
        [[nodiscard]] AuthoringDocument& EditDocument(kairo::assets::AssetID id)
        {
            RequireProject();
            return m_Documents.Edit(id);
        }
        [[nodiscard]] CommandHistory& DocumentHistory()
        {
            RequireProject();
            return m_Documents.History();
        }

        /// Creates one unsaved typed document and matching asset metadata as a
        /// single in-memory transaction. The persistent ID is shared by file,
        /// tab, compiler artifact, and registry; no path-derived identity exists.
        [[nodiscard]] kairo::assets::AssetID CreateDocument(DocumentKind kind, std::string name,
            const std::filesystem::path& projectRelativePath)
        {
            RequireProject();
            const auto relative = kairo::assets::NormalizeAssetPath(projectRelativePath);
            kairo::assets::AssetID id;
            for (std::size_t attempt = 0u; attempt < 64u; ++attempt)
            {
                id = kairo::assets::GenerateAssetID();
                if (!m_Assets.Contains(id)) break;
            }
            if (!id.IsValid() || m_Assets.Contains(id))
                throw std::runtime_error("Cannot allocate a unique authoring-document asset ID.");

            m_Documents.Create(id, kind, std::move(name), relative);
            try
            {
                m_Assets.Insert({ id, kairo::assets::AssetType::Document,
                    kairo::assets::AssetOrigin::SourceFile, relative,
                    "kairo.document-v1", 1u, {} });
            }
            catch (...)
            {
                m_Documents.Close(id, UnsavedChangesPolicy::Discard);
                throw;
            }
            m_AssetsDirty = true;
            return id;
        }

        /// Opens only registry-owned document files. Importing an unregistered
        /// file is a distinct asset-import transaction, preventing a tab from
        /// silently bypassing dependency metadata and path uniqueness.
        [[nodiscard]] kairo::assets::AssetID OpenDocument(
            const std::filesystem::path& projectRelativePath)
        {
            RequireProject();
            const auto metadata = m_Assets.FindByPath(projectRelativePath);
            if (!metadata.has_value())
                throw std::out_of_range("No asset metadata exists for this authoring-document path.");
            if (metadata->Type != kairo::assets::AssetType::Document)
                throw std::invalid_argument("The requested project asset is not an authoring document.");
            return m_Documents.Open(metadata->Path, metadata->ID);
        }

        void SaveDocument(kairo::assets::AssetID id)
        {
            RequireDocumentMetadata(id);
            m_Documents.Save(id);
        }

        void SaveDocumentAs(kairo::assets::AssetID id,
            const std::filesystem::path& projectRelativePath,
            ExistingDocumentPolicy policy = ExistingDocumentPolicy::Reject)
        {
            const auto metadata = RequireDocumentMetadata(id);
            const auto relative = kairo::assets::NormalizeAssetPath(projectRelativePath);
            if (kairo::assets::PortableAssetPathKey(relative) ==
                kairo::assets::PortableAssetPathKey(metadata.Path))
            {
                m_Documents.Save(id);
                return;
            }
            if (const auto owner = m_Assets.FindByPath(relative);
                owner.has_value() && owner->ID != id)
                throw std::invalid_argument("Another project asset already owns the document destination path.");
            if (metadata.Revision == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Document asset revision is exhausted and cannot be moved.");

            // Registry preconditions are proven before disk publication. Under
            // ProjectSession's single-writer editor contract, Move cannot then
            // fail due to an intervening metadata mutation.
            m_Documents.SaveAs(id, relative, policy);
            m_Assets.Move(id, relative);
            m_AssetsDirty = true;
        }

        void CloseDocument(kairo::assets::AssetID id,
            UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            RequireProject();
            m_Documents.Close(id, policy);
        }

        /// Task: create an empty project without exposing partially written
        /// state. Files are built inside a unique sibling staging directory and
        /// the whole directory is renamed into place only after validation.
        /// Preconditions: destination must not already exist; this function is
        /// intentionally non-destructive.
        void CreateProject(const std::filesystem::path& destination, std::string name,
            UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            RejectUnsavedReplacement(policy, "create another project");
            if (destination.empty()) throw std::invalid_argument("Project destination cannot be empty.");
            ProjectDescriptor descriptor{ std::move(name), "Assets.kassets", "Scenes/Main.kscene" };
            ValidateProjectDescriptor(descriptor);

            std::error_code error;
            const std::filesystem::path absoluteDestination = std::filesystem::absolute(destination, error).lexically_normal();
            if (error) throw std::runtime_error("Cannot resolve project destination: " + error.message());
            const auto filename = absoluteDestination.filename();
            if (filename.empty() || filename == "." || filename == "..")
                throw std::invalid_argument("Project destination must name a new directory.");
            if (std::filesystem::exists(absoluteDestination, error))
                throw std::invalid_argument("Project destination already exists.");
            if (error) throw std::runtime_error("Cannot inspect project destination: " + error.message());

            const std::filesystem::path parent = absoluteDestination.parent_path();
            std::filesystem::create_directories(parent, error);
            if (error) throw std::runtime_error("Cannot create project parent directory: " + error.message());
            const std::filesystem::path staging = parent /
                ("." + filename.string() + ".creating-" + kairo::assets::GenerateAssetID().ToString());

            try
            {
                std::filesystem::create_directories(staging, error);
                if (error) throw std::runtime_error("Cannot create project staging directory: " + error.message());
                kairo::assets::AssetRegistry emptyAssets;
                kairo::engine::Scene emptyScene;
                kairo::assets::SaveAssetManifest(staging / descriptor.AssetManifest, emptyAssets);
                kairo::engine::SaveScene(staging / descriptor.StartupScene, emptyScene, emptyAssets);
                SaveProjectDescriptor(staging / DefaultProjectFileName, descriptor);
                std::filesystem::rename(staging, absoluteDestination, error);
                if (error) throw std::runtime_error("Cannot publish completed project directory: " + error.message());
            }
            catch (...)
            {
                std::filesystem::remove_all(staging, error);
                throw;
            }

            OpenProject(absoluteDestination / DefaultProjectFileName, UnsavedChangesPolicy::Discard);
        }

        /// Task: load descriptor, registry, and startup scene into candidates,
        /// then replace the live session only after all files validate. Failure
        /// preserves the currently open project, assets, scene, and dirty flags.
        void OpenProject(const std::filesystem::path& projectFile,
            UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            RejectUnsavedReplacement(policy, "open another project");
            std::filesystem::path canonicalFile = CanonicalExistingFile(projectFile, "project descriptor");
            std::filesystem::path root = canonicalFile.parent_path();
            ProjectDescriptor descriptor = LoadProjectDescriptor(canonicalFile);
            std::filesystem::path activeScenePath = descriptor.StartupScene;
            const std::filesystem::path manifest = ResolveExistingProjectFile(root, descriptor.AssetManifest, "asset manifest");
            const std::filesystem::path scenePath = ResolveExistingProjectFile(root, descriptor.StartupScene, "startup scene");

            kairo::assets::AssetRegistry candidateAssets;
            kairo::assets::LoadAssetManifest(manifest, candidateAssets);
            kairo::engine::Scene candidateScene;
            kairo::engine::LoadScene(scenePath, candidateAssets, candidateScene);
            ProjectDocuments candidateDocuments(root);

            // Registry replacement provides the strong exception guarantee. All
            // following moves are non-throwing for these standard allocator types.
            static_assert(std::is_nothrow_move_assignable_v<kairo::engine::Scene>);
            static_assert(std::is_nothrow_move_assignable_v<ProjectDescriptor>);
            static_assert(std::is_nothrow_move_assignable_v<std::filesystem::path>);
            static_assert(std::is_nothrow_move_assignable_v<ProjectDocuments>);
            m_Assets.ReplaceAll(candidateAssets.Snapshot());
            m_Scene = std::move(candidateScene);
            m_Documents = std::move(candidateDocuments);
            m_Descriptor = std::move(descriptor);
            m_ProjectFile = std::move(canonicalFile);
            m_ProjectRoot = std::move(root);
            m_ActiveScenePath = std::move(activeScenePath);
            m_HasProject = true;
            m_SceneDirty = false;
            m_AssetsDirty = false;
        }

        /// Task: switch the authored scene while preserving the registry and
        /// ProjectSession object identity. Unsaved scene changes must be handled
        /// explicitly by the caller before switching.
        void OpenScene(const std::filesystem::path& projectRelativePath,
            UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            RequireProject();
            if (m_SceneDirty && policy == UnsavedChangesPolicy::Reject)
                throw std::logic_error("Cannot open another scene while the active scene has unsaved changes.");
            const auto relative = kairo::assets::NormalizeAssetPath(projectRelativePath);
            const auto path = ResolveExistingProjectFile(m_ProjectRoot, relative, "scene");
            kairo::engine::Scene candidate;
            kairo::engine::LoadScene(path, m_Assets, candidate);
            m_Scene = std::move(candidate);
            m_ActiveScenePath = relative;
            m_SceneDirty = false;
        }

        void SaveScene()
        {
            RequireProject();
            const auto path = ResolveProjectOutput(m_ProjectRoot, m_ActiveScenePath);
            kairo::engine::SaveScene(path, m_Scene, m_Assets);
            m_SceneDirty = false;
        }

        /// Task: save a new scene document without changing the project's startup
        /// scene. Choosing startup content is a distinct project-setting action,
        /// avoiding a misleading multi-file atomicity promise.
        void SaveSceneAs(const std::filesystem::path& projectRelativePath)
        {
            RequireProject();
            const auto relative = kairo::assets::NormalizeAssetPath(projectRelativePath);
            const auto path = ResolveProjectOutput(m_ProjectRoot, relative);
            kairo::engine::SaveScene(path, m_Scene, m_Assets);
            m_ActiveScenePath = relative;
            m_SceneDirty = false;
        }

        void SaveAssets()
        {
            RequireProject();
            const auto path = ResolveProjectOutput(m_ProjectRoot, m_Descriptor.AssetManifest);
            kairo::assets::SaveAssetManifest(path, m_Assets);
            m_AssetsDirty = false;
        }

        /// Task: save independently atomic asset and scene files after proving
        /// the in-memory scene resolves against the current registry. This does
        /// not claim cross-file crash atomicity; recovery journaling is a later
        /// project-session milestone.
        void SaveAll()
        {
            RequireProject();
            (void)kairo::engine::SerializeScene(m_Scene, m_Assets);
            m_Documents.SaveAll();
            SaveAssets();
            SaveScene();
        }

        void Close(UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            if (!m_HasProject) return;
            if (HasUnsavedChanges() && policy == UnsavedChangesPolicy::Reject)
                throw std::logic_error("Cannot close a project with unsaved changes.");
            m_Documents.CloseAll(UnsavedChangesPolicy::Discard);
            m_Assets.ReplaceAll({});
            m_Scene = {};
            m_Descriptor = {};
            m_ProjectFile.clear();
            m_ProjectRoot.clear();
            m_ActiveScenePath.clear();
            m_HasProject = false;
            m_SceneDirty = false;
            m_AssetsDirty = false;
        }

    private:
        ProjectDescriptor m_Descriptor;
        std::filesystem::path m_ProjectFile;
        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_ActiveScenePath;
        kairo::assets::AssetRegistry m_Assets;
        kairo::engine::Scene m_Scene;
        ProjectDocuments m_Documents;
        bool m_HasProject = false;
        bool m_SceneDirty = false;
        bool m_AssetsDirty = false;

        void RequireProject() const
        {
            if (!m_HasProject) throw std::logic_error("No Kairo project is open.");
        }

        void RejectUnsavedReplacement(UnsavedChangesPolicy policy, std::string_view action) const
        {
            if (HasUnsavedChanges() && policy == UnsavedChangesPolicy::Reject)
                throw std::logic_error("Cannot " + std::string(action) + " while the current project has unsaved changes.");
        }

        [[nodiscard]] kairo::assets::AssetMetadata RequireDocumentMetadata(
            kairo::assets::AssetID id) const
        {
            RequireProject();
            const auto metadata = m_Assets.Find(id);
            if (!metadata.has_value()) throw std::out_of_range("Authoring document is not registered as a project asset.");
            if (metadata->Type != kairo::assets::AssetType::Document)
                throw std::invalid_argument("Asset ID does not identify an authoring document.");
            if (!m_Documents.Contains(id)) throw std::logic_error("Authoring document asset is not open.");
            if (kairo::assets::PortableAssetPathKey(metadata->Path) !=
                kairo::assets::PortableAssetPathKey(m_Documents.RelativePath(id)))
                throw std::logic_error("Open document path disagrees with its asset metadata.");
            return *metadata;
        }

    };
}
