module;

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentSerialization;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentTypes;
import Kairo.Editor.TextFormat;
import Kairo.EngineCore.Entity;
import Kairo.Foundation.Math;

export namespace kairo::editor
{
    class DocumentFormatError final : public std::runtime_error
    {
    public:
        DocumentFormatError(std::size_t line, std::size_t column, std::string message)
            : std::runtime_error("Kairo document " + std::to_string(line) + ":" +
                std::to_string(column) + ": " + message), Line(line), Column(column) {}

        std::size_t Line;
        std::size_t Column;
    };

    namespace document_format_detail
    {
        constexpr std::size_t MaximumDocumentBytes = 256u * 1024u * 1024u;
        constexpr std::size_t MaximumDocumentLineBytes = 128u * 1024u;

        [[nodiscard]] inline std::uint64_t ParseID(const FormatToken& token,
            std::size_t line, std::string_view role)
        {
            std::uint64_t value = 0u;
            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || value == 0u)
                throw DocumentFormatError(line, token.Column, std::string(role) + " must be a positive 64-bit integer");
            return value;
        }

        [[nodiscard]] inline std::int64_t ParseInteger(const FormatToken& token, std::size_t line)
        {
            std::int64_t value = 0;
            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size())
                throw DocumentFormatError(line, token.Column, "invalid signed integer value");
            return value;
        }

        [[nodiscard]] inline double ParseDouble(const FormatToken& token, std::size_t line)
        {
            double value = 0.0;
            const auto [end, error] = std::from_chars(token.Text.data(), token.Text.data() + token.Text.size(), value,
                std::chars_format::general);
            if (error != std::errc{} || end != token.Text.data() + token.Text.size() || !std::isfinite(value))
                throw DocumentFormatError(line, token.Column, "invalid finite floating-point value");
            return value;
        }

        [[nodiscard]] inline bool ParseBoolean(const FormatToken& token, std::size_t line)
        {
            if (token.Text == "true") return true;
            if (token.Text == "false") return false;
            throw DocumentFormatError(line, token.Column, "expected true or false");
        }

        [[nodiscard]] inline std::string FormatDouble(double value)
        {
            if (!std::isfinite(value)) throw std::invalid_argument("Cannot serialize a non-finite document value.");
            if (value == 0.0) return "0";
            char buffer[128]{};
            const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value,
                std::chars_format::general, std::numeric_limits<double>::max_digits10);
            if (error != std::errc{}) throw std::runtime_error("Cannot format document floating-point value.");
            return std::string(buffer, end);
        }

        [[nodiscard]] inline std::string SerializeValue(const DocumentValue& value)
        {
            value.Validate();
            switch (value.Type())
            {
                case ValueType::Flow: throw std::invalid_argument("Flow is not a serializable data value.");
                case ValueType::Boolean: return "bool " + std::string(value.Get<bool>() ? "true" : "false");
                case ValueType::Integer: return "int " + std::to_string(value.Get<std::int64_t>());
                case ValueType::Float: return "float " + FormatDouble(value.Get<double>());
                case ValueType::Vector2:
                {
                    const auto& vector = value.Get<kairo::foundation::math::Vec2d>();
                    return "vec2 " + FormatDouble(vector.x) + " " + FormatDouble(vector.y);
                }
                case ValueType::Vector3:
                {
                    const auto& vector = value.Get<kairo::foundation::math::Vec3d>();
                    return "vec3 " + FormatDouble(vector.x) + " " + FormatDouble(vector.y) + " " + FormatDouble(vector.z);
                }
                case ValueType::Vector4:
                {
                    const auto& vector = value.Get<kairo::foundation::math::Vec4d>();
                    return "vec4 " + FormatDouble(vector.x) + " " + FormatDouble(vector.y) + " " +
                        FormatDouble(vector.z) + " " + FormatDouble(vector.w);
                }
                case ValueType::String: return "string " + QuoteFormatText(value.Get<std::string>());
                case ValueType::Asset: return "asset " + value.Get<kairo::assets::AssetID>().ToString();
                case ValueType::Entity: return "entity " +
                    std::to_string(value.Get<kairo::engine::Entity>().Value);
            }
            throw std::logic_error("Unknown document value type.");
        }

        [[nodiscard]] inline std::size_t ValueTokenCount(ValueType type) noexcept
        {
            switch (type)
            {
                case ValueType::Vector2: return 3u;
                case ValueType::Vector3: return 4u;
                case ValueType::Vector4: return 5u;
                case ValueType::Flow: return 1u;
                default: return 2u;
            }
        }

        [[nodiscard]] inline DocumentValue ParseValue(const std::vector<FormatToken>& tokens,
            std::size_t start, std::size_t line)
        {
            if (start >= tokens.size()) throw DocumentFormatError(line, 1u, "missing typed value");
            const auto type = ParseValueType(tokens[start].Text);
            if (!type.has_value() || *type == ValueType::Flow)
                throw DocumentFormatError(line, tokens[start].Column, "unknown or non-data value type");
            const std::size_t expected = start + ValueTokenCount(*type);
            if (tokens.size() != expected)
            {
                const std::size_t column = tokens.size() > expected ? tokens[expected].Column : tokens[start].Column;
                throw DocumentFormatError(line, column, "typed value has the wrong component count");
            }
            try
            {
                switch (*type)
                {
                    case ValueType::Boolean: return DocumentValue(ParseBoolean(tokens[start + 1u], line));
                    case ValueType::Integer: return DocumentValue(ParseInteger(tokens[start + 1u], line));
                    case ValueType::Float: return DocumentValue(ParseDouble(tokens[start + 1u], line));
                    case ValueType::Vector2: return DocumentValue(kairo::foundation::math::Vec2d{
                        ParseDouble(tokens[start + 1u], line), ParseDouble(tokens[start + 2u], line) });
                    case ValueType::Vector3: return DocumentValue(kairo::foundation::math::Vec3d{
                        ParseDouble(tokens[start + 1u], line), ParseDouble(tokens[start + 2u], line),
                        ParseDouble(tokens[start + 3u], line) });
                    case ValueType::Vector4: return DocumentValue(kairo::foundation::math::Vec4d{
                        ParseDouble(tokens[start + 1u], line), ParseDouble(tokens[start + 2u], line),
                        ParseDouble(tokens[start + 3u], line), ParseDouble(tokens[start + 4u], line) });
                    case ValueType::String: return DocumentValue(tokens[start + 1u].Text);
                    case ValueType::Asset: return DocumentValue(kairo::assets::AssetID::Parse(tokens[start + 1u].Text));
                    case ValueType::Entity:
                    {
                        const std::uint64_t value = ParseID(tokens[start + 1u], line, "entity ID");
                        if (value > std::numeric_limits<std::uint32_t>::max())
                            throw DocumentFormatError(line, tokens[start + 1u].Column,
                                "entity ID exceeds the 32-bit scene identity range");
                        return DocumentValue(kairo::engine::Entity{ static_cast<std::uint32_t>(value) });
                    }
                    case ValueType::Flow: break;
                }
            }
            catch (const DocumentFormatError&) { throw; }
            catch (const std::exception& error)
            {
                throw DocumentFormatError(line, tokens[start + 1u].Column, error.what());
            }
            throw DocumentFormatError(line, tokens[start].Column, "unsupported typed value");
        }

        struct LocatedNode final { DocumentNode Value; std::size_t Line = 1u; };
        struct LocatedConnection final { DocumentConnection Value; std::size_t Line = 1u; std::size_t Column = 1u; };
        struct SourceLocation final { std::size_t Line = 1u; std::size_t Column = 1u; };
    }

    /// Canonical self-describing text. Pin contracts are persisted so documents
    /// remain loadable and lossless when a schema/plugin is unavailable.
    [[nodiscard]] inline std::string SerializeDocument(const AuthoringDocument& document)
    {
        using namespace document_format_detail;
        std::ostringstream output;
        output << "kairo-document 1\n";
        output << "id " << document.ID().ToString() << '\n';
        output << "kind " << kairo::editor::Name(document.Kind()) << '\n';
        output << "name " << QuoteFormatText(document.Name()) << '\n';
        for (const NodeID nodeID : document.NodeIDs())
        {
            const DocumentNode& node = document.Node(nodeID);
            output << "node " << node.ID.Value << ' ' << node.TypeKey << ' '
                << FormatDouble(node.Position.X) << ' ' << FormatDouble(node.Position.Y) << '\n';
            for (const DocumentPin& pin : node.Pins)
            {
                output << "pin " << pin.ID.Value << ' ' << pin.Key << ' ' << QuoteFormatText(pin.DisplayName) << ' '
                    << kairo::editor::Name(pin.Direction) << ' ' << kairo::editor::Name(pin.Type) << ' '
                    << kairo::editor::Name(pin.Cardinality) << ' ' << (pin.Required ? "true" : "false") << ' ';
                if (pin.DefaultValue.has_value()) output << "default " << SerializeValue(*pin.DefaultValue);
                else output << "no-default";
                output << '\n';
            }
            for (const auto& [key, value] : node.Properties)
                output << "property " << key << ' ' << SerializeValue(value) << '\n';
            output << "end-node\n";
        }
        for (const DocumentConnection& connection : document.Connections())
            output << "connect " << connection.Output.Value << ' ' << connection.Input.Value << '\n';
        return output.str();
    }

    [[nodiscard]] inline AuthoringDocument ParseDocument(std::string_view source)
    {
        using namespace document_format_detail;
        if (source.size() > MaximumDocumentBytes)
            throw std::length_error("Kairo document exceeds the 256 MiB safety limit.");

        bool headerSeen = false;
        bool bodySeen = false;
        std::optional<kairo::assets::AssetID> id;
        std::optional<DocumentKind> kind;
        std::optional<std::string> name;
        SourceLocation nameLocation;
        std::vector<LocatedNode> nodes;
        std::vector<LocatedConnection> connections;
        std::optional<LocatedNode> current;
        std::istringstream input{ std::string(source) };
        std::string lineText;
        std::size_t lineNumber = 0u;
        while (std::getline(input, lineText))
        {
            ++lineNumber;
            if (lineText.size() > MaximumDocumentLineBytes)
                throw DocumentFormatError(lineNumber, 1u, "line exceeds the 128 KiB safety limit");
            const auto tokens = TokenizeFormatLine<DocumentFormatError>(lineText, lineNumber);
            if (tokens.empty()) continue;
            if (!headerSeen)
            {
                RequireTokenCount<DocumentFormatError>(tokens, 2u, lineNumber, "kairo-document header");
                if (tokens[0].Text != "kairo-document")
                    throw DocumentFormatError(lineNumber, tokens[0].Column, "expected kairo-document header");
                if (tokens[1].Text != "1")
                    throw DocumentFormatError(lineNumber, tokens[1].Column, "unsupported document version");
                headerSeen = true;
                continue;
            }

            const std::string& statement = tokens[0].Text;
            if (current.has_value())
            {
                if (statement == "pin")
                {
                    if (tokens.size() < 9u) throw DocumentFormatError(lineNumber, 1u, "pin statement is incomplete");
                    const auto direction = ParsePinDirection(tokens[4].Text);
                    const auto type = ParseValueType(tokens[5].Text);
                    const auto cardinality = ParsePinCardinality(tokens[6].Text);
                    if (!direction.has_value()) throw DocumentFormatError(lineNumber, tokens[4].Column, "unknown pin direction");
                    if (!type.has_value()) throw DocumentFormatError(lineNumber, tokens[5].Column, "unknown pin value type");
                    if (!cardinality.has_value()) throw DocumentFormatError(lineNumber, tokens[6].Column, "unknown pin cardinality");
                    DocumentPin pin{ { ParseID(tokens[1], lineNumber, "pin ID") }, tokens[2].Text, tokens[3].Text,
                        *direction, *type, *cardinality, ParseBoolean(tokens[7], lineNumber), std::nullopt };
                    if (std::ranges::find(current->Value.Pins, pin.ID, &DocumentPin::ID) != current->Value.Pins.end())
                        throw DocumentFormatError(lineNumber, tokens[1].Column, "duplicate pin ID in node record");
                    if (std::ranges::find(current->Value.Pins, pin.Key, &DocumentPin::Key) != current->Value.Pins.end())
                        throw DocumentFormatError(lineNumber, tokens[2].Column, "duplicate pin key in node record");
                    if (tokens[8].Text == "default") pin.DefaultValue = ParseValue(tokens, 9u, lineNumber);
                    else
                    {
                        RequireTokenCount<DocumentFormatError>(tokens, 9u, lineNumber, "pin no-default");
                        if (tokens[8].Text != "no-default")
                            throw DocumentFormatError(lineNumber, tokens[8].Column, "expected default or no-default");
                    }
                    current->Value.Pins.push_back(std::move(pin));
                }
                else if (statement == "property")
                {
                    if (tokens.size() < 4u) throw DocumentFormatError(lineNumber, 1u, "property statement is incomplete");
                    DocumentValue value = ParseValue(tokens, 2u, lineNumber);
                    if (!current->Value.Properties.emplace(tokens[1].Text, std::move(value)).second)
                        throw DocumentFormatError(lineNumber, tokens[1].Column, "duplicate property key");
                }
                else if (statement == "end-node")
                {
                    RequireTokenCount<DocumentFormatError>(tokens, 1u, lineNumber, "end-node");
                    nodes.push_back(std::move(*current));
                    current.reset();
                }
                else throw DocumentFormatError(lineNumber, tokens[0].Column,
                    "unknown node statement '" + statement + "'");
                continue;
            }

            if (statement == "id")
            {
                if (bodySeen) throw DocumentFormatError(lineNumber, tokens[0].Column,
                    "document metadata cannot appear after node or connection records");
                RequireTokenCount<DocumentFormatError>(tokens, 2u, lineNumber, "id");
                if (id.has_value()) throw DocumentFormatError(lineNumber, tokens[0].Column, "duplicate document id");
                try { id = kairo::assets::AssetID::Parse(tokens[1].Text); }
                catch (const std::exception& error) { throw DocumentFormatError(lineNumber, tokens[1].Column, error.what()); }
                if (!id->IsValid()) throw DocumentFormatError(lineNumber, tokens[1].Column, "document id cannot be zero");
            }
            else if (statement == "kind")
            {
                if (bodySeen) throw DocumentFormatError(lineNumber, tokens[0].Column,
                    "document metadata cannot appear after node or connection records");
                RequireTokenCount<DocumentFormatError>(tokens, 2u, lineNumber, "kind");
                if (kind.has_value()) throw DocumentFormatError(lineNumber, tokens[0].Column, "duplicate document kind");
                kind = ParseDocumentKind(tokens[1].Text);
                if (!kind.has_value()) throw DocumentFormatError(lineNumber, tokens[1].Column, "unknown document kind");
            }
            else if (statement == "name")
            {
                if (bodySeen) throw DocumentFormatError(lineNumber, tokens[0].Column,
                    "document metadata cannot appear after node or connection records");
                RequireTokenCount<DocumentFormatError>(tokens, 2u, lineNumber, "name");
                if (name.has_value()) throw DocumentFormatError(lineNumber, tokens[0].Column, "duplicate document name");
                name = tokens[1].Text;
                nameLocation = { lineNumber, tokens[1].Column };
            }
            else if (statement == "node")
            {
                RequireTokenCount<DocumentFormatError>(tokens, 5u, lineNumber, "node");
                if (!id.has_value() || !kind.has_value() || !name.has_value())
                    throw DocumentFormatError(lineNumber, tokens[0].Column, "document metadata must precede nodes");
                bodySeen = true;
                LocatedNode node;
                node.Line = lineNumber;
                node.Value.ID = { ParseID(tokens[1], lineNumber, "node ID") };
                node.Value.TypeKey = tokens[2].Text;
                node.Value.Position = { ParseDouble(tokens[3], lineNumber), ParseDouble(tokens[4], lineNumber) };
                current = std::move(node);
            }
            else if (statement == "connect")
            {
                RequireTokenCount<DocumentFormatError>(tokens, 3u, lineNumber, "connect");
                if (!id.has_value() || !kind.has_value() || !name.has_value())
                    throw DocumentFormatError(lineNumber, tokens[0].Column,
                        "document metadata must precede connections");
                bodySeen = true;
                connections.push_back({ { { ParseID(tokens[1], lineNumber, "output pin ID") },
                    { ParseID(tokens[2], lineNumber, "input pin ID") } }, lineNumber, tokens[0].Column });
            }
            else throw DocumentFormatError(lineNumber, tokens[0].Column, "unknown statement '" + statement + "'");
        }

        if (!headerSeen) throw DocumentFormatError(1u, 1u, "missing kairo-document header");
        if (current.has_value()) throw DocumentFormatError(lineNumber + 1u, 1u, "node record is missing end-node");
        if (!id.has_value() || !kind.has_value() || !name.has_value())
            throw DocumentFormatError(lineNumber + 1u, 1u, "document requires id, kind, and name metadata");

        std::optional<AuthoringDocument> document;
        try { document.emplace(*id, *kind, std::move(*name)); }
        catch (const std::exception& error)
        {
            throw DocumentFormatError(nameLocation.Line, nameLocation.Column, error.what());
        }
        for (LocatedNode& node : nodes)
        {
            try { document->RestoreNode(std::move(node.Value)); }
            catch (const std::exception& error) { throw DocumentFormatError(node.Line, 1u, error.what()); }
        }
        for (const LocatedConnection& connection : connections)
        {
            try { document->Connect(connection.Value.Output, connection.Value.Input); }
            catch (const std::exception& error)
            {
                throw DocumentFormatError(connection.Line, connection.Column, error.what());
            }
        }
        return std::move(*document);
    }

    [[nodiscard]] inline AuthoringDocument LoadDocument(const std::filesystem::path& path)
    {
        return ParseDocument(LoadBoundedTextFile(path,
            document_format_detail::MaximumDocumentBytes, "Kairo authoring document"));
    }

    inline void SaveDocument(const std::filesystem::path& path, const AuthoringDocument& document)
    {
        SaveTextFileAtomically(path, SerializeDocument(document), "Kairo authoring document");
    }
}
