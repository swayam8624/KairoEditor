module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

export module Kairo.Editor.Diagnostics;

import Kairo.Assets.AssetID;
import Kairo.EngineCore.Entity;

export namespace kairo::editor
{
    enum class DiagnosticSeverity : std::uint8_t
    {
        Information = 1u,
        Warning = 2u,
        Error = 3u
    };

    struct ProjectDiagnosticTarget final
    {
        std::filesystem::path Path;
        friend bool operator==(const ProjectDiagnosticTarget&, const ProjectDiagnosticTarget&) = default;
    };
    struct AssetDiagnosticTarget final
    {
        kairo::assets::AssetID Asset;
        friend bool operator==(const AssetDiagnosticTarget&, const AssetDiagnosticTarget&) = default;
    };
    struct EntityDiagnosticTarget final
    {
        kairo::engine::Entity Entity;
        std::string Component;
        friend bool operator==(const EntityDiagnosticTarget&, const EntityDiagnosticTarget&) = default;
    };
    struct GraphDiagnosticTarget final
    {
        kairo::assets::AssetID Document;
        std::uint64_t Node = 0u;
        std::uint64_t Pin = 0u;
        friend bool operator==(const GraphDiagnosticTarget&, const GraphDiagnosticTarget&) = default;
    };
    struct SourceDiagnosticTarget final
    {
        std::filesystem::path Path;
        std::size_t Line = 0u;
        std::size_t Column = 0u;
        friend bool operator==(const SourceDiagnosticTarget&, const SourceDiagnosticTarget&) = default;
    };

    using DiagnosticTarget = std::variant<std::monostate, ProjectDiagnosticTarget,
        AssetDiagnosticTarget, EntityDiagnosticTarget, GraphDiagnosticTarget,
        SourceDiagnosticTarget>;

    struct EditorDiagnostic final
    {
        std::string Producer;
        std::string Code;
        DiagnosticSeverity Severity = DiagnosticSeverity::Information;
        std::string Message;
        DiagnosticTarget Target;

        friend bool operator==(const EditorDiagnostic&, const EditorDiagnostic&) = default;
    };

    class DiagnosticStore final
    {
    public:
        void ReplaceProducer(std::string producer, std::vector<EditorDiagnostic> diagnostics)
        {
            if (producer.empty())
                throw std::invalid_argument("Diagnostic producer cannot be empty.");
            std::erase_if(m_Diagnostics,
                [&producer](const EditorDiagnostic& diagnostic)
                { return diagnostic.Producer == producer; });
            for (EditorDiagnostic& diagnostic : diagnostics)
            {
                if (!diagnostic.Producer.empty() && diagnostic.Producer != producer)
                    throw std::invalid_argument("Diagnostic producer replacement is inconsistent.");
                if (diagnostic.Code.empty() || diagnostic.Message.empty())
                    throw std::invalid_argument("Diagnostics require a code and message.");
                diagnostic.Producer = producer;
                m_Diagnostics.push_back(std::move(diagnostic));
            }
            Sort();
        }

        void ClearProducer(const std::string& producer)
        {
            std::erase_if(m_Diagnostics,
                [&producer](const EditorDiagnostic& diagnostic)
                { return diagnostic.Producer == producer; });
        }

        [[nodiscard]] const std::vector<EditorDiagnostic>& Snapshot() const noexcept
        { return m_Diagnostics; }

        [[nodiscard]] std::size_t Count(DiagnosticSeverity severity) const noexcept
        {
            return static_cast<std::size_t>(std::ranges::count(
                m_Diagnostics, severity, &EditorDiagnostic::Severity));
        }

        [[nodiscard]] std::optional<DiagnosticTarget> NextNavigable(
            std::size_t afterIndex = 0u) const
        {
            if (m_Diagnostics.empty()) return std::nullopt;
            for (std::size_t step = 0u; step < m_Diagnostics.size(); ++step)
            {
                const std::size_t index = (afterIndex + step) % m_Diagnostics.size();
                if (!std::holds_alternative<std::monostate>(m_Diagnostics[index].Target))
                    return m_Diagnostics[index].Target;
            }
            return std::nullopt;
        }

    private:
        std::vector<EditorDiagnostic> m_Diagnostics;

        void Sort()
        {
            std::ranges::stable_sort(m_Diagnostics,
                [](const EditorDiagnostic& left, const EditorDiagnostic& right)
                {
                    if (left.Severity != right.Severity)
                        return static_cast<std::uint8_t>(left.Severity) >
                            static_cast<std::uint8_t>(right.Severity);
                    if (left.Producer != right.Producer)
                        return left.Producer < right.Producer;
                    if (left.Code != right.Code) return left.Code < right.Code;
                    return left.Message < right.Message;
                });
        }
    };
}
