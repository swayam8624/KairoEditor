module;

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.AuthoringDocument;

import Kairo.Assets;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    inline constexpr std::size_t MaximumDocumentNodes = 100'000u;
    inline constexpr std::size_t MaximumDocumentPins = 1'000'000u;
    inline constexpr std::size_t MaximumDocumentConnections = 2'000'000u;

    struct DocumentPin final
    {
        PinID ID;
        std::string Key;
        std::string DisplayName;
        PinDirection Direction = PinDirection::Input;
        ValueType Type = ValueType::Flow;
        PinCardinality Cardinality = PinCardinality::Single;
        bool Required = false;
        std::optional<DocumentValue> DefaultValue;

        [[nodiscard]] bool operator==(const DocumentPin& other) const
        {
            const bool defaultsEqual = DefaultValue.has_value() == other.DefaultValue.has_value() &&
                (!DefaultValue.has_value() || *DefaultValue == *other.DefaultValue);
            return ID == other.ID && Key == other.Key && DisplayName == other.DisplayName &&
                Direction == other.Direction && Type == other.Type && Cardinality == other.Cardinality &&
                Required == other.Required && defaultsEqual;
        }
    };

    struct DocumentNode final
    {
        NodeID ID;
        std::string TypeKey;
        CanvasPosition Position;
        std::vector<DocumentPin> Pins;
        std::map<std::string, DocumentValue, std::less<>> Properties;

        friend bool operator==(const DocumentNode&, const DocumentNode&) = default;
    };

    struct DocumentConnection final
    {
        PinID Output;
        PinID Input;
        friend constexpr auto operator<=>(const DocumentConnection&, const DocumentConnection&) noexcept = default;
    };

    struct ConnectionCheck final
    {
        bool Allowed = false;
        std::string Code;
        std::string Message;
    };

    /// Backend-neutral structured authoring model shared by graph and text.
    ///
    /// Identity: the document uses an existing persistent Kairo asset UUID;
    /// nodes and pins use separate non-zero monotonic document-local IDs.
    /// Ordering: maps/sets make traversal and future serialization deterministic.
    /// Degeneracy: malformed schemas, non-finite positions, duplicate IDs,
    /// incompatible connections, and safety-limit overflows are rejected before
    /// observable mutation. Direct mutable container access is never exposed.
    class AuthoringDocument final
    {
    public:
        AuthoringDocument(kairo::assets::AssetID id, DocumentKind kind, std::string name)
            : m_ID(id), m_Kind(kind), m_Name(std::move(name))
        {
            if (!m_ID.IsValid()) throw std::invalid_argument("Authoring document requires a valid persistent asset ID.");
            ValidateUtf8Text(m_Name, { 1u, 256u, false, false }, "Document name");
        }

        [[nodiscard]] const kairo::assets::AssetID& ID() const noexcept { return m_ID; }
        [[nodiscard]] DocumentKind Kind() const noexcept { return m_Kind; }
        [[nodiscard]] const std::string& Name() const noexcept { return m_Name; }
        [[nodiscard]] std::size_t NodeCount() const noexcept { return m_Nodes.size(); }
        [[nodiscard]] std::size_t PinCount() const noexcept { return m_PinOwners.size(); }
        [[nodiscard]] std::size_t ConnectionCount() const noexcept { return m_Connections.size(); }

        void Rename(std::string name)
        {
            ValidateUtf8Text(name, { 1u, 256u, false, false }, "Document name");
            m_Name = std::move(name);
        }

        [[nodiscard]] bool Contains(NodeID id) const noexcept { return m_Nodes.contains(id); }
        [[nodiscard]] bool Contains(PinID id) const noexcept { return m_PinOwners.contains(id); }

        [[nodiscard]] const DocumentNode& Node(NodeID id) const
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end()) throw std::out_of_range("Document does not contain this node ID.");
            return found->second;
        }

        [[nodiscard]] const DocumentPin& Pin(PinID id) const
        {
            const auto owner = m_PinOwners.find(id);
            if (owner == m_PinOwners.end()) throw std::out_of_range("Document does not contain this pin ID.");
            const auto& pins = Node(owner->second).Pins;
            const auto found = std::ranges::find(pins, id, &DocumentPin::ID);
            if (found == pins.end()) throw std::logic_error("Document pin index is internally inconsistent.");
            return *found;
        }

        [[nodiscard]] NodeID NodeForPin(PinID id) const
        {
            const auto found = m_PinOwners.find(id);
            if (found == m_PinOwners.end()) throw std::out_of_range("Document does not contain this pin ID.");
            return found->second;
        }

        [[nodiscard]] std::vector<NodeID> NodeIDs() const
        {
            std::vector<NodeID> result;
            result.reserve(m_Nodes.size());
            for (const auto& [id, node] : m_Nodes) result.push_back(id);
            return result;
        }

        [[nodiscard]] std::vector<DocumentConnection> Connections() const
        {
            return { m_Connections.begin(), m_Connections.end() };
        }

        [[nodiscard]] NodeID AddNode(const NodeSchema& schema, CanvasPosition position = {})
        {
            const NodeID id{ m_NextNode };
            if (!id) throw std::overflow_error("Document exhausted its 64-bit node ID space.");
            if (schema.Pins.size() > RemainingIDs(m_NextPin))
                throw std::overflow_error("Document exhausted its 64-bit pin ID space.");
            std::vector<PinID> pinIDs;
            pinIDs.reserve(schema.Pins.size());
            for (std::size_t index = 0u; index < schema.Pins.size(); ++index)
                pinIDs.push_back({ m_NextPin + static_cast<std::uint64_t>(index) });
            AddNodeWithIDs(schema, id, pinIDs, position);
            return id;
        }

        /// Task: restore serialized or undo-owned stable IDs. Pin IDs correspond
        /// to schema pin order. Allocation cursors advance past restored maxima
        /// and never wrap into the invalid zero identity.
        void AddNodeWithIDs(const NodeSchema& schema, NodeID nodeID,
            const std::vector<PinID>& pinIDs, CanvasPosition position = {})
        {
            ValidateNodeSchema(schema);
            if (schema.Kind != m_Kind) throw std::invalid_argument("Node schema belongs to a different document kind.");
            if (schema.Pins.size() != pinIDs.size())
                throw std::invalid_argument("Restored pin ID count must match the node schema.");

            DocumentNode node;
            node.ID = nodeID;
            node.TypeKey = schema.TypeKey;
            node.Position = position;
            node.Properties = schema.PropertyDefaults;
            node.Pins.reserve(schema.Pins.size());
            for (std::size_t index = 0u; index < schema.Pins.size(); ++index)
            {
                const PinSchema& pin = schema.Pins[index];
                node.Pins.push_back({ pinIDs[index], pin.Key, pin.DisplayName, pin.Direction,
                    pin.Type, pin.Cardinality, pin.Required, pin.DefaultValue });
            }
            RestoreNode(std::move(node));
        }

        /// Input: complete self-describing node record from persistence or undo.
        /// Task: retain unknown-plugin nodes without data loss while still
        /// proving structural safety. Schema compatibility remains a diagnostic,
        /// allowing the editor to open and preserve documents when a provider is
        /// temporarily unavailable.
        void RestoreNode(DocumentNode node)
        {
            ValidatePosition(node.Position);
            if (!node.ID) throw std::invalid_argument("Node ID cannot be zero.");
            if (node.TypeKey.size() > 128u || !IsSchemaKey(node.TypeKey, true))
                throw std::invalid_argument("Restored node has an invalid type key.");
            if (m_Nodes.size() >= MaximumDocumentNodes) throw std::length_error("Document exceeds its node safety limit.");
            if (node.Pins.size() > 256u) throw std::length_error("Restored node exceeds 256 pins.");
            if (node.Properties.size() > 256u) throw std::length_error("Restored node exceeds 256 properties.");
            if (m_PinOwners.size() + node.Pins.size() > MaximumDocumentPins)
                throw std::length_error("Document exceeds its pin safety limit.");
            if (m_Nodes.contains(node.ID)) throw std::invalid_argument("Document already contains this node ID.");

            std::set<PinID> uniquePins;
            std::set<std::string_view> uniquePinKeys;
            for (const DocumentPin& pin : node.Pins)
            {
                if (!pin.ID) throw std::invalid_argument("Pin ID cannot be zero.");
                if (m_PinOwners.contains(pin.ID) || !uniquePins.insert(pin.ID).second)
                    throw std::invalid_argument("Document contains a duplicate pin ID.");
                if (pin.Key.size() > 64u || !IsSchemaKey(pin.Key, false) || !uniquePinKeys.insert(pin.Key).second)
                    throw std::invalid_argument("Restored node contains an invalid or duplicate pin key.");
                ValidateUtf8Text(pin.DisplayName, { 1u, 128u, false, false }, "Pin display name");
                if (pin.Direction == PinDirection::Output && (pin.Required || pin.DefaultValue.has_value()))
                    throw std::invalid_argument("Restored output pin has invalid required/default state.");
                if (pin.Type == ValueType::Flow && pin.DefaultValue.has_value())
                    throw std::invalid_argument("Restored flow pin cannot have a default value.");
                if (pin.Required && pin.DefaultValue.has_value())
                    throw std::invalid_argument("Restored required pin cannot have a default value.");
                if (pin.DefaultValue.has_value() && pin.DefaultValue->Type() != pin.Type)
                    throw std::invalid_argument("Restored pin default type does not match the pin type.");
            }
            for (const auto& [key, value] : node.Properties)
            {
                if (key.size() > 64u || !IsSchemaKey(key, false))
                    throw std::invalid_argument("Restored node contains an invalid property key.");
                if (value.Type() == ValueType::Flow)
                    throw std::invalid_argument("Node properties cannot use the flow type.");
                value.Validate();
            }

            const NodeID nodeID = node.ID;
            std::vector<PinID> pinIDs;
            pinIDs.reserve(node.Pins.size());
            for (const DocumentPin& pin : node.Pins) pinIDs.push_back(pin.ID);
            const auto [inserted, accepted] = m_Nodes.emplace(nodeID, std::move(node));
            if (!accepted) throw std::logic_error("Document node insertion failed after duplicate validation.");
            std::vector<PinID> indexed;
            indexed.reserve(pinIDs.size());
            try
            {
                for (const PinID pinID : pinIDs)
                {
                    m_PinOwners.emplace(pinID, nodeID);
                    indexed.push_back(pinID);
                }
            }
            catch (...)
            {
                for (const PinID pinID : indexed) m_PinOwners.erase(pinID);
                m_Nodes.erase(inserted);
                throw;
            }

            m_NextNode = AdvanceCursor(m_NextNode, nodeID.Value);
            for (const PinID pinID : pinIDs) m_NextPin = AdvanceCursor(m_NextPin, pinID.Value);
        }

        void RemoveNode(NodeID id)
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end()) throw std::out_of_range("Document does not contain this node ID.");
            std::set<PinID> removedPins;
            for (const DocumentPin& pin : found->second.Pins) removedPins.insert(pin.ID);
            std::erase_if(m_Connections, [&removedPins](const DocumentConnection& connection)
            {
                return removedPins.contains(connection.Output) || removedPins.contains(connection.Input);
            });
            for (const PinID pin : removedPins) m_PinOwners.erase(pin);
            m_Nodes.erase(found);
        }

        void SetNodePosition(NodeID id, CanvasPosition position)
        {
            ValidatePosition(position);
            MutableNode(id).Position = position;
        }

        void SetProperty(NodeID id, std::string_view key, DocumentValue value)
        {
            value.Validate();
            auto& properties = MutableNode(id).Properties;
            const auto found = properties.find(key);
            if (found == properties.end()) throw std::out_of_range("Node has no property named: " + std::string(key));
            if (found->second.Type() != value.Type()) throw std::invalid_argument("Node property value type cannot change.");
            found->second = std::move(value);
        }

        void SetPinDefault(PinID id, DocumentValue value)
        {
            value.Validate();
            DocumentPin& pin = MutablePin(id);
            if (pin.Direction != PinDirection::Input) throw std::invalid_argument("Only input pins have default values.");
            if (pin.Type == ValueType::Flow) throw std::invalid_argument("Flow pins cannot have data defaults.");
            if (pin.Required) throw std::invalid_argument("Required pins cannot have default values.");
            if (pin.Type != value.Type()) throw std::invalid_argument("Pin default type does not match its declared type.");
            pin.DefaultValue = std::move(value);
        }

        void ClearPinDefault(PinID id)
        {
            DocumentPin& pin = MutablePin(id);
            if (pin.Direction != PinDirection::Input || pin.Type == ValueType::Flow)
                throw std::invalid_argument("This pin cannot have a data default.");
            pin.DefaultValue.reset();
        }

        [[nodiscard]] ConnectionCheck CanConnect(PinID outputID, PinID inputID) const
        {
            if (!Contains(outputID) || !Contains(inputID))
                return { false, "missing-pin", "Both connection endpoints must exist in the document." };
            if (outputID == inputID) return { false, "same-pin", "A pin cannot connect to itself." };
            const DocumentPin& output = Pin(outputID);
            const DocumentPin& input = Pin(inputID);
            if (output.Direction != PinDirection::Output || input.Direction != PinDirection::Input)
                return { false, "direction", "Connections must run from an output pin to an input pin." };
            if (output.Type != input.Type)
                return { false, "type", "Pin types are incompatible: " +
                    std::string(kairo::editor::Name(output.Type)) + " to " +
                    std::string(kairo::editor::Name(input.Type)) + "." };
            const DocumentConnection candidate{ outputID, inputID };
            if (m_Connections.contains(candidate))
                return { false, "duplicate", "This connection already exists." };

            const auto outputCount = CountConnections(outputID);
            const auto inputCount = CountConnections(inputID);
            if (output.Cardinality == PinCardinality::Single && outputCount != 0u)
                return { false, "output-cardinality", "The output pin accepts only one connection." };
            if (input.Cardinality == PinCardinality::Single && inputCount != 0u)
                return { false, "input-cardinality", "The input pin accepts only one connection." };
            if (m_Connections.size() >= MaximumDocumentConnections)
                return { false, "connection-limit", "The document reached its connection safety limit." };
            return { true, {}, {} };
        }

        void Connect(PinID output, PinID input)
        {
            const ConnectionCheck check = CanConnect(output, input);
            if (!check.Allowed) throw std::invalid_argument(check.Code + ": " + check.Message);
            m_Connections.insert({ output, input });
        }

        void Disconnect(PinID output, PinID input)
        {
            if (m_Connections.erase({ output, input }) == 0u)
                throw std::out_of_range("Document does not contain this connection.");
        }

        [[nodiscard]] bool IsConnected(PinID id) const noexcept { return CountConnections(id) != 0u; }

    private:
        kairo::assets::AssetID m_ID;
        DocumentKind m_Kind;
        std::string m_Name;
        std::map<NodeID, DocumentNode> m_Nodes;
        std::map<PinID, NodeID> m_PinOwners;
        std::set<DocumentConnection> m_Connections;
        std::uint64_t m_NextNode = 1u;
        std::uint64_t m_NextPin = 1u;

        [[nodiscard]] static std::size_t RemainingIDs(std::uint64_t cursor) noexcept
        {
            if (cursor == 0u) return 0u;
            const std::uint64_t remaining = std::numeric_limits<std::uint64_t>::max() - cursor + 1u;
            return remaining > std::numeric_limits<std::size_t>::max()
                ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(remaining);
        }

        [[nodiscard]] static std::uint64_t AdvanceCursor(std::uint64_t cursor, std::uint64_t used) noexcept
        {
            if (cursor == 0u || used < cursor) return cursor;
            return used == std::numeric_limits<std::uint64_t>::max() ? 0u : used + 1u;
        }

        static void ValidatePosition(CanvasPosition position)
        {
            constexpr double limit = 1.0e9;
            if (!std::isfinite(position.X) || !std::isfinite(position.Y) ||
                std::abs(position.X) > limit || std::abs(position.Y) > limit)
                throw std::invalid_argument("Canvas position must be finite and within +/-1e9 units.");
        }

        [[nodiscard]] DocumentNode& MutableNode(NodeID id)
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end()) throw std::out_of_range("Document does not contain this node ID.");
            return found->second;
        }

        [[nodiscard]] DocumentPin& MutablePin(PinID id)
        {
            const NodeID owner = NodeForPin(id);
            auto& pins = MutableNode(owner).Pins;
            const auto found = std::ranges::find(pins, id, &DocumentPin::ID);
            if (found == pins.end()) throw std::logic_error("Document pin index is internally inconsistent.");
            return *found;
        }

        [[nodiscard]] std::size_t CountConnections(PinID id) const noexcept
        {
            return static_cast<std::size_t>(std::ranges::count_if(m_Connections,
                [id](const DocumentConnection& connection)
                {
                    return connection.Output == id || connection.Input == id;
                }));
        }
    };
}
