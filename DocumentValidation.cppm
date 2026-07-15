module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentValidation;

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;

export namespace kairo::editor
{
    enum class DiagnosticSeverity : std::uint8_t { Information, Warning, Error };

    struct DocumentDiagnostic final
    {
        DiagnosticSeverity Severity = DiagnosticSeverity::Error;
        std::string Code;
        std::string Message;
        std::optional<NodeID> Node;
        std::optional<PinID> Pin;

        friend bool operator==(const DocumentDiagnostic&, const DocumentDiagnostic&) = default;
    };

    [[nodiscard]] inline bool HasErrors(const std::vector<DocumentDiagnostic>& diagnostics) noexcept
    {
        return std::ranges::any_of(diagnostics, [](const DocumentDiagnostic& diagnostic)
        {
            return diagnostic.Severity == DiagnosticSeverity::Error;
        });
    }

    /// Input: authored document and the schema catalog defining its node types.
    /// Output: deterministic diagnostics ordered by node ID, pin order, then
    /// connection order. Task: detect schema drift and incomplete required data
    /// without mutating the document. Compilers must reject when HasErrors is true.
    [[nodiscard]] inline std::vector<DocumentDiagnostic> ValidateDocument(
        const AuthoringDocument& document, const DocumentSchemaRegistry& schemas)
    {
        std::vector<DocumentDiagnostic> diagnostics;
        for (const NodeID nodeID : document.NodeIDs())
        {
            const DocumentNode& node = document.Node(nodeID);
            if (!schemas.Contains(node.TypeKey))
            {
                diagnostics.push_back({ DiagnosticSeverity::Error, "unknown-node-type",
                    "No schema is registered for node type '" + node.TypeKey + "'.", nodeID, std::nullopt });
                continue;
            }

            const NodeSchema& schema = schemas.Require(node.TypeKey);
            if (schema.Kind != document.Kind())
                diagnostics.push_back({ DiagnosticSeverity::Error, "document-kind",
                    "Node type belongs to a different document kind.", nodeID, std::nullopt });
            if (schema.Pins.size() != node.Pins.size())
            {
                diagnostics.push_back({ DiagnosticSeverity::Error, "pin-schema-drift",
                    "Node pin count no longer matches its registered schema.", nodeID, std::nullopt });
                continue;
            }

            for (std::size_t index = 0u; index < node.Pins.size(); ++index)
            {
                const DocumentPin& pin = node.Pins[index];
                const PinSchema& expected = schema.Pins[index];
                if (pin.Key != expected.Key || pin.Direction != expected.Direction || pin.Type != expected.Type ||
                    pin.Cardinality != expected.Cardinality || pin.Required != expected.Required)
                {
                    diagnostics.push_back({ DiagnosticSeverity::Error, "pin-schema-drift",
                        "Pin contract no longer matches its registered schema.", nodeID, pin.ID });
                    continue;
                }
                if (pin.DefaultValue.has_value() && pin.DefaultValue->Type() != pin.Type)
                    diagnostics.push_back({ DiagnosticSeverity::Error, "pin-default-type",
                        "Pin default value has the wrong type.", nodeID, pin.ID });
                if (pin.Required && !document.IsConnected(pin.ID))
                    diagnostics.push_back({ DiagnosticSeverity::Error, "required-input",
                        "Required input '" + pin.DisplayName + "' is not connected.", nodeID, pin.ID });
                else if (pin.Direction == PinDirection::Input && pin.Type != ValueType::Flow &&
                    !document.IsConnected(pin.ID) && !pin.DefaultValue.has_value())
                    diagnostics.push_back({ DiagnosticSeverity::Error, "missing-input-value",
                        "Input '" + pin.DisplayName + "' has neither a connection nor a default value.", nodeID, pin.ID });
            }

            if (schema.PropertyDefaults.size() != node.Properties.size())
                diagnostics.push_back({ DiagnosticSeverity::Error, "property-schema-drift",
                    "Node property set no longer matches its registered schema.", nodeID, std::nullopt });
            else
            {
                for (const auto& [key, expected] : schema.PropertyDefaults)
                {
                    const auto found = node.Properties.find(key);
                    if (found == node.Properties.end() || found->second.Type() != expected.Type())
                        diagnostics.push_back({ DiagnosticSeverity::Error, "property-schema-drift",
                            "Property '" + key + "' no longer matches its registered schema.", nodeID, std::nullopt });
                }
            }
        }
        return diagnostics;
    }
}
