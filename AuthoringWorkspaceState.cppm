module;

#include <algorithm>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.AuthoringWorkspaceState;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentProjection;
import Kairo.Editor.GraphCanvas;

export namespace kairo::editor
{
    inline constexpr std::size_t MaximumDocumentDraftBytes = 16u * 1024u * 1024u;

    /// Owns the transient authoring state for one open document.
    ///
    /// Input: an identity-stable AuthoringDocument owned by ProjectDocuments.
    /// Output: persistent-in-session graph navigation, selection, and an
    /// editable structured-text draft.
    /// Task: keep UI state outside the document while detecting when graph or
    /// command edits make a dirty text draft stale. A stale draft is retained
    /// for explicit user resolution; it is never silently replaced or applied.
    class DocumentViewState final
    {
    public:
        explicit DocumentViewState(const AuthoringDocument& document)
        {
            ResetText(document);
        }

        [[nodiscard]] const kairo::assets::AssetID& DocumentID() const noexcept { return m_DocumentID; }
        [[nodiscard]] GraphViewport& Viewport() noexcept { return m_Viewport; }
        [[nodiscard]] const GraphViewport& Viewport() const noexcept { return m_Viewport; }
        [[nodiscard]] GraphSelection& Selection() noexcept { return m_Selection; }
        [[nodiscard]] const GraphSelection& Selection() const noexcept { return m_Selection; }
        [[nodiscard]] GraphConnectionDrag& ConnectionDrag() noexcept { return m_ConnectionDrag; }
        [[nodiscard]] const GraphConnectionDrag& ConnectionDrag() const noexcept { return m_ConnectionDrag; }
        [[nodiscard]] const std::string& TextDraft() const noexcept { return m_TextDraft; }
        [[nodiscard]] const std::string& BaselineSource() const noexcept { return m_BaselineSource; }
        [[nodiscard]] bool IsTextDirty() const noexcept { return m_TextDraft != m_BaselineSource; }
        [[nodiscard]] bool HasExternalConflict() const noexcept { return m_ExternalConflict; }

        /// Input: temporary editor text, which may be syntactically incomplete.
        /// Task: retain it without parsing so normal keystrokes can create
        /// short-lived invalid syntax. The parser remains the apply boundary.
        void SetTextDraft(std::string source)
        {
            if (source.size() > MaximumDocumentDraftBytes)
                throw std::length_error("Document text draft exceeds the 16 MiB editor safety limit.");
            m_TextDraft = std::move(source);
        }

        /// Mutable buffer contract used by native text widgets with explicit
        /// resize callbacks. Callers must resize through ResizeTextDraft so the
        /// workspace safety limit remains authoritative.
        [[nodiscard]] char* TextDraftData() noexcept { return m_TextDraft.data(); }
        [[nodiscard]] std::size_t TextDraftCapacity() const noexcept { return m_TextDraft.capacity(); }
        void ResizeTextDraft(std::size_t size)
        {
            if (size > MaximumDocumentDraftBytes)
                throw std::length_error("Document text draft exceeds the 16 MiB editor safety limit.");
            m_TextDraft.resize(size);
        }

        /// Synchronizes clean text after graph/undo changes. If local text is
        /// dirty, the draft survives and a conflict is raised when the current
        /// canonical document no longer matches its editing baseline.
        void Synchronize(const AuthoringDocument& document)
        {
            RequireIdentity(document);
            m_Selection.RemoveMissing(document);
            const std::string canonical = BuildBoundedCanonicalSource(document);
            if (!IsTextDirty())
            {
                m_TextDraft = canonical;
                m_BaselineSource = canonical;
                m_ExternalConflict = false;
                return;
            }
            m_ExternalConflict = canonical != m_BaselineSource;
        }

        /// Reverts local text to the current canonical graph representation.
        void ResetText(const AuthoringDocument& document)
        {
            if (m_DocumentID.IsValid()) RequireIdentity(document);
            m_DocumentID = document.ID();
            m_TextDraft = BuildBoundedCanonicalSource(document);
            m_BaselineSource = m_TextDraft;
            m_ExternalConflict = false;
            m_Selection.RemoveMissing(document);
        }

        /// Records a successful ApplyDocumentTextCommand. Call only after the
        /// command has mutated the authoritative document; this refreshes the
        /// canonical source and clears the resolved draft/conflict state.
        void TextApplySucceeded(const AuthoringDocument& document)
        {
            ResetText(document);
        }

    private:
        kairo::assets::AssetID m_DocumentID;
        GraphViewport m_Viewport;
        GraphSelection m_Selection;
        GraphConnectionDrag m_ConnectionDrag;
        std::string m_TextDraft;
        std::string m_BaselineSource;
        bool m_ExternalConflict = false;

        /// Produces the authoritative text representation while preserving the
        /// same memory bound used by interactive edits. Documents may also be
        /// constructed programmatically, so the UI cannot assume their
        /// canonical projection is already small enough for an editor buffer.
        [[nodiscard]] static std::string BuildBoundedCanonicalSource(const AuthoringDocument& document)
        {
            std::string source = BuildDocumentTextProjection(document).Source();
            if (source.size() > MaximumDocumentDraftBytes)
                throw std::length_error("Document canonical source exceeds the 16 MiB editor safety limit.");
            return source;
        }

        void RequireIdentity(const AuthoringDocument& document) const
        {
            if (document.ID() != m_DocumentID)
                throw std::invalid_argument("Document view state cannot synchronize a different document identity.");
        }
    };

    /// Address-stable per-document view-state registry. Opening an existing ID
    /// returns its retained state, allowing tab switches without resetting pan,
    /// zoom, selection, or a text draft. Project close should call Clear().
    class AuthoringWorkspaceState final
    {
    public:
        [[nodiscard]] std::size_t Size() const noexcept { return m_Views.size(); }
        [[nodiscard]] bool Contains(kairo::assets::AssetID id) const noexcept { return m_Views.contains(id); }
        [[nodiscard]] bool HasDirtyTextDrafts() const noexcept
        {
            return std::ranges::any_of(m_Views,
                [](const auto& entry) { return entry.second.IsTextDirty(); });
        }
        [[nodiscard]] std::vector<kairo::assets::AssetID> DocumentIDs() const
        {
            std::vector<kairo::assets::AssetID> result;
            result.reserve(m_Views.size());
            for (const auto& [id, view] : m_Views)
            {
                (void)view;
                result.push_back(id);
            }
            return result;
        }

        [[nodiscard]] DocumentViewState& Open(const AuthoringDocument& document)
        {
            const auto [view, inserted] = m_Views.try_emplace(document.ID(), document);
            if (!inserted) view->second.Synchronize(document);
            return view->second;
        }

        [[nodiscard]] DocumentViewState& At(kairo::assets::AssetID id)
        {
            const auto found = m_Views.find(id);
            if (found == m_Views.end()) throw std::out_of_range("Document view state is not open.");
            return found->second;
        }

        [[nodiscard]] const DocumentViewState& At(kairo::assets::AssetID id) const
        {
            const auto found = m_Views.find(id);
            if (found == m_Views.end()) throw std::out_of_range("Document view state is not open.");
            return found->second;
        }

        void Close(kairo::assets::AssetID id) noexcept { m_Views.erase(id); }
        void Clear() noexcept { m_Views.clear(); }

    private:
        std::map<kairo::assets::AssetID, DocumentViewState> m_Views;
    };
}
