module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.LogicDocumentCompiler;

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentCompiler;
import Kairo.Editor.DocumentSchema;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.DocumentValidation;
import Kairo.EngineCore.Entity;
import Kairo.EngineCore.LogicBytecode;
import Kairo.Foundation.Math.Vector;

export namespace kairo::editor
{
    /// Compiles the core visual-logic schemas into EngineCore's bounded runtime
    /// bytecode. Authoring node IDs are used only for diagnostics; runtime
    /// artifacts contain no editor layout or mutable graph representation.
    class LogicDocumentCompiler final : public DocumentCompiler
    {
    public:
        [[nodiscard]] DocumentKind Kind() const noexcept override { return DocumentKind::Logic; }
        [[nodiscard]] std::string_view Target() const noexcept override
        {
            return "kairo.logic.bytecode-v1";
        }

        [[nodiscard]] DocumentCompilerOutput Compile(const AuthoringDocument& document,
            const DocumentSchemaRegistry&) const override
        {
            Builder builder(document);
            return builder.Build();
        }

    private:
        class Builder final
        {
        public:
            explicit Builder(const AuthoringDocument& document) : m_Document(document)
            {
                for (const DocumentConnection connection : document.Connections())
                {
                    m_Incoming.emplace(connection.Input, connection.Output);
                    m_Outgoing[connection.Output].push_back(connection.Input);
                }
            }

            [[nodiscard]] DocumentCompilerOutput Build()
            {
                ValidateFlowCycles();
                for (const NodeID id : m_Document.NodeIDs())
                {
                    const DocumentNode& node = m_Document.Node(id);
                    if (node.TypeKey == "kairo.logic.event-begin-play")
                        CompileEntry(id, kairo::engine::LogicEventKind::BeginPlay, {}, "then");
                    else if (node.TypeKey == "kairo.logic.event-tick")
                        CompileEntry(id, kairo::engine::LogicEventKind::Tick, {}, "then");
                    else if (node.TypeKey == "kairo.logic.input-action")
                    {
                        const std::string& action = Property(node, "action").Get<std::string>();
                        if (action.empty())
                            Error("invalid-action", "Input Action requires a non-empty action name.", id);
                        else
                        {
                            CompileEntry(id, kairo::engine::LogicEventKind::InputPressed, action, "pressed");
                            CompileEntry(id, kairo::engine::LogicEventKind::InputReleased, action, "released");
                        }
                    }
                    else if (node.TypeKey == "kairo.logic.collision-begin")
                        CompileEntry(id, kairo::engine::LogicEventKind::CollisionBegin, {}, "then");
                    else if (node.TypeKey == "kairo.logic.collision-end")
                        CompileEntry(id, kairo::engine::LogicEventKind::CollisionEnd, {}, "then");
                }
                if (m_Program.Entries.empty())
                    Error("missing-entry", "Logic document requires at least one event entry node.");

                DocumentCompilerOutput output;
                output.Diagnostics = std::move(m_Diagnostics);
                if (!HasErrors(output.Diagnostics))
                {
                    m_Program.Validate();
                    output.Payload = kairo::engine::SerializeLogicProgram(m_Program);
                }
                return output;
            }

        private:
            const AuthoringDocument& m_Document;
            kairo::engine::LogicProgram m_Program;
            std::vector<DocumentDiagnostic> m_Diagnostics;
            std::map<PinID, PinID> m_Incoming;
            std::map<PinID, std::vector<PinID>> m_Outgoing;
            std::set<NodeID> m_ActiveFlow;
            std::set<PinID> m_ActiveData;

            void ValidateFlowCycles()
            {
                std::map<NodeID, std::uint8_t> state;
                const auto visit = [&](const auto& self, NodeID id) -> void
                {
                    if (state[id] == 2u) return;
                    if (state[id] == 1u)
                    {
                        Error("flow-cycle", "Flow cycles are not supported by the V1 bounded logic compiler.", id);
                        return;
                    }
                    state[id] = 1u;
                    const DocumentNode& node = m_Document.Node(id);
                    for (const DocumentPin& pin : node.Pins)
                    {
                        if (pin.Direction != PinDirection::Output || pin.Type != ValueType::Flow) continue;
                        const auto outgoing = m_Outgoing.find(pin.ID);
                        if (outgoing == m_Outgoing.end()) continue;
                        for (const PinID target : outgoing->second)
                            self(self, m_Document.NodeForPin(target));
                    }
                    state[id] = 2u;
                };
                for (const NodeID id : m_Document.NodeIDs()) visit(visit, id);
            }

            void CompileEntry(NodeID event, kairo::engine::LogicEventKind kind,
                std::string action, std::string_view output)
            {
                const auto offset = static_cast<std::uint32_t>(m_Program.Instructions.size());
                m_Program.Entries.push_back({ kind, std::move(action), offset });
                CompileFlowOutput(m_Document.Node(event), output);
                Emit({ kairo::engine::LogicOpcode::Halt });
            }

            void CompileFlowOutput(const DocumentNode& node, std::string_view key)
            {
                const DocumentPin& pin = Pin(node, key);
                const auto found = m_Outgoing.find(pin.ID);
                if (found == m_Outgoing.end()) return;
                for (const PinID input : found->second) CompileFlowNode(m_Document.NodeForPin(input));
            }

            void CompileFlowNode(NodeID id)
            {
                if (!m_ActiveFlow.insert(id).second)
                {
                    Error("flow-cycle", "Flow cycles are not supported by the V1 bounded logic compiler.", id);
                    return;
                }
                const DocumentNode& node = m_Document.Node(id);
                if (node.TypeKey == "kairo.logic.print")
                {
                    const DocumentPin& message = Pin(node, "message");
                    if (m_Incoming.contains(message.ID))
                        Error("dynamic-string", "Print currently requires a literal message.", id, message.ID);
                    else
                    {
                        const auto constant = AddString(Default(message).Get<std::string>());
                        Emit({ kairo::engine::LogicOpcode::Print, constant });
                    }
                    CompileFlowOutput(node, "then");
                }
                else if (node.TypeKey == "kairo.logic.set-position")
                {
                    const auto entity = CompileInput(node, "entity", ValueType::Entity);
                    const auto position = CompileInput(node, "position", ValueType::Vector3);
                    if (entity && position)
                        Emit({ kairo::engine::LogicOpcode::SetEntityPosition, *entity, *position });
                    CompileFlowOutput(node, "then");
                }
                else if (node.TypeKey == "kairo.logic.apply-impulse")
                {
                    const auto entity = CompileInput(node, "entity", ValueType::Entity);
                    const auto impulse = CompileInput(node, "impulse", ValueType::Vector3);
                    if (entity && impulse)
                        Emit({ kairo::engine::LogicOpcode::ApplyEntityImpulse, *entity, *impulse });
                    CompileFlowOutput(node, "then");
                }
                else if (node.TypeKey == "kairo.logic.branch") CompileBranch(node);
                else Error("unsupported-flow-node",
                    "This logic node does not yet have executable runtime semantics.", id);
                m_ActiveFlow.erase(id);
            }

            void CompileBranch(const DocumentNode& node)
            {
                const auto condition = CompileInput(node, "condition", ValueType::Boolean);
                if (!condition) return;
                const std::size_t branch = m_Program.Instructions.size();
                Emit({ kairo::engine::LogicOpcode::JumpIfFalse, *condition, 0u });
                CompileFlowOutput(node, "true");
                const std::size_t skipFalse = m_Program.Instructions.size();
                Emit({ kairo::engine::LogicOpcode::Jump, 0u });
                m_Program.Instructions[branch].B = static_cast<std::uint32_t>(m_Program.Instructions.size());
                CompileFlowOutput(node, "false");
                m_Program.Instructions[skipFalse].A = static_cast<std::uint32_t>(m_Program.Instructions.size());
            }

            [[nodiscard]] std::optional<std::uint32_t> CompileInput(const DocumentNode& node,
                std::string_view key, ValueType expected)
            {
                const DocumentPin& input = Pin(node, key);
                const auto source = m_Incoming.find(input.ID);
                if (source == m_Incoming.end())
                {
                    if (!input.DefaultValue)
                    {
                        Error("missing-runtime-input", "Runtime input is not connected.", node.ID, input.ID);
                        return std::nullopt;
                    }
                    return CompileConstant(*input.DefaultValue, node.ID, input.ID);
                }
                if (!m_ActiveData.insert(source->second).second)
                {
                    Error("data-cycle", "Data dependency contains a cycle.", node.ID, input.ID);
                    return std::nullopt;
                }
                const DocumentNode& producer = m_Document.Node(m_Document.NodeForPin(source->second));
                const DocumentPin& output = m_Document.Pin(source->second);
                std::optional<std::uint32_t> result;
                if (output.Type != expected)
                    Error("runtime-type", "Connected runtime value has an incompatible type.", node.ID, input.ID);
                else if (producer.TypeKey == "kairo.logic.add-float" && output.Key == "result")
                {
                    const auto a = CompileInput(producer, "a", ValueType::Float);
                    const auto b = CompileInput(producer, "b", ValueType::Float);
                    if (a && b)
                    {
                        result = AllocateRegister();
                        Emit({ kairo::engine::LogicOpcode::AddFloat, *result, *a, *b });
                    }
                }
                else if (producer.TypeKey == "kairo.logic.event-tick" && output.Key == "delta_seconds") result = 0u;
                else if (producer.TypeKey == "kairo.logic.input-action" && output.Key == "value") result = 1u;
                else if ((producer.TypeKey == "kairo.logic.collision-begin" ||
                    producer.TypeKey == "kairo.logic.collision-end") && output.Key == "other") result = 2u;
                else if (producer.TypeKey == "kairo.logic.entity-reference" && output.Key == "entity")
                    result = CompileConstant(Property(producer, "entity"), producer.ID, output.ID);
                else if (producer.TypeKey == "kairo.logic.vector3" && output.Key == "value")
                    result = CompileConstant(Property(producer, "value"), producer.ID, output.ID);
                else Error("unsupported-data-node",
                    "This data output does not yet have executable runtime semantics.", producer.ID, output.ID);
                m_ActiveData.erase(source->second);
                return result;
            }

            [[nodiscard]] std::optional<std::uint32_t> CompileConstant(const DocumentValue& value,
                NodeID node, PinID pin)
            {
                const std::uint32_t reg = AllocateRegister();
                switch (value.Type())
                {
                    case ValueType::Boolean:
                        Emit({ kairo::engine::LogicOpcode::LoadBoolean, reg,
                            value.Get<bool>() ? 1u : 0u }); return reg;
                    case ValueType::Float:
                        Emit({ kairo::engine::LogicOpcode::LoadFloat, reg,
                            AddFloat(value.Get<double>()) }); return reg;
                    case ValueType::Vector3:
                        Emit({ kairo::engine::LogicOpcode::LoadVector3, reg,
                            AddVector(value.Get<kairo::foundation::math::Vec3d>()) }); return reg;
                    case ValueType::Entity:
                        Emit({ kairo::engine::LogicOpcode::LoadEntity, reg,
                            AddEntity(value.Get<kairo::engine::Entity>()) }); return reg;
                    default:
                        Error("unsupported-constant", "Value type cannot be emitted by logic bytecode.", node, pin);
                        return std::nullopt;
                }
            }

            [[nodiscard]] static const DocumentPin& Pin(const DocumentNode& node, std::string_view key)
            {
                const auto found = std::ranges::find(node.Pins, key, &DocumentPin::Key);
                if (found == node.Pins.end()) throw std::logic_error("Validated node is missing pin '" + std::string(key) + "'.");
                return *found;
            }

            [[nodiscard]] static const DocumentValue& Default(const DocumentPin& pin)
            {
                if (!pin.DefaultValue) throw std::logic_error("Validated input has no default value.");
                return *pin.DefaultValue;
            }

            [[nodiscard]] static const DocumentValue& Property(const DocumentNode& node, std::string_view key)
            {
                const auto found = node.Properties.find(key);
                if (found == node.Properties.end()) throw std::logic_error("Validated node is missing property '" + std::string(key) + "'.");
                return found->second;
            }

            [[nodiscard]] std::uint32_t AllocateRegister()
            {
                if (m_Program.RegisterCount == kairo::engine::LogicProgram::MaximumRegisters)
                    throw std::length_error("Logic graph requires too many runtime registers.");
                return m_Program.RegisterCount++;
            }
            [[nodiscard]] std::uint32_t AddString(std::string value)
            { m_Program.Strings.push_back(std::move(value)); return static_cast<std::uint32_t>(m_Program.Strings.size() - 1u); }
            [[nodiscard]] std::uint32_t AddFloat(double value)
            { m_Program.Floats.push_back(value); return static_cast<std::uint32_t>(m_Program.Floats.size() - 1u); }
            [[nodiscard]] std::uint32_t AddVector(kairo::foundation::math::Vec3d value)
            { m_Program.Vectors.push_back(value); return static_cast<std::uint32_t>(m_Program.Vectors.size() - 1u); }
            [[nodiscard]] std::uint32_t AddEntity(kairo::engine::Entity value)
            { m_Program.Entities.push_back(value); return static_cast<std::uint32_t>(m_Program.Entities.size() - 1u); }
            void Emit(kairo::engine::LogicInstruction instruction)
            {
                if (m_Program.Instructions.size() >= kairo::engine::LogicProgram::MaximumInstructions)
                    throw std::length_error("Logic graph exceeds the runtime instruction limit.");
                m_Program.Instructions.push_back(instruction);
            }
            void Error(std::string code, std::string message,
                std::optional<NodeID> node = std::nullopt, std::optional<PinID> pin = std::nullopt)
            { m_Diagnostics.push_back({ DiagnosticSeverity::Error, std::move(code), std::move(message), node, pin }); }
        };
    };
}
