module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Kairo.Editor.DocumentProjection;

import Kairo.Assets;
import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.CommandHistory;
import Kairo.Editor.DocumentSerialization;
import Kairo.Editor.DocumentTypes;

export namespace kairo::editor
{
    enum class DocumentSourceRole : std::uint8_t
    {
        Metadata,
        Node,
        Pin,
        Property,
        Connection
    };

    /// Byte-oriented span into canonical UTF-8 `.kdoc` source. Offsets match
    /// text editor buffers directly; line/column remain one-based for humans.
    /// Key identifies metadata/property names where no stable local ID exists.
    struct DocumentSourceSpan final
    {
        std::size_t Offset = 0u;
        std::size_t Length = 0u;
        std::size_t Line = 1u;
        std::size_t Column = 1u;
        DocumentSourceRole Role = DocumentSourceRole::Metadata;
        std::optional<NodeID> Node;
        std::optional<PinID> Pin;
        std::optional<PinID> InputPin;
        std::string Key;
    };

    class DocumentTextProjection final
    {
    public:
        [[nodiscard]] const std::string& Source() const noexcept { return m_Source; }
        [[nodiscard]] const std::vector<DocumentSourceSpan>& Spans() const noexcept { return m_Spans; }

        [[nodiscard]] std::optional<DocumentSourceSpan> At(std::size_t byteOffset) const
        {
            for (const DocumentSourceSpan& span : m_Spans)
                if (byteOffset >= span.Offset && byteOffset < span.Offset + span.Length) return span;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<DocumentSourceSpan> ForNode(NodeID id) const
        {
            for (const DocumentSourceSpan& span : m_Spans)
                if (span.Role == DocumentSourceRole::Node && span.Node == id) return span;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<DocumentSourceSpan> ForPin(PinID id) const
        {
            for (const DocumentSourceSpan& span : m_Spans)
                if (span.Role == DocumentSourceRole::Pin && span.Pin == id) return span;
            return std::nullopt;
        }

    private:
        std::string m_Source;
        std::vector<DocumentSourceSpan> m_Spans;

        friend DocumentTextProjection BuildDocumentTextProjection(const AuthoringDocument&);
    };

    namespace document_projection_detail
    {
        struct SourceLine final
        {
            std::size_t Offset = 0u;
            std::size_t Length = 0u;
            std::size_t Number = 1u;
        };

        [[nodiscard]] inline std::vector<SourceLine> IndexLines(std::string_view source)
        {
            std::vector<SourceLine> lines;
            std::size_t offset = 0u;
            std::size_t number = 1u;
            while (offset < source.size())
            {
                const std::size_t newline = source.find('\n', offset);
                const std::size_t end = newline == std::string_view::npos ? source.size() : newline;
                lines.push_back({ offset, end - offset, number++ });
                offset = newline == std::string_view::npos ? source.size() : newline + 1u;
            }
            return lines;
        }
    }

    /// Builds canonical text and exact structural navigation spans in one
    /// deterministic pass. The line walk mirrors SerializeDocument ordering;
    /// an internal mismatch throws rather than returning corrupt navigation.
    [[nodiscard]] inline DocumentTextProjection BuildDocumentTextProjection(
        const AuthoringDocument& document)
    {
        using namespace document_projection_detail;
        DocumentTextProjection projection;
        projection.m_Source = SerializeDocument(document);
        const auto lines = IndexLines(projection.m_Source);
        std::size_t cursor = 0u;
        const auto append = [&projection, &lines, &cursor](DocumentSourceRole role,
            std::optional<NodeID> node = std::nullopt, std::optional<PinID> pin = std::nullopt,
            std::optional<PinID> input = std::nullopt, std::string key = {})
        {
            if (cursor >= lines.size()) throw std::logic_error("Document projection line map is internally inconsistent.");
            const SourceLine& line = lines[cursor++];
            projection.m_Spans.push_back({ line.Offset, line.Length, line.Number, 1u,
                role, node, pin, input, std::move(key) });
        };

        append(DocumentSourceRole::Metadata, {}, {}, {}, "header");
        append(DocumentSourceRole::Metadata, {}, {}, {}, "id");
        append(DocumentSourceRole::Metadata, {}, {}, {}, "kind");
        append(DocumentSourceRole::Metadata, {}, {}, {}, "name");
        for (const NodeID nodeID : document.NodeIDs())
        {
            const DocumentNode& node = document.Node(nodeID);
            append(DocumentSourceRole::Node, nodeID);
            for (const DocumentPin& pin : node.Pins)
                append(DocumentSourceRole::Pin, nodeID, pin.ID, {}, pin.Key);
            for (const auto& [key, value] : node.Properties)
            {
                (void)value;
                append(DocumentSourceRole::Property, nodeID, {}, {}, key);
            }
            append(DocumentSourceRole::Node, nodeID, {}, {}, "end-node");
        }
        for (const DocumentConnection& connection : document.Connections())
            append(DocumentSourceRole::Connection, document.NodeForPin(connection.Output),
                connection.Output, connection.Input);
        if (cursor != lines.size())
            throw std::logic_error("Document projection did not consume its canonical source map.");
        return projection;
    }

    /// Parses an edited projection and prevents the text surface from changing
    /// persistent asset identity or domain kind. Rename, topology, values, and
    /// positions remain editable because they are normal authored state.
    [[nodiscard]] inline AuthoringDocument ParseDocumentProjection(std::string_view source,
        kairo::assets::AssetID expectedID, DocumentKind expectedKind)
    {
        AuthoringDocument candidate = ParseDocument(source);
        if (candidate.ID() != expectedID)
            throw std::invalid_argument("Document text cannot change persistent asset identity.");
        if (candidate.Kind() != expectedKind)
            throw std::invalid_argument("Document text cannot change its domain kind.");
        return candidate;
    }

    /// Replaces the entire constrained text projection through one no-throw
    /// state swap after successful parsing. Execute and Undo perform the same
    /// swap; sequential valid text edits merge naturally while the oldest
    /// pre-edit state remains in this command for exact undo.
    class ApplyDocumentTextCommand final : public EditorCommand
    {
    public:
        ApplyDocumentTextCommand(AuthoringDocument& document, std::string_view editedSource)
            : m_Document(&document),
              m_Alternate(ParseDocumentProjection(editedSource, document.ID(), document.Kind()))
        {
            if (SerializeDocument(document) == SerializeDocument(m_Alternate))
                throw std::invalid_argument("Document text edit does not change authored state.");
        }

        [[nodiscard]] std::string_view Name() const noexcept override { return "Edit Document Text"; }
        void Execute() override { m_Document->SwapContents(m_Alternate); }
        void Undo() override { m_Document->SwapContents(m_Alternate); }

        [[nodiscard]] bool TryMerge(EditorCommand& newer) noexcept override
        {
            const auto* edit = dynamic_cast<const ApplyDocumentTextCommand*>(&newer);
            // The newer command has already swapped its result into m_Document.
            // Keeping this command's alternate preserves the original undo
            // state; after Undo it receives the latest state for Redo.
            return edit != nullptr && edit->m_Document == m_Document;
        }

    private:
        AuthoringDocument* m_Document;
        AuthoringDocument m_Alternate;
    };
}
