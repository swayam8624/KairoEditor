// TEACHING MODULE: UI-independent undo/redo boundary. Commands own the
// before/after data needed to reverse authoring changes; they mutate
// ProjectSession/EngineCore scene or documents, not ImGui widget state.
//
// A Command's Execute/Undo must provide a strong exception guarantee:
// if it throws, authored data remains unchanged and the history cursor
// must not advance. On a new edit after Undo, redo history is replaced.
// TryMerge permits gesture coalescing (e.g., inspector drag) but should
// not merge unrelated edits or cross transaction boundaries.
//
// Distinguish this *authoring history* from RuntimeWorld's deferred game
// commands and PhysicsWorld's contact cache; they have different lifetimes
// and persistence semantics. See SceneCommands.cppm and EditorTests.cpp.
module;

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.CommandHistory;

export namespace kairo::editor
{
    /// One reversible authored-data operation.
    ///
    /// Input: a concrete command containing every value needed to apply and
    /// reverse one edit. Output: deterministic mutation through Execute/Undo.
    /// Task: provide a UI-independent transaction boundary shared by hierarchy,
    /// Inspector, graph, code, timeline, and future automation surfaces.
    ///
    /// Commands must provide the strong exception guarantee: a throwing
    /// Execute or Undo leaves authored data unchanged. CommandHistory changes
    /// its cursor only after the operation succeeds.
    class EditorCommand
    {
    public:
        virtual ~EditorCommand() = default;
        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        virtual void Execute() = 0;
        virtual void Undo() = 0;

        /// Task: coalesce adjacent edits such as Inspector drag frames.
        /// The newer command has already executed when this is called. Return
        /// true only when this command can adopt its final undo/redo value.
        [[nodiscard]] virtual bool TryMerge(EditorCommand&) noexcept { return false; }
    };

    /// Owns a linear, bounded undo/redo journal.
    ///
    /// Input: successful EditorCommand transactions. Output: causal undo/redo
    /// traversal and human-readable next-operation names. Task: guarantee that
    /// a new edit after undo discards the obsolete redo branch. The entry bound
    /// prevents an arbitrarily long editing session from retaining unbounded
    /// command payloads; the oldest applied operation is evicted first.
    // Owns executed commands plus a cursor dividing applied operations
    // from redoable ones. A throwing command must not corrupt the cursor.
    class CommandHistory final
    {
    public:
        explicit CommandHistory(std::size_t maximumEntries = 512u) : m_MaximumEntries(maximumEntries)
        {
            if (maximumEntries == 0u)
                throw std::invalid_argument("Command history capacity must be positive.");
            m_Entries.reserve(maximumEntries);
        }

        CommandHistory(const CommandHistory&) = delete;
        CommandHistory& operator=(const CommandHistory&) = delete;
        CommandHistory(CommandHistory&&) noexcept = default;
        CommandHistory& operator=(CommandHistory&&) noexcept = default;

        [[nodiscard]] bool CanUndo() const noexcept { return m_Cursor != 0u; }
        [[nodiscard]] bool CanRedo() const noexcept { return m_Cursor != m_Entries.size(); }
        [[nodiscard]] std::size_t AppliedCount() const noexcept { return m_Cursor; }
        [[nodiscard]] std::size_t RetainedCount() const noexcept { return m_Entries.size(); }

        [[nodiscard]] std::string_view UndoName() const noexcept
        {
            return CanUndo() ? m_Entries[m_Cursor - 1u]->Name() : std::string_view{};
        }

        [[nodiscard]] std::string_view RedoName() const noexcept
        {
            return CanRedo() ? m_Entries[m_Cursor]->Name() : std::string_view{};
        }

        /// Executes before changing journal ownership. If execution fails, the
        /// existing undo and redo branches remain intact.
        // A successfully executed new command invalidates the redo
        // branch. Merging is optional and must respect the earlier
        // command's undo information and transaction boundary.
        void Execute(std::unique_ptr<EditorCommand> command)
        {
            if (!command) throw std::invalid_argument("Cannot execute a null editor command.");
            command->Execute();

            if (m_Cursor != m_Entries.size())
                m_Entries.erase(m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Cursor), m_Entries.end());

            if (m_Cursor != 0u && m_Entries[m_Cursor - 1u]->TryMerge(*command)) return;

            if (m_Entries.size() == m_MaximumEntries)
            {
                m_Entries.erase(m_Entries.begin());
                --m_Cursor;
            }
            m_Entries.push_back(std::move(command));
            ++m_Cursor;
        }

        // Reverse the latest applied command, moving the cursor only
        // after Undo succeeds; failure must preserve history position.
        void Undo()
        {
            if (!CanUndo()) throw std::logic_error("No editor command is available to undo.");
            m_Entries[m_Cursor - 1u]->Undo();
            --m_Cursor;
        }

        // Reapply from the redo suffix. The command still owns its
        // before/after state and does not reconstruct it from the UI.
        void Redo()
        {
            if (!CanRedo()) throw std::logic_error("No editor command is available to redo.");
            m_Entries[m_Cursor]->Execute();
            ++m_Cursor;
        }

        /// Task: release commands whose object references belong to a closing
        /// or replacing project. Hosts must clear history at document identity
        /// boundaries before destroying the referenced session.
        void Clear() noexcept
        {
            m_Entries.clear();
            m_Cursor = 0u;
        }

    private:
        std::size_t m_MaximumEntries;
        std::vector<std::unique_ptr<EditorCommand>> m_Entries;
        std::size_t m_Cursor = 0u;
    };
}
