module;

#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.CommandTransaction;

import Kairo.Editor.CommandHistory;

export namespace kairo::editor
{
    /// Reports that both a transaction operation and its compensating recovery
    /// failed. The two exception pointers remain available for diagnostics;
    /// callers must treat the affected authored model as requiring validation.
    class CommandTransactionRecoveryError final : public std::runtime_error
    {
    public:
        CommandTransactionRecoveryError(std::string message,
            std::exception_ptr operationFailure, std::exception_ptr recoveryFailure)
            : std::runtime_error(std::move(message)),
              m_OperationFailure(std::move(operationFailure)),
              m_RecoveryFailure(std::move(recoveryFailure)) {}

        [[nodiscard]] const std::exception_ptr& OperationFailure() const noexcept
        {
            return m_OperationFailure;
        }

        [[nodiscard]] const std::exception_ptr& RecoveryFailure() const noexcept
        {
            return m_RecoveryFailure;
        }

    private:
        std::exception_ptr m_OperationFailure;
        std::exception_ptr m_RecoveryFailure;
    };

    /// Groups multiple reversible edits into one atomic history entry.
    ///
    /// Input: non-null commands that each provide the strong exception guarantee.
    /// Output: ordered execute/redo and reverse-ordered undo as one EditorCommand.
    /// Task: provide the transaction primitive required by hierarchy operations,
    /// graph rewrites, modeling tools, prefab overrides, and approved AI edits.
    /// Failure behavior: a partial execute is undone; a partial undo is re-executed.
    /// If compensation itself fails, CommandTransactionRecoveryError preserves
    /// both failures because the authored model can no longer be assumed intact.
    class CompositeEditorCommand final : public EditorCommand
    {
    public:
        CompositeEditorCommand(std::string name,
            std::vector<std::unique_ptr<EditorCommand>> commands)
            : m_Name(std::move(name)), m_Commands(std::move(commands))
        {
            if (m_Name.empty()) throw std::invalid_argument("A command transaction requires a name.");
            if (m_Commands.empty()) throw std::invalid_argument("A command transaction cannot be empty.");
            for (const auto& command : m_Commands)
                if (!command) throw std::invalid_argument("A command transaction cannot contain a null command.");
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return m_Name; }
        [[nodiscard]] std::size_t CommandCount() const noexcept { return m_Commands.size(); }

        void Execute() override
        {
            if (m_State == State::Applied)
                throw std::logic_error("A command transaction is already applied.");

            std::size_t applied = 0u;
            try
            {
                for (; applied < m_Commands.size(); ++applied) m_Commands[applied]->Execute();
            }
            catch (...)
            {
                const std::exception_ptr operationFailure = std::current_exception();
                const std::exception_ptr recoveryFailure = UndoAppliedPrefix(applied);
                if (recoveryFailure)
                    throw CommandTransactionRecoveryError(
                        "Command transaction execute failed and rollback also failed.",
                        operationFailure, recoveryFailure);
                std::rethrow_exception(operationFailure);
            }
            m_State = State::Applied;
        }

        void Undo() override
        {
            if (m_State != State::Applied)
                throw std::logic_error("A command transaction is not applied.");

            std::size_t firstUndone = m_Commands.size();
            try
            {
                while (firstUndone != 0u)
                {
                    --firstUndone;
                    m_Commands[firstUndone]->Undo();
                }
            }
            catch (...)
            {
                const std::exception_ptr operationFailure = std::current_exception();
                const std::exception_ptr recoveryFailure = ExecuteSuffix(firstUndone + 1u);
                if (recoveryFailure)
                    throw CommandTransactionRecoveryError(
                        "Command transaction undo failed and state restoration also failed.",
                        operationFailure, recoveryFailure);
                std::rethrow_exception(operationFailure);
            }
            m_State = State::Undone;
        }

    private:
        enum class State : unsigned char { Ready, Applied, Undone };

        std::string m_Name;
        std::vector<std::unique_ptr<EditorCommand>> m_Commands;
        State m_State = State::Ready;

        [[nodiscard]] std::exception_ptr UndoAppliedPrefix(std::size_t applied) noexcept
        {
            while (applied != 0u)
            {
                --applied;
                try
                {
                    m_Commands[applied]->Undo();
                }
                catch (...)
                {
                    return std::current_exception();
                }
            }
            return {};
        }

        [[nodiscard]] std::exception_ptr ExecuteSuffix(std::size_t first) noexcept
        {
            for (std::size_t index = first; index < m_Commands.size(); ++index)
            {
                try
                {
                    m_Commands[index]->Execute();
                }
                catch (...)
                {
                    return std::current_exception();
                }
            }
            return {};
        }
    };

    /// Collects commands without mutating authored data until Commit is called.
    /// Destruction and Cancel discard an open transaction. Commit consumes the
    /// transaction even when execution fails, preventing accidental retries of
    /// stateful command objects after an error.
    class CommandTransaction final
    {
    public:
        explicit CommandTransaction(std::string name) : m_Name(std::move(name))
        {
            if (m_Name.empty()) throw std::invalid_argument("A command transaction requires a name.");
        }

        CommandTransaction(const CommandTransaction&) = delete;
        CommandTransaction& operator=(const CommandTransaction&) = delete;
        CommandTransaction(CommandTransaction&& other) noexcept
            : m_Name(std::move(other.m_Name)),
              m_Commands(std::move(other.m_Commands)),
              m_Open(std::exchange(other.m_Open, false)) {}

        CommandTransaction& operator=(CommandTransaction&& other) noexcept
        {
            if (this == &other) return *this;
            m_Name = std::move(other.m_Name);
            m_Commands = std::move(other.m_Commands);
            m_Open = std::exchange(other.m_Open, false);
            return *this;
        }

        [[nodiscard]] bool IsOpen() const noexcept { return m_Open; }
        [[nodiscard]] std::size_t CommandCount() const noexcept { return m_Commands.size(); }

        void Add(std::unique_ptr<EditorCommand> command)
        {
            RequireOpen();
            if (!command) throw std::invalid_argument("Cannot add a null command to a transaction.");
            m_Commands.push_back(std::move(command));
        }

        template<class Command, class... Arguments>
        Command& Emplace(Arguments&&... arguments)
        {
            auto command = std::make_unique<Command>(std::forward<Arguments>(arguments)...);
            Command& result = *command;
            Add(std::move(command));
            return result;
        }

        void Commit(CommandHistory& history)
        {
            RequireOpen();
            if (m_Commands.empty()) throw std::logic_error("Cannot commit an empty command transaction.");
            m_Open = false;
            history.Execute(std::make_unique<CompositeEditorCommand>(
                std::move(m_Name), std::move(m_Commands)));
        }

        void Cancel() noexcept
        {
            m_Commands.clear();
            m_Open = false;
        }

    private:
        std::string m_Name;
        std::vector<std::unique_ptr<EditorCommand>> m_Commands;
        bool m_Open = true;

        void RequireOpen() const
        {
            if (!m_Open) throw std::logic_error("Command transaction is already closed.");
        }
    };
}
