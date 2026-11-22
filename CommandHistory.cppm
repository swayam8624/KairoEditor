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
        // One edit is executed against ProjectSession *before* changing
        // the undo journal. On success the redo suffix is discarded;
        // TryMerge can coalesce a drag into the previous command while
        // preserving its original before-value. A bounded journal
        // evicts oldest commands if capacity is exhausted.
        // Command::Execute is expected to supply its own rollback
        // if it throws; this class cannot undo arbitrary partial edits.
        void Execute(std::unique_ptr<EditorCommand> command)
        {
            if (!command) throw std::invalid_argument("Cannot execute a null editor command.");
            command->Execute();

            // New edits fork history: after Undo, previously redoable
            // actions are invalid because the authored scene has
            // taken another path. Erase that suffix before appending.
            if (m_Cursor != m_Entries.size())
                m_Entries.erase(m_Entries.begin() + static_cast<std::ptrdiff_t>(m_Cursor), m_Entries.end());

            // Gestures such as repeated position changes merge only
            // if the command verifies the same project/Entity/property.
            // The most recent after-value replaces the old after,
            // while the old before-value stays available for Undo.
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
        // Run the inverse operation before decrementing the journal
        // cursor. The command remains present if Undo throws, but
        // the command implementation is still responsible for
        // not leaving a half-restored Scene.
        void Undo()
        {
            if (!CanUndo()) throw std::logic_error("No editor command is available to undo.");
            m_Entries[m_Cursor - 1u]->Undo();
            --m_Cursor;
        }

        // Reapply from the redo suffix. The command still owns its
        // before/after state and does not reconstruct it from the UI.
        // Re-execute the saved command, then advance cursor on
        // success. Do not reconstruct after-values from ImGui widgets:
        // UI text/drag values may have changed since original Execute.
        void Redo()
        {
            if (!CanRedo()) throw std::logic_error("No editor command is available to redo.");
            m_Entries[m_Cursor]->Execute();
            ++m_Cursor;
        }

        /// Task: release commands whose object references belong to a closing
        /// or replacing project. Hosts must clear history at document identity
        /// boundaries before destroying the referenced session.
        // ProjectSession/EngineCore Scene ownership may be replaced
        // or closed; all commands borrow pointers into that project.
        // Clear the journal before those objects become invalid.
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
