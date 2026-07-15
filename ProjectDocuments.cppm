module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.ProjectDocuments;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.ProjectPaths;
import Kairo.Editor.Types;

export namespace kairo::editor
{
    inline constexpr std::size_t MaximumOpenProjectDocuments = 256u;

    enum class ExistingDocumentPolicy : std::uint8_t { Reject, Replace };

    struct ProjectDocumentInfo final
    {
        kairo::assets::AssetID ID;
        DocumentKind Kind = DocumentKind::Logic;
        std::filesystem::path RelativePath;
        std::string Name;
        bool Dirty = false;
        bool Active = false;
    };

    /// Owns the stable in-memory lifetime of all typed documents open in one
    /// project. The workspace is UI-independent: tabs, Graph, Code, Split, and
    /// automation all address the same AuthoringDocument instances.
    ///
    /// Paths are project-relative `.kdoc` files indexed with KairoAssets'
    /// portable case-folded key. Closing any document clears the shared command
    /// history before destroying objects referenced by retained commands.
    class ProjectDocuments final
    {
    public:
        ProjectDocuments() = default;
        explicit ProjectDocuments(const std::filesystem::path& projectRoot)
            : m_ProjectRoot(CanonicalProjectRoot(projectRoot)) {}

        ProjectDocuments(const ProjectDocuments&) = delete;
        ProjectDocuments& operator=(const ProjectDocuments&) = delete;

        ProjectDocuments(ProjectDocuments&& other) noexcept
            : m_ProjectRoot(std::move(other.m_ProjectRoot)), m_Documents(std::move(other.m_Documents)),
              m_PathIndex(std::move(other.m_PathIndex)), m_Active(other.m_Active),
              m_History(std::move(other.m_History))
        {
            other.m_Active.reset();
        }

        ProjectDocuments& operator=(ProjectDocuments&& other) noexcept
        {
            if (this == &other) return *this;
            m_History.Clear();
            m_Documents.clear();
            m_PathIndex.clear();
            m_ProjectRoot = std::move(other.m_ProjectRoot);
            m_Documents = std::move(other.m_Documents);
            m_PathIndex = std::move(other.m_PathIndex);
            m_Active = other.m_Active;
            m_History = std::move(other.m_History);
            other.m_Active.reset();
            return *this;
        }

        [[nodiscard]] bool IsBound() const noexcept { return !m_ProjectRoot.empty(); }
        [[nodiscard]] std::size_t Count() const noexcept { return m_Documents.size(); }
        [[nodiscard]] bool Empty() const noexcept { return m_Documents.empty(); }
        [[nodiscard]] bool HasUnsavedChanges() const noexcept
        {
            return std::ranges::any_of(m_Documents, [](const auto& entry) { return entry.second.Dirty; });
        }
        [[nodiscard]] std::optional<kairo::assets::AssetID> ActiveID() const noexcept { return m_Active; }
        [[nodiscard]] CommandHistory& History() noexcept { return m_History; }
        [[nodiscard]] const CommandHistory& History() const noexcept { return m_History; }

        /// Creates an unsaved document at a reserved project path. The caller
        /// supplies the persistent ID so ProjectSession can use the same ID in
        /// KairoAssets metadata without generating two competing identities.
        void Create(kairo::assets::AssetID id, DocumentKind kind, std::string name,
            const std::filesystem::path& projectRelativePath)
        {
            RequireBound();
            if (!id.IsValid()) throw std::invalid_argument("Project document requires a valid asset ID.");
            if (m_Documents.size() >= MaximumOpenProjectDocuments)
                throw std::length_error("Project reached its 256 open-document safety limit.");
            const auto relative = NormalizeDocumentPath(projectRelativePath);
            const std::string key = kairo::assets::PortableAssetPathKey(relative);
            if (m_Documents.contains(id)) throw std::invalid_argument("This document ID is already open.");
            if (m_PathIndex.contains(key)) throw std::invalid_argument("This document path is already open.");
            const auto output = ResolveProjectOutput(m_ProjectRoot, relative);
            std::error_code error;
            if (std::filesystem::exists(output, error))
                throw std::invalid_argument("Cannot create a document over an existing project file.");
            if (error) throw std::runtime_error("Cannot inspect document destination: " + error.message());

            InsertRecord(id, relative, AuthoringDocument(id, kind, std::move(name)), true);
            m_Active = id;
        }

        /// Loads and validates a self-describing document before observable
        /// mutation. Reopening an already-open path activates its existing
        /// object instead of producing two diverging editor tabs.
        [[nodiscard]] kairo::assets::AssetID Open(const std::filesystem::path& projectRelativePath,
            std::optional<kairo::assets::AssetID> expectedID = std::nullopt)
        {
            RequireBound();
            const auto relative = NormalizeDocumentPath(projectRelativePath);
            const std::string key = kairo::assets::PortableAssetPathKey(relative);
            if (const auto existing = m_PathIndex.find(key); existing != m_PathIndex.end())
            {
                if (expectedID.has_value() && existing->second != *expectedID)
                    throw std::invalid_argument("Open document identity does not match the expected asset ID.");
                m_Active = existing->second;
                return existing->second;
            }
            if (m_Documents.size() >= MaximumOpenProjectDocuments)
                throw std::length_error("Project reached its 256 open-document safety limit.");
            const auto source = ResolveExistingProjectFile(m_ProjectRoot, relative, "authoring document");
            AuthoringDocument candidate = LoadDocument(source);
            if (expectedID.has_value() && candidate.ID() != *expectedID)
                throw std::invalid_argument("Authoring document identity does not match its asset metadata.");
            if (m_Documents.contains(candidate.ID()))
                throw std::invalid_argument("This document ID is already open at another project path.");
            const auto id = candidate.ID();
            InsertRecord(id, relative, std::move(candidate), false);
            m_Active = id;
            return id;
        }

        [[nodiscard]] bool Contains(kairo::assets::AssetID id) const noexcept
        {
            return m_Documents.contains(id);
        }

        [[nodiscard]] const AuthoringDocument& Get(kairo::assets::AssetID id) const
        {
            return RequireRecord(id).Value;
        }

        /// Mutable access conservatively marks the document dirty before it is
        /// returned. Callers should obtain this reference before constructing a
        /// document command, ensuring failed or custom mutations never appear
        /// cleaner than the on-disk source.
        [[nodiscard]] AuthoringDocument& Edit(kairo::assets::AssetID id)
        {
            auto& record = RequireRecord(id);
            record.Dirty = true;
            return record.Value;
        }

        [[nodiscard]] bool IsDirty(kairo::assets::AssetID id) const { return RequireRecord(id).Dirty; }
        [[nodiscard]] const std::filesystem::path& RelativePath(kairo::assets::AssetID id) const
        {
            return RequireRecord(id).RelativePath;
        }

        void MarkDirty(kairo::assets::AssetID id) { RequireRecord(id).Dirty = true; }

        void Activate(kairo::assets::AssetID id)
        {
            (void)RequireRecord(id);
            m_Active = id;
        }

        void Save(kairo::assets::AssetID id)
        {
            auto& record = RequireRecord(id);
            SaveDocument(ResolveProjectOutput(m_ProjectRoot, record.RelativePath), record.Value);
            record.Dirty = false;
        }

        /// Writes the new file atomically, then updates the in-memory path index
        /// without changing document identity. Replace must be an explicit UI
        /// decision; the default never overwrites an unrelated project file.
        void SaveAs(kairo::assets::AssetID id, const std::filesystem::path& projectRelativePath,
            ExistingDocumentPolicy policy = ExistingDocumentPolicy::Reject)
        {
            auto& record = RequireRecord(id);
            const auto relative = NormalizeDocumentPath(projectRelativePath);
            const std::string key = kairo::assets::PortableAssetPathKey(relative);
            const std::string oldKey = kairo::assets::PortableAssetPathKey(record.RelativePath);
            if (key == oldKey)
            {
                Save(id);
                return;
            }
            if (const auto existing = m_PathIndex.find(key);
                existing != m_PathIndex.end() && existing->second != id)
                throw std::invalid_argument("Another open document already owns this path.");
            const auto output = ResolveProjectOutput(m_ProjectRoot, relative);
            std::error_code error;
            if (std::filesystem::exists(output, error) && policy == ExistingDocumentPolicy::Reject)
                throw std::invalid_argument("Document destination already exists; replacement was not authorized.");
            if (error) throw std::runtime_error("Cannot inspect document destination: " + error.message());

            // Reserve the new index before disk I/O. Failure leaves the old
            // record and path untouched; a failed save releases the reservation.
            const auto [reserved, inserted] = m_PathIndex.emplace(key, id);
            if (!inserted && reserved->second != id)
                throw std::invalid_argument("Another open document already owns this path.");
            try { SaveDocument(output, record.Value); }
            catch (...)
            {
                if (inserted) m_PathIndex.erase(reserved);
                throw;
            }
            std::filesystem::path committed = relative;
            record.RelativePath.swap(committed);
            m_PathIndex.erase(oldKey);
            record.Dirty = false;
        }

        void SaveAll()
        {
            for (auto& [id, record] : m_Documents)
                if (record.Dirty) Save(id);
        }

        void Close(kairo::assets::AssetID id,
            UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            auto found = m_Documents.find(id);
            if (found == m_Documents.end()) throw std::out_of_range("Project document is not open.");
            if (found->second.Dirty && policy == UnsavedChangesPolicy::Reject)
                throw std::logic_error("Cannot close an authoring document with unsaved changes.");
            m_History.Clear();
            m_PathIndex.erase(kairo::assets::PortableAssetPathKey(found->second.RelativePath));
            m_Documents.erase(found);
            if (m_Active == id)
                m_Active = m_Documents.empty() ? std::nullopt : std::optional(m_Documents.begin()->first);
        }

        void CloseAll(UnsavedChangesPolicy policy = UnsavedChangesPolicy::Reject)
        {
            if (HasUnsavedChanges() && policy == UnsavedChangesPolicy::Reject)
                throw std::logic_error("Cannot close project documents with unsaved changes.");
            m_History.Clear();
            m_Documents.clear();
            m_PathIndex.clear();
            m_Active.reset();
        }

        [[nodiscard]] std::vector<ProjectDocumentInfo> Snapshot() const
        {
            std::vector<ProjectDocumentInfo> result;
            result.reserve(m_Documents.size());
            for (const auto& [id, record] : m_Documents)
                result.push_back({ id, record.Value.Kind(), record.RelativePath,
                    record.Value.Name(), record.Dirty, m_Active == id });
            return result;
        }

    private:
        struct Record final
        {
            std::filesystem::path RelativePath;
            AuthoringDocument Value;
            bool Dirty = false;
        };

        std::filesystem::path m_ProjectRoot;
        std::map<kairo::assets::AssetID, Record> m_Documents;
        std::map<std::string, kairo::assets::AssetID, std::less<>> m_PathIndex;
        std::optional<kairo::assets::AssetID> m_Active;
        CommandHistory m_History;

        [[nodiscard]] static std::filesystem::path NormalizeDocumentPath(
            const std::filesystem::path& path)
        {
            const auto normalized = kairo::assets::NormalizeAssetPath(path);
            if (normalized.extension() != ".kdoc")
                throw std::invalid_argument("Authoring documents must use the .kdoc extension.");
            return normalized;
        }

        void RequireBound() const
        {
            if (!IsBound()) throw std::logic_error("Project document workspace is not bound to a project root.");
        }

        [[nodiscard]] Record& RequireRecord(kairo::assets::AssetID id)
        {
            const auto found = m_Documents.find(id);
            if (found == m_Documents.end()) throw std::out_of_range("Project document is not open.");
            return found->second;
        }

        [[nodiscard]] const Record& RequireRecord(kairo::assets::AssetID id) const
        {
            const auto found = m_Documents.find(id);
            if (found == m_Documents.end()) throw std::out_of_range("Project document is not open.");
            return found->second;
        }

        void InsertRecord(kairo::assets::AssetID id, const std::filesystem::path& relative,
            AuthoringDocument document, bool dirty)
        {
            const std::string key = kairo::assets::PortableAssetPathKey(relative);
            const auto [record, accepted] = m_Documents.emplace(id,
                Record{ relative, std::move(document), dirty });
            if (!accepted) throw std::invalid_argument("This document ID is already open.");
            try
            {
                if (!m_PathIndex.emplace(key, id).second)
                    throw std::invalid_argument("This document path is already open.");
            }
            catch (...)
            {
                m_Documents.erase(record);
                throw;
            }
        }
    };
}
