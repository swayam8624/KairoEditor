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
#include <utility>
#include <vector>

export module Kairo.Editor.GraphCanvas;

import Kairo.Editor.AuthoringDocument;
import Kairo.Editor.DocumentTypes;
import Kairo.Foundation.Math;

export namespace kairo::editor
{
    using GraphPoint = kairo::foundation::math::Vec2d;

    inline constexpr double MinimumGraphZoom = 0.10;
    inline constexpr double MaximumGraphZoom = 4.00;
    inline constexpr double GraphSpatialCellSize = 512.0;
    inline constexpr std::size_t MaximumGraphLayoutPins = 1'000'000u;

    struct GraphRect final
    {
        GraphPoint Minimum{};
        GraphPoint Maximum{};

        [[nodiscard]] double Width() const noexcept { return Maximum.x - Minimum.x; }
        [[nodiscard]] double Height() const noexcept { return Maximum.y - Minimum.y; }
        [[nodiscard]] GraphPoint Center() const noexcept
        {
            return { (Minimum.x + Maximum.x) * 0.5, (Minimum.y + Maximum.y) * 0.5 };
        }
        [[nodiscard]] bool Contains(GraphPoint point) const noexcept
        {
            return point.x >= Minimum.x && point.x <= Maximum.x &&
                point.y >= Minimum.y && point.y <= Maximum.y;
        }
        [[nodiscard]] bool Intersects(const GraphRect& other) const noexcept
        {
            return Maximum.x >= other.Minimum.x && Minimum.x <= other.Maximum.x &&
                Maximum.y >= other.Minimum.y && Minimum.y <= other.Maximum.y;
        }
        friend bool operator==(const GraphRect&, const GraphRect&) = default;
    };

    [[nodiscard]] inline GraphRect NormalizeGraphRect(GraphPoint first, GraphPoint second)
    {
        return { { std::min(first.x, second.x), std::min(first.y, second.y) },
            { std::max(first.x, second.x), std::max(first.y, second.y) } };
    }

    inline void ValidateGraphRect(const GraphRect& rectangle, bool requireArea = true)
    {
        constexpr double limit = 1.0e9;
        const auto finite = [](double value) { return std::isfinite(value); };
        if (!finite(rectangle.Minimum.x) || !finite(rectangle.Minimum.y) ||
            !finite(rectangle.Maximum.x) || !finite(rectangle.Maximum.y))
            throw std::invalid_argument("Graph rectangle coordinates must be finite.");
        if (std::abs(rectangle.Minimum.x) > limit || std::abs(rectangle.Minimum.y) > limit ||
            std::abs(rectangle.Maximum.x) > limit || std::abs(rectangle.Maximum.y) > limit)
            throw std::invalid_argument("Graph rectangle coordinates must remain within +/-1e9 units.");
        if (rectangle.Minimum.x > rectangle.Maximum.x || rectangle.Minimum.y > rectangle.Maximum.y ||
            (requireArea && (rectangle.Width() <= 0.0 || rectangle.Height() <= 0.0)))
            throw std::invalid_argument("Graph rectangle minimum/maximum ordering is invalid.");
    }

    /// Maps document coordinates to one panel's screen rectangle. DocumentOrigin
    /// is the document point visible at ScreenOrigin. Cursor-centered zoom keeps
    /// the document point under the cursor invariant, preventing disorienting
    /// jumps while navigating dense graphs.
    class GraphViewport final
    {
    public:
        [[nodiscard]] GraphPoint DocumentOrigin() const noexcept { return m_DocumentOrigin; }
        [[nodiscard]] double Zoom() const noexcept { return m_Zoom; }

        [[nodiscard]] GraphPoint ToScreen(GraphPoint document, GraphPoint screenOrigin) const noexcept
        {
            return { screenOrigin.x + (document.x - m_DocumentOrigin.x) * m_Zoom,
                screenOrigin.y + (document.y - m_DocumentOrigin.y) * m_Zoom };
        }

        [[nodiscard]] GraphPoint ToDocument(GraphPoint screen, GraphPoint screenOrigin) const noexcept
        {
            return { m_DocumentOrigin.x + (screen.x - screenOrigin.x) / m_Zoom,
                m_DocumentOrigin.y + (screen.y - screenOrigin.y) / m_Zoom };
        }

        void SetDocumentOrigin(GraphPoint origin)
        {
            ValidatePoint(origin, "Graph viewport origin");
            m_DocumentOrigin = origin;
        }

        void PanByScreenDelta(GraphPoint delta)
        {
            ValidatePoint(delta, "Graph pan delta");
            SetDocumentOrigin({ m_DocumentOrigin.x - delta.x / m_Zoom,
                m_DocumentOrigin.y - delta.y / m_Zoom });
        }

        void ZoomAt(double requestedZoom, GraphPoint anchorScreen, GraphPoint screenOrigin)
        {
            ValidatePoint(anchorScreen, "Graph zoom anchor");
            ValidatePoint(screenOrigin, "Graph panel origin");
            if (!std::isfinite(requestedZoom) || requestedZoom <= 0.0)
                throw std::invalid_argument("Graph zoom must be finite and positive.");
            const GraphPoint anchorDocument = ToDocument(anchorScreen, screenOrigin);
            const double candidateZoom = std::clamp(requestedZoom, MinimumGraphZoom, MaximumGraphZoom);
            const GraphPoint candidateOrigin{
                anchorDocument.x - (anchorScreen.x - screenOrigin.x) / candidateZoom,
                anchorDocument.y - (anchorScreen.y - screenOrigin.y) / candidateZoom };
            ValidatePoint(candidateOrigin, "Graph viewport origin");
            m_Zoom = candidateZoom;
            m_DocumentOrigin = candidateOrigin;
        }

        void ZoomBy(double factor, GraphPoint anchorScreen, GraphPoint screenOrigin)
        {
            if (!std::isfinite(factor) || factor <= 0.0)
                throw std::invalid_argument("Graph zoom factor must be finite and positive.");
            ZoomAt(m_Zoom * factor, anchorScreen, screenOrigin);
        }

        /// Frames document bounds inside a positive screen size. Padding is in
        /// screen pixels and may consume no more than half either dimension.
        void Frame(const GraphRect& documentBounds, GraphPoint screenSize, double padding = 48.0)
        {
            ValidateGraphRect(documentBounds);
            ValidatePoint(screenSize, "Graph panel size");
            if (screenSize.x <= 0.0 || screenSize.y <= 0.0 || !std::isfinite(padding) || padding < 0.0 ||
                padding * 2.0 >= screenSize.x || padding * 2.0 >= screenSize.y)
                throw std::invalid_argument("Graph frame size/padding is invalid.");
            const double candidateZoom = std::clamp(std::min((screenSize.x - padding * 2.0) / documentBounds.Width(),
                (screenSize.y - padding * 2.0) / documentBounds.Height()),
                MinimumGraphZoom, MaximumGraphZoom);
            const GraphPoint candidateOrigin{
                documentBounds.Center().x - screenSize.x / (2.0 * candidateZoom),
                documentBounds.Center().y - screenSize.y / (2.0 * candidateZoom) };
            ValidatePoint(candidateOrigin, "Graph viewport origin");
            m_Zoom = candidateZoom;
            m_DocumentOrigin = candidateOrigin;
        }

    private:
        GraphPoint m_DocumentOrigin{};
        double m_Zoom = 1.0;

        static void ValidatePoint(GraphPoint point, const char* role)
        {
            constexpr double limit = 1.0e12;
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                std::abs(point.x) > limit || std::abs(point.y) > limit)
                throw std::invalid_argument(std::string(role) + " must be finite and within +/-1e12.");
        }
    };

    struct GraphPinLayout final
    {
        PinID ID;
        GraphPoint Position{};
        friend bool operator==(const GraphPinLayout&, const GraphPinLayout&) = default;
    };

    struct GraphNodeLayout final
    {
        NodeID ID;
        GraphRect Bounds;
        double HeaderHeight = 28.0;
        std::uint64_t ZOrder = 0u;
        std::vector<GraphPinLayout> Pins;
        friend bool operator==(const GraphNodeLayout&, const GraphNodeLayout&) = default;
    };

    struct GraphCell final
    {
        std::int64_t X = 0;
        std::int64_t Y = 0;
        friend constexpr auto operator<=>(const GraphCell&, const GraphCell&) noexcept = default;
    };

    /// Uniform-grid index for visible-node culling and pointer queries. Rebuild
    /// is strong-guarantee: a malformed layout leaves the previous index intact.
    /// Query output is ID-sorted and independent of insertion or hash order.
    class GraphSpatialIndex final
    {
    public:
        void Rebuild(const std::vector<GraphNodeLayout>& layouts)
        {
            GraphSpatialIndex candidate;
            for (const GraphNodeLayout& layout : layouts) candidate.Insert(layout);
            *this = std::move(candidate);
        }

        /// Adds identity validation against the source document while allowing
        /// collapsed/LOD layouts to omit hidden pins. Every supplied pin must
        /// still exist and belong to its declared node.
        void Rebuild(const AuthoringDocument& document,
            const std::vector<GraphNodeLayout>& layouts)
        {
            for (const GraphNodeLayout& layout : layouts)
            {
                (void)document.Node(layout.ID);
                for (const GraphPinLayout& pin : layout.Pins)
                {
                    (void)document.Pin(pin.ID);
                    if (document.NodeForPin(pin.ID) != layout.ID)
                        throw std::invalid_argument("Graph pin layout belongs to a different document node.");
                }
            }
            Rebuild(layouts);
        }

        [[nodiscard]] std::size_t NodeCount() const noexcept { return m_Nodes.size(); }
        [[nodiscard]] std::size_t PinCount() const noexcept { return m_Pins.size(); }

        [[nodiscard]] const GraphNodeLayout& Layout(NodeID id) const
        {
            const auto found = m_Nodes.find(id);
            if (found == m_Nodes.end()) throw std::out_of_range("Graph layout does not contain this node.");
            return found->second;
        }

        [[nodiscard]] std::vector<NodeID> Query(const GraphRect& area) const
        {
            ValidateGraphRect(area, false);
            std::set<NodeID> candidates;
            const CellRange cells = CellsFor(area);
            if (cells.Count() > MaximumQueryCells)
            {
                for (const auto& [id, layout] : m_Nodes)
                    if (layout.Bounds.Intersects(area)) candidates.insert(id);
            }
            else
            {
                ForEachCell(cells, [this, &candidates](GraphCell cell)
                {
                    const auto found = m_NodeCells.find(cell);
                    if (found == m_NodeCells.end()) return;
                    candidates.insert(found->second.begin(), found->second.end());
                });
                std::erase_if(candidates, [this, &area](NodeID id)
                {
                    return !m_Nodes.at(id).Bounds.Intersects(area);
                });
            }
            return { candidates.begin(), candidates.end() };
        }

        [[nodiscard]] std::optional<NodeID> HitNode(GraphPoint point) const
        {
            const GraphRect query{ point, point };
            std::optional<NodeID> result;
            std::uint64_t bestOrder = 0u;
            for (const NodeID id : Query(query))
            {
                const auto& layout = m_Nodes.at(id);
                if (!layout.Bounds.Contains(point)) continue;
                if (!result.has_value() || layout.ZOrder > bestOrder ||
                    (layout.ZOrder == bestOrder && id > *result))
                {
                    result = id;
                    bestOrder = layout.ZOrder;
                }
            }
            return result;
        }

        [[nodiscard]] std::optional<PinID> HitPin(GraphPoint point, double radius) const
        {
            if (!std::isfinite(radius) || radius <= 0.0 || radius > 256.0)
                throw std::invalid_argument("Graph pin hit radius must be in (0, 256].");
            ValidateGraphRect({ point, point }, false);
            const GraphRect area{ { point.x - radius, point.y - radius },
                { point.x + radius, point.y + radius } };
            std::set<PinID> candidates;
            ForEachCell(CellsFor(area), [this, &candidates](GraphCell cell)
            {
                const auto found = m_PinCells.find(cell);
                if (found == m_PinCells.end()) return;
                candidates.insert(found->second.begin(), found->second.end());
            });
            std::optional<PinID> result;
            double bestDistance = radius * radius;
            for (const PinID id : candidates)
            {
                const GraphPoint pin = m_Pins.at(id).second;
                const double dx = pin.x - point.x;
                const double dy = pin.y - point.y;
                const double distance = dx * dx + dy * dy;
                if (distance <= bestDistance && (!result.has_value() || distance < bestDistance || id < *result))
                {
                    result = id;
                    bestDistance = distance;
                }
            }
            return result;
        }

    private:
        static constexpr std::uint64_t MaximumCellsPerNode = 4096u;
        static constexpr std::uint64_t MaximumQueryCells = 65'536u;

        struct CellRange final
        {
            GraphCell Minimum;
            GraphCell Maximum;
            [[nodiscard]] std::uint64_t Count() const noexcept
            {
                const auto width = static_cast<std::uint64_t>(Maximum.X - Minimum.X) + 1u;
                const auto height = static_cast<std::uint64_t>(Maximum.Y - Minimum.Y) + 1u;
                return width > std::numeric_limits<std::uint64_t>::max() / height
                    ? std::numeric_limits<std::uint64_t>::max() : width * height;
            }
        };

        std::map<NodeID, GraphNodeLayout> m_Nodes;
        std::map<PinID, std::pair<NodeID, GraphPoint>> m_Pins;
        std::map<GraphCell, std::vector<NodeID>> m_NodeCells;
        std::map<GraphCell, std::vector<PinID>> m_PinCells;

        static GraphCell CellFor(GraphPoint point) noexcept
        {
            return { static_cast<std::int64_t>(std::floor(point.x / GraphSpatialCellSize)),
                static_cast<std::int64_t>(std::floor(point.y / GraphSpatialCellSize)) };
        }

        static CellRange CellsFor(const GraphRect& rectangle) noexcept
        {
            return { CellFor(rectangle.Minimum), CellFor(rectangle.Maximum) };
        }

        template<class Function>
        static void ForEachCell(const CellRange& range, Function&& function)
        {
            for (std::int64_t y = range.Minimum.Y;; ++y)
            {
                for (std::int64_t x = range.Minimum.X;; ++x)
                {
                    function(GraphCell{ x, y });
                    if (x == range.Maximum.X) break;
                }
                if (y == range.Maximum.Y) break;
            }
        }

        void Insert(const GraphNodeLayout& layout)
        {
            if (!layout.ID) throw std::invalid_argument("Graph node layout requires a non-zero ID.");
            ValidateGraphRect(layout.Bounds);
            if (!std::isfinite(layout.HeaderHeight) || layout.HeaderHeight <= 0.0 ||
                layout.HeaderHeight > layout.Bounds.Height())
                throw std::invalid_argument("Graph node header height is invalid.");
            if (layout.Pins.size() > 256u)
                throw std::length_error("Graph node layout exceeds 256 pins.");
            const CellRange nodeCells = CellsFor(layout.Bounds);
            if (nodeCells.Count() > MaximumCellsPerNode)
                throw std::length_error("Graph node layout spans too many spatial cells.");
            if (!m_Nodes.emplace(layout.ID, layout).second)
                throw std::invalid_argument("Graph layout contains a duplicate node ID.");
            ForEachCell(nodeCells, [this, id = layout.ID](GraphCell cell)
            {
                m_NodeCells[cell].push_back(id);
            });

            if (m_Pins.size() + layout.Pins.size() > MaximumGraphLayoutPins)
                throw std::length_error("Graph layout exceeds its pin safety limit.");
            for (const GraphPinLayout& pin : layout.Pins)
            {
                if (!pin.ID) throw std::invalid_argument("Graph pin layout requires a non-zero ID.");
                ValidateGraphRect({ pin.Position, pin.Position }, false);
                if (!m_Pins.emplace(pin.ID, std::pair{ layout.ID, pin.Position }).second)
                    throw std::invalid_argument("Graph layout contains a duplicate pin ID.");
                m_PinCells[CellFor(pin.Position)].push_back(pin.ID);
            }
        }
    };

    enum class GraphSelectionMode : std::uint8_t { Replace, Add, Subtract, Toggle };

    class GraphSelection final
    {
    public:
        [[nodiscard]] bool Contains(NodeID id) const noexcept { return m_Selected.contains(id); }
        [[nodiscard]] std::size_t Size() const noexcept { return m_Selected.size(); }
        [[nodiscard]] std::optional<NodeID> Primary() const noexcept { return m_Primary; }
        [[nodiscard]] std::vector<NodeID> Snapshot() const { return { m_Selected.begin(), m_Selected.end() }; }

        void Clear() noexcept { m_Selected.clear(); m_Primary.reset(); }

        void Apply(NodeID id, GraphSelectionMode mode)
        {
            if (!id) throw std::invalid_argument("Cannot select a zero node ID.");
            if (mode == GraphSelectionMode::Replace) Clear();
            if (mode == GraphSelectionMode::Subtract ||
                (mode == GraphSelectionMode::Toggle && m_Selected.contains(id)))
            {
                m_Selected.erase(id);
                if (m_Primary == id) m_Primary = m_Selected.empty() ? std::nullopt : std::optional(*m_Selected.rbegin());
                return;
            }
            m_Selected.insert(id);
            m_Primary = id;
        }

        void ApplyMarquee(const GraphSpatialIndex& index, const GraphRect& area, GraphSelectionMode mode)
        {
            const auto hits = index.Query(area);
            if (mode == GraphSelectionMode::Replace) Clear();
            for (const NodeID id : hits) Apply(id, mode == GraphSelectionMode::Replace
                ? GraphSelectionMode::Add : mode);
        }

        void RemoveMissing(const AuthoringDocument& document) noexcept
        {
            std::erase_if(m_Selected, [&document](NodeID id) { return !document.Contains(id); });
            if (m_Primary.has_value() && !m_Selected.contains(*m_Primary))
                m_Primary = m_Selected.empty() ? std::nullopt : std::optional(*m_Selected.rbegin());
        }

    private:
        std::set<NodeID> m_Selected;
        std::optional<NodeID> m_Primary;
    };

    /// Tracks one connection gesture without mutating the document. Completion
    /// returns an ordered output/input pair only when AuthoringDocument accepts
    /// it; the UI then submits ConnectDocumentPinsCommand to shared history.
    class GraphConnectionDrag final
    {
    public:
        [[nodiscard]] bool Active() const noexcept { return m_Source.has_value(); }
        [[nodiscard]] std::optional<PinID> Source() const noexcept { return m_Source; }

        void Begin(const AuthoringDocument& document, PinID source)
        {
            (void)document.Pin(source);
            m_Source = source;
        }

        [[nodiscard]] ConnectionCheck Preview(const AuthoringDocument& document, PinID target) const
        {
            if (!m_Source.has_value()) return { false, "inactive-drag", "No graph connection drag is active." };
            const DocumentPin& source = document.Pin(*m_Source);
            return source.Direction == PinDirection::Output
                ? document.CanConnect(*m_Source, target)
                : document.CanConnect(target, *m_Source);
        }

        [[nodiscard]] std::optional<DocumentConnection> Complete(
            const AuthoringDocument& document, PinID target)
        {
            if (!m_Source.has_value()) return std::nullopt;
            const PinID sourceID = *m_Source;
            m_Source.reset();
            const DocumentPin& source = document.Pin(sourceID);
            const DocumentConnection connection = source.Direction == PinDirection::Output
                ? DocumentConnection{ sourceID, target } : DocumentConnection{ target, sourceID };
            return document.CanConnect(connection.Output, connection.Input).Allowed
                ? std::optional(connection) : std::nullopt;
        }

        void Cancel() noexcept { m_Source.reset(); }

    private:
        std::optional<PinID> m_Source;
    };
}
