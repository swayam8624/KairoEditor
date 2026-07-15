module;

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentSchema;

import Kairo.Editor.DocumentTypes;
import Kairo.Editor.TextValidation;

export namespace kairo::editor
{
    struct PinSchema final
    {
        std::string Key;
        std::string DisplayName;
        PinDirection Direction = PinDirection::Input;
        ValueType Type = ValueType::Flow;
        PinCardinality Cardinality = PinCardinality::Single;
        bool Required = false;
        std::optional<DocumentValue> DefaultValue;

        [[nodiscard]] bool operator==(const PinSchema& other) const
        {
            const bool defaultsEqual = DefaultValue.has_value() == other.DefaultValue.has_value() &&
                (!DefaultValue.has_value() || *DefaultValue == *other.DefaultValue);
            return Key == other.Key && DisplayName == other.DisplayName && Direction == other.Direction &&
                Type == other.Type && Cardinality == other.Cardinality && Required == other.Required && defaultsEqual;
        }
    };

    struct NodeSchema final
    {
        DocumentKind Kind = DocumentKind::Logic;
        std::string TypeKey;
        std::string DisplayName;
        std::string Category;
        std::vector<PinSchema> Pins;
        std::map<std::string, DocumentValue, std::less<>> PropertyDefaults;

        friend bool operator==(const NodeSchema&, const NodeSchema&) = default;
    };

    [[nodiscard]] inline bool IsSchemaKey(std::string_view key, bool allowDots) noexcept
    {
        if (key.empty()) return false;
        bool segmentStart = true;
        for (const unsigned char character : key)
        {
            if (allowDots && character == '.')
            {
                if (segmentStart) return false;
                segmentStart = true;
                continue;
            }
            const bool alpha = (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') || character == '_';
            const bool continuation = alpha || (character >= '0' && character <= '9') || character == '-';
            if ((segmentStart && !alpha) || (!segmentStart && !continuation)) return false;
            segmentStart = false;
        }
        return !segmentStart;
    }

    /// Task: prove a node type can instantiate deterministically before it
    /// enters a registry used by documents, search, serialization, or compilers.
    inline void ValidateNodeSchema(const NodeSchema& schema)
    {
        if (schema.TypeKey.size() > 128u || !IsSchemaKey(schema.TypeKey, true))
            throw std::invalid_argument("Node schema type key must contain 1 to 128 ASCII identifier bytes.");
        ValidateUtf8Text(schema.DisplayName, { 1u, 128u, false, false }, "Node schema display name");
        ValidateUtf8Text(schema.Category, { 1u, 128u, false, false }, "Node schema category");
        if (schema.Pins.size() > 256u) throw std::length_error("Node schema exceeds 256 pins.");
        if (schema.PropertyDefaults.size() > 256u) throw std::length_error("Node schema exceeds 256 properties.");

        std::vector<std::string_view> pinKeys;
        pinKeys.reserve(schema.Pins.size());
        for (const PinSchema& pin : schema.Pins)
        {
            if (pin.Key.size() > 64u || !IsSchemaKey(pin.Key, false))
                throw std::invalid_argument("Pin schema key must contain 1 to 64 ASCII identifier bytes.");
            ValidateUtf8Text(pin.DisplayName, { 1u, 128u, false, false }, "Pin display name");
            if (std::ranges::find(pinKeys, pin.Key) != pinKeys.end())
                throw std::invalid_argument("Node schema contains duplicate pin key: " + pin.Key);
            pinKeys.push_back(pin.Key);

            if (pin.Direction == PinDirection::Output && pin.DefaultValue.has_value())
                throw std::invalid_argument("Output pins cannot define default values.");
            if (pin.Type == ValueType::Flow && pin.DefaultValue.has_value())
                throw std::invalid_argument("Flow pins cannot define data defaults.");
            if (pin.Required && pin.DefaultValue.has_value())
                throw std::invalid_argument("A required pin cannot also define a default value.");
            if (pin.DefaultValue.has_value() && pin.DefaultValue->Type() != pin.Type)
                throw std::invalid_argument("Pin default type does not match its declared value type.");
            if (pin.Direction == PinDirection::Output && pin.Required)
                throw std::invalid_argument("Only input pins can be marked required.");
        }

        for (const auto& [key, value] : schema.PropertyDefaults)
        {
            if (key.size() > 64u || !IsSchemaKey(key, false))
                throw std::invalid_argument("Property key must contain 1 to 64 ASCII identifier bytes.");
            value.Validate();
        }
    }

    /// Deterministically ordered catalog of node schemas. Registration copies a
    /// fully validated immutable definition; duplicate keys are rejected rather
    /// than silently changing the meaning of already-authored documents.
    class DocumentSchemaRegistry final
    {
    public:
        void Register(NodeSchema schema)
        {
            ValidateNodeSchema(schema);
            const std::string key = schema.TypeKey;
            if (!m_Schemas.emplace(key, std::move(schema)).second)
                throw std::invalid_argument("Node schema is already registered: " + key);
        }

        [[nodiscard]] bool Contains(std::string_view typeKey) const noexcept
        {
            return m_Schemas.find(typeKey) != m_Schemas.end();
        }

        [[nodiscard]] const NodeSchema& Require(std::string_view typeKey) const
        {
            const auto found = m_Schemas.find(typeKey);
            if (found == m_Schemas.end()) throw std::out_of_range("Unknown node schema: " + std::string(typeKey));
            return found->second;
        }

        [[nodiscard]] std::vector<NodeSchema> Snapshot(DocumentKind kind) const
        {
            std::vector<NodeSchema> result;
            for (const auto& [key, schema] : m_Schemas)
                if (schema.Kind == kind) result.push_back(schema);
            return result;
        }

        [[nodiscard]] std::size_t Size() const noexcept { return m_Schemas.size(); }

    private:
        std::map<std::string, NodeSchema, std::less<>> m_Schemas;
    };
}
