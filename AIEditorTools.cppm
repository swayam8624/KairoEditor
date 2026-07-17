module;

#include <algorithm>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

export module Kairo.Editor.AITools;

import Kairo.AI;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.ProjectSession;
import Kairo.Editor.SceneCommands;
import Kairo.EngineCore;

export namespace kairo::editor
{
    struct AIEditorToolPreview final
    {
        std::string CallID;
        std::string ToolName;
        std::string Summary;
        kairo::ai::ToolCapability Capability = kairo::ai::ToolCapability::ReadProject;
        bool RequiresApproval = false;
    };

    struct AIEditorToolAudit final
    {
        kairo::ai::ToolCall Call;
        kairo::ai::InteractionMode Mode = kairo::ai::InteractionMode::Ask;
        kairo::ai::AuthorizationDecision Decision = kairo::ai::AuthorizationDecision::DeniedByMode;
        std::string Summary;
        bool Invoked = false;
    };

    /// Adapts model-proposed scene tools to the editor's existing command
    /// journal. Preview performs full argument and entity validation without
    /// mutation. Execute passes through KairoAI authorization first, then uses
    /// the same reversible commands as hierarchy and viewport interactions.
    class AIEditorTools final
    {
    public:
        AIEditorTools(ProjectSession& project, CommandHistory& history)
            : m_Project(&project), m_History(&history)
        {
            m_Registry.Register({ "project.list_entities", kairo::ai::ToolCapability::ReadProject,
                "List stable scene entity IDs and names." },
                [this](std::string_view arguments) { return ListEntities(arguments); });
            m_Registry.Register({ "scene.create_entity", kairo::ai::ToolCapability::MutateProject,
                "Create one undoable empty scene entity." },
                [this](std::string_view arguments) { return CreateEntity(arguments); });
            m_Registry.Register({ "scene.delete_entity", kairo::ai::ToolCapability::MutateProject,
                "Delete one entity subtree through the undo journal." },
                [this](std::string_view arguments) { return DeleteEntity(arguments); });
        }

        [[nodiscard]] static std::vector<kairo::ai::ToolDefinition> Definitions()
        {
            return {
                { "project.list_entities", "List stable scene entity IDs and names.",
                    R"({"type":"object","properties":{},"additionalProperties":false})" },
                { "scene.create_entity", "Create one undoable empty scene entity.",
                    R"({"type":"object","properties":{"name":{"type":"string","minLength":1,"maxLength":256}},"required":["name"],"additionalProperties":false})" },
                { "scene.delete_entity", "Delete one entity subtree through the undo journal.",
                    R"({"type":"object","properties":{"entity":{"type":"integer","minimum":1}},"required":["entity"],"additionalProperties":false})" }
            };
        }

        [[nodiscard]] AIEditorToolPreview Preview(const kairo::ai::ToolCall& call) const
        {
            if (call.ID.empty()) throw std::invalid_argument("AI editor tool call ID cannot be empty.");
            if (call.Name == "project.list_entities")
            {
                (void)ParseObject(call.Arguments, {});
                return { call.ID, call.Name, "Read the current scene entity list.",
                    kairo::ai::ToolCapability::ReadProject, false };
            }
            if (call.Name == "scene.create_entity")
            {
                const auto object = ParseObject(call.Arguments, { "name" });
                const std::string name = RequireName(object);
                return { call.ID, call.Name, "Create entity '" + name + "'.",
                    kairo::ai::ToolCapability::MutateProject, true };
            }
            if (call.Name == "scene.delete_entity")
            {
                const auto object = ParseObject(call.Arguments, { "entity" });
                const auto entity = RequireEntity(object);
                if (!m_Project->Scene().Contains(entity))
                    throw std::out_of_range("AI delete preview references an entity that does not exist.");
                return { call.ID, call.Name,
                    "Delete entity '" + m_Project->Scene().Name(entity).Value + "' and its descendants.",
                    kairo::ai::ToolCapability::MutateProject, true };
            }
            throw std::out_of_range("AI requested an unknown editor tool.");
        }

        [[nodiscard]] kairo::ai::ToolExecution Execute(kairo::ai::InteractionMode mode,
            const kairo::ai::ToolCall& call,
            const std::optional<kairo::ai::ToolApproval>& approval = std::nullopt)
        {
            const AIEditorToolPreview preview = Preview(call);
            kairo::ai::ToolExecution result = m_Registry.Execute(mode, call, approval);
            m_Audit.push_back({ call, mode, result.AuthorizationResult.Decision,
                preview.Summary, result.Invoked });
            return result;
        }

        [[nodiscard]] const std::vector<AIEditorToolAudit>& Audit() const noexcept { return m_Audit; }

    private:
        using Json = nlohmann::json;
        ProjectSession* m_Project;
        CommandHistory* m_History;
        kairo::ai::ToolRegistry m_Registry;
        std::vector<AIEditorToolAudit> m_Audit;

        [[nodiscard]] static Json ParseObject(std::string_view text,
            std::initializer_list<std::string_view> allowed)
        {
            Json object;
            try { object = Json::parse(text); }
            catch (const Json::exception& error)
            { throw std::invalid_argument(std::string("AI editor tool arguments are invalid JSON: ") + error.what()); }
            if (!object.is_object()) throw std::invalid_argument("AI editor tool arguments must be a JSON object.");
            for (auto item = object.begin(); item != object.end(); ++item)
            {
                if (std::ranges::find(allowed, std::string_view(item.key())) == allowed.end())
                    throw std::invalid_argument("AI editor tool arguments contain unknown field '" + item.key() + "'.");
            }
            return object;
        }

        [[nodiscard]] static std::string RequireName(const Json& object)
        {
            if (!object.contains("name") || !object.at("name").is_string())
                throw std::invalid_argument("AI create entity requires a string name.");
            std::string name = object.at("name").get<std::string>();
            if (name.empty() || name.size() > 256u)
                throw std::invalid_argument("AI entity name must contain between 1 and 256 bytes.");
            return name;
        }

        [[nodiscard]] static kairo::engine::Entity RequireEntity(const Json& object)
        {
            if (!object.contains("entity") || !object.at("entity").is_number_unsigned())
                throw std::invalid_argument("AI delete entity requires a positive unsigned entity ID.");
            const std::uint64_t value = object.at("entity").get<std::uint64_t>();
            if (value == 0u || value > std::numeric_limits<std::uint32_t>::max())
                throw std::invalid_argument("AI entity ID is outside the persistent 32-bit range.");
            return kairo::engine::Entity{ static_cast<std::uint32_t>(value) };
        }

        [[nodiscard]] std::string ListEntities(std::string_view arguments) const
        {
            (void)ParseObject(arguments, {});
            Json output = Json::array();
            for (const auto entity : m_Project->Scene().Entities())
                output.push_back({ { "entity", entity.Value },
                    { "name", m_Project->Scene().Name(entity).Value } });
            return output.dump();
        }

        [[nodiscard]] std::string CreateEntity(std::string_view arguments)
        {
            const std::string name = RequireName(ParseObject(arguments, { "name" }));
            auto command = std::make_unique<CreateEntityCommand>(*m_Project, name);
            auto* created = command.get();
            m_History->Execute(std::move(command));
            return Json{ { "entity", created->CreatedEntity().Value } }.dump();
        }

        [[nodiscard]] std::string DeleteEntity(std::string_view arguments)
        {
            const auto entity = RequireEntity(ParseObject(arguments, { "entity" }));
            if (!m_Project->Scene().Contains(entity))
                throw std::out_of_range("AI delete tool references an entity that does not exist.");
            m_History->Execute(std::make_unique<DeleteEntityCommand>(*m_Project, entity));
            return Json{ { "deleted", entity.Value } }.dump();
        }
    };
}
