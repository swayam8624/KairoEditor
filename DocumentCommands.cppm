module;

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentCommands;

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;

export namespace kairo::editor
{
    /// Creates a schema-backed node and preserves every allocated local ID for
    /// redo. Input: immutable schema snapshot and finite canvas position.
    /// Output: one new node. Degeneracy: CreatedNode is unavailable until the
    /// first successful Execute; undo removes all incident edges as usual.
    class AddDocumentNodeCommand final : public EditorCommand
    {
    public:
        AddDocumentNodeCommand(AuthoringDocument& document, NodeSchema schema,
            CanvasPosition position = {})
            : m_Document(&document), m_Schema(std::move(schema)), m_Position(position) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Add Node"; }

        [[nodiscard]] NodeID CreatedNode() const
        {
            if (!m_Snapshot.has_value()) throw std::logic_error("Add Node has not executed yet.");
            return m_Snapshot->ID;
        }

        void Execute() override
        {
            if (m_Snapshot.has_value())
            {
                m_Document->RestoreNode(*m_Snapshot);
                return;
            }
            const NodeID id = m_Document->AddNode(m_Schema, m_Position);
            try { m_Snapshot = m_Document->Node(id); }
            catch (...)
            {
                m_Document->RemoveNode(id);
                throw;
            }
        }

        void Undo() override { m_Document->RemoveNode(CreatedNode()); }

    private:
        AuthoringDocument* m_Document;
        NodeSchema m_Schema;
        CanvasPosition m_Position;
        std::optional<DocumentNode> m_Snapshot;
    };

    /// Removes a node while retaining its self-describing record and every
    /// incident edge. Undo is transactional: if an unexpected edge restore
    /// failure occurs, the restored node is removed again before propagating.
    class RemoveDocumentNodeCommand final : public EditorCommand
    {
    public:
        RemoveDocumentNodeCommand(AuthoringDocument& document, NodeID node)
            : m_Document(&document), m_Node(document.Node(node)),
              m_Connections(document.ConnectionsForNode(node)) {}

        [[nodiscard]] std::string_view Name() const noexcept override { return "Remove Node"; }
        void Execute() override { m_Document->RemoveNode(m_Node.ID); }

        void Undo() override
        {
            m_Document->RestoreNode(m_Node);
            try
            {
                for (const DocumentConnection& connection : m_Connections)
                    m_Document->Connect(connection.Output, connection.Input);
            }
            catch (...)
            {
                m_Document->RemoveNode(m_Node.ID);
                throw;
            }
        }

    private:
        AuthoringDocument* m_Document;
        DocumentNode m_Node;
        std::vector<DocumentConnection> m_Connections;
    };

    /// Removes one graph selection as a single reversible transaction.
    /// Connections shared by two selected nodes are captured once, allowing
    /// Undo to restore exact topology without exposing partial deletion steps.
    class RemoveDocumentNodesCommand final : public EditorCommand
    {
    public:
        RemoveDocumentNodesCommand(AuthoringDocument& document, std::vector<NodeID> nodes)
            : m_Document(&document)
        {
            std::ranges::sort(nodes);
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
            if (nodes.empty()) throw std::invalid_argument("Remove Nodes requires a non-empty selection.");
            m_Nodes.reserve(nodes.size());
            for (const NodeID id : nodes)
            {
                m_Nodes.push_back(document.Node(id));
                for (const auto& connection : document.ConnectionsForNode(id))
                    if (std::ranges::find(m_Connections, connection) == m_Connections.end())
                        m_Connections.push_back(connection);
            }
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return "Remove Nodes"; }

        void Execute() override
        {
            for (const DocumentNode& node : m_Nodes)
                if (m_Document->Contains(node.ID)) m_Document->RemoveNode(node.ID);
        }

        void Undo() override
        {
            std::vector<NodeID> restored;
            try
            {
                for (const DocumentNode& node : m_Nodes)
                {
                    m_Document->RestoreNode(node);
                    restored.push_back(node.ID);
                }
                for (const auto& connection : m_Connections)
                    m_Document->Connect(connection.Output, connection.Input);
            }
            catch (...)
            {
                for (const NodeID id : restored)
                    if (m_Document->Contains(id)) m_Document->RemoveNode(id);
                throw;
            }
        }

    private:
        AuthoringDocument* m_Document;
        std::vector<DocumentNode> m_Nodes;
        std::vector<DocumentConnection> m_Connections;
    };

    /// Adds one validated directed edge. All type and cardinality rules remain
    /// centralized in AuthoringDocument rather than duplicated by the command.
    class ConnectDocumentPinsCommand final : public EditorCommand
    {
    public:
        ConnectDocumentPinsCommand(AuthoringDocument& document, PinID output, PinID input)
            : m_Document(&document), m_Connection{ output, input } {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Connect Pins"; }
        void Execute() override { m_Document->Connect(m_Connection.Output, m_Connection.Input); }
        void Undo() override { m_Document->Disconnect(m_Connection.Output, m_Connection.Input); }
    private:
        AuthoringDocument* m_Document;
        DocumentConnection m_Connection;
    };

    /// Removes one existing directed edge and restores that exact edge on undo.
    class DisconnectDocumentPinsCommand final : public EditorCommand
    {
    public:
        DisconnectDocumentPinsCommand(AuthoringDocument& document, PinID output, PinID input)
            : m_Document(&document), m_Connection{ output, input }
        {
            const auto connections = document.Connections();
            if (std::ranges::find(connections, m_Connection) == connections.end())
                throw std::out_of_range("Cannot create disconnect command for a missing connection.");
        }
        [[nodiscard]] std::string_view Name() const noexcept override { return "Disconnect Pins"; }
        void Execute() override { m_Document->Disconnect(m_Connection.Output, m_Connection.Input); }
        void Undo() override { m_Document->Connect(m_Connection.Output, m_Connection.Input); }
    private:
        AuthoringDocument* m_Document;
        DocumentConnection m_Connection;
    };

    /// Replaces one canvas position. Consecutive drag frames for the same node
    /// merge into one undo entry while preserving the pre-drag position.
    class SetDocumentNodePositionCommand final : public EditorCommand
    {
    public:
        SetDocumentNodePositionCommand(AuthoringDocument& document, NodeID node, CanvasPosition value)
            : m_Document(&document), m_Node(node), m_Before(document.Node(node).Position), m_After(value) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Move Node"; }
        void Execute() override { m_Document->SetNodePosition(m_Node, m_After); }
        void Undo() override { m_Document->SetNodePosition(m_Node, m_Before); }
        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* move = dynamic_cast<SetDocumentNodePositionCommand*>(&newer);
            if (move == nullptr || move->m_Document != m_Document || move->m_Node != m_Node) return false;
            m_After = move->m_After;
            return true;
        }
    private:
        AuthoringDocument* m_Document;
        NodeID m_Node;
        CanvasPosition m_Before;
        CanvasPosition m_After;
    };

    struct DocumentNodePositionEdit final
    {
        NodeID Node;
        CanvasPosition Position;
        friend bool operator==(const DocumentNodePositionEdit&, const DocumentNodePositionEdit&) = default;
    };

    /// Moves one selection as one causal command. Every node and target is
    /// validated before Execute, so applying the sorted batch cannot partially
    /// fail. Consecutive pointer frames with the same identity set merge while
    /// retaining the positions that existed before the drag began.
    class SetDocumentNodePositionsCommand final : public EditorCommand
    {
    public:
        SetDocumentNodePositionsCommand(AuthoringDocument& document,
            std::vector<DocumentNodePositionEdit> edits)
            : m_Document(&document), m_After(std::move(edits))
        {
            if (m_After.empty()) throw std::invalid_argument("A multi-node move requires at least one node.");
            if (m_After.size() > MaximumDocumentNodes)
                throw std::length_error("A multi-node move exceeds the document node safety limit.");
            std::ranges::sort(m_After, {}, &DocumentNodePositionEdit::Node);
            m_Before.reserve(m_After.size());
            std::optional<NodeID> previous;
            for (const auto& edit : m_After)
            {
                if (!edit.Node) throw std::invalid_argument("A multi-node move contains a zero node ID.");
                if (previous == edit.Node) throw std::invalid_argument("A multi-node move contains a duplicate node ID.");
                ValidateCanvasPosition(edit.Position);
                m_Before.push_back({ edit.Node, document.Node(edit.Node).Position });
                previous = edit.Node;
            }
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return "Move Nodes"; }
        void Execute() override { Apply(m_After); }
        void Undo() override { Apply(m_Before); }

        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* move = dynamic_cast<SetDocumentNodePositionsCommand*>(&newer);
            if (move == nullptr || move->m_Document != m_Document ||
                move->m_After.size() != m_After.size()) return false;
            for (std::size_t index = 0u; index < m_After.size(); ++index)
                if (move->m_After[index].Node != m_After[index].Node) return false;
            m_After.swap(move->m_After);
            return true;
        }

    private:
        AuthoringDocument* m_Document;
        std::vector<DocumentNodePositionEdit> m_Before;
        std::vector<DocumentNodePositionEdit> m_After;

        void Apply(const std::vector<DocumentNodePositionEdit>& edits)
        {
            for (const auto& edit : edits) m_Document->SetNodePosition(edit.Node, edit.Position);
        }
    };

    /// Replaces one typed property. The document enforces type stability;
    /// adjacent edits to the same property coalesce for sliders/text entry.
    class SetDocumentPropertyCommand final : public EditorCommand
    {
    public:
        SetDocumentPropertyCommand(AuthoringDocument& document, NodeID node,
            std::string key, DocumentValue value)
            : m_Document(&document), m_Node(node), m_Key(std::move(key)),
              m_Before(document.Node(node).Properties.at(m_Key)), m_After(std::move(value)) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Edit Node Property"; }
        void Execute() override { m_Document->SetProperty(m_Node, m_Key, m_After); }
        void Undo() override { m_Document->SetProperty(m_Node, m_Key, m_Before); }
        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* edit = dynamic_cast<SetDocumentPropertyCommand*>(&newer);
            if (edit == nullptr || edit->m_Document != m_Document || edit->m_Node != m_Node ||
                edit->m_Key != m_Key) return false;
            m_After = edit->m_After;
            return true;
        }
    private:
        AuthoringDocument* m_Document;
        NodeID m_Node;
        std::string m_Key;
        DocumentValue m_Before;
        DocumentValue m_After;
    };

    /// Replaces or clears one input-pin default as a single reversible value.
    /// Optional state is intentional: an absent default differs from a typed
    /// zero and is preserved exactly through undo and merge.
    class SetDocumentPinDefaultCommand final : public EditorCommand
    {
    public:
        SetDocumentPinDefaultCommand(AuthoringDocument& document, PinID pin,
            std::optional<DocumentValue> value)
            : m_Document(&document), m_Pin(pin), m_Before(document.Pin(pin).DefaultValue),
              m_After(std::move(value)) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Edit Pin Default"; }
        void Execute() override { Apply(m_After); }
        void Undo() override { Apply(m_Before); }
        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* edit = dynamic_cast<SetDocumentPinDefaultCommand*>(&newer);
            if (edit == nullptr || edit->m_Document != m_Document || edit->m_Pin != m_Pin) return false;
            m_After = edit->m_After;
            return true;
        }
    private:
        void Apply(const std::optional<DocumentValue>& value)
        {
            if (value.has_value()) m_Document->SetPinDefault(m_Pin, *value);
            else m_Document->ClearPinDefault(m_Pin);
        }
        AuthoringDocument* m_Document;
        PinID m_Pin;
        std::optional<DocumentValue> m_Before;
        std::optional<DocumentValue> m_After;
    };

    /// Renames the document with the same UTF-8 validation used by direct API
    /// mutation. Consecutive keystrokes collapse into one undo operation.
    class RenameDocumentCommand final : public EditorCommand
    {
    public:
        RenameDocumentCommand(AuthoringDocument& document, std::string value)
            : m_Document(&document), m_Before(document.Name()), m_After(std::move(value)) {}
        [[nodiscard]] std::string_view Name() const noexcept override { return "Rename Document"; }
        void Execute() override { m_Document->Rename(m_After); }
        void Undo() override { m_Document->Rename(m_Before); }
        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            auto* rename = dynamic_cast<RenameDocumentCommand*>(&newer);
            if (rename == nullptr || rename->m_Document != m_Document) return false;
            m_After.swap(rename->m_After);
            return true;
        }
    private:
        AuthoringDocument* m_Document;
        std::string m_Before;
        std::string m_After;
    };
}
