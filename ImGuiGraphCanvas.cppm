module;

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.ImGuiGraphCanvas;

import Kairo.Assets;
import Kairo.Editor;

export namespace kairo::editor
{
    /// Custom-drawn authoring canvas backed by the UI-independent graph kernel.
    /// ImGui supplies clipping and pointer/keyboard input; document topology,
    /// compatibility, selection, viewport transforms, and undoable mutations
    /// remain in KairoEditor core.
    class ImGuiGraphCanvas final
    {
    public:
        explicit ImGuiGraphCanvas(const DocumentSchemaRegistry& schemas) noexcept : m_Schemas(&schemas) {}

        void BeginFrame() noexcept { m_Focused = false; }
        [[nodiscard]] bool Focused() const noexcept { return m_Focused; }

        void QueueAction(EditorAction action)
        {
            m_PendingActions.push_back(action);
        }

        void Draw(ProjectSession& project, DocumentViewState& view,
            kairo::assets::AssetID documentID)
        {
            ImGui::PushID(documentID.ToString().c_str());
            const bool addAtCenter = ImGui::Button("+", { 28.0f, 28.0f });
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Add node");
            ImGui::PopID();

            const std::string childID = "##GraphCanvas-" + documentID.ToString();
            const ImVec2 available = ImGui::GetContentRegionAvail();
            if (available.x < 32.0f || available.y < 32.0f) return;
            if (!ImGui::BeginChild(childID.c_str(), available, ImGuiChildFlags_Borders,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
            {
                ImGui::EndChild();
                return;
            }

            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const ImVec2 size = ImGui::GetContentRegionAvail();
            ImGui::InvisibleButton("##GraphInput", size,
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
            m_Focused = focused;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->PushClipRect(origin, { origin.x + size.x, origin.y + size.y }, true);

            const AuthoringDocument& document = project.Document(documentID);
            view.Synchronize(document);
            std::vector<GraphNodeLayout> layouts = BuildLayouts(document);
            GraphSpatialIndex index;
            index.Rebuild(document, layouts);
            HandleNavigation(view.Viewport(), layouts, hovered, focused, origin, size, mouse);
            const bool addAtPointer = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
            if (addAtCenter || addAtPointer)
            {
                const GraphPoint screen = addAtCenter
                    ? GraphPoint{ origin.x + size.x * 0.5, origin.y + size.y * 0.5 }
                    : GraphPoint{ mouse.x, mouse.y };
                OpenPalette(documentID, view.Viewport().ToDocument(screen, { origin.x, origin.y }));
            }
            DrawGrid(*draw, view.Viewport(), origin, size);
            DrawConnections(*draw, document, index, view.Viewport(), origin);
            DrawNodes(*draw, document, index, view.Selection(), view.Viewport(), origin, size);
            HandleQueuedActions(project, view, documentID, layouts, origin, size, mouse, hovered);
            HandleInteraction(project, view, documentID, index, hovered, origin, mouse);
            DrawGestureOverlay(*draw, project.Document(documentID), view, index, origin, mouse);
            DrawPalette(project, view, documentID);

            draw->PopClipRect();
            ImGui::EndChild();
        }

        [[nodiscard]] std::optional<std::string> TakeError()
        {
            auto error = std::move(m_Error);
            m_Error.reset();
            return error;
        }

    private:
        static constexpr double NodeWidth = 260.0;
        static constexpr double HeaderHeight = 32.0;
        static constexpr double PinRowHeight = 24.0;
        static constexpr double NodeBottomPadding = 12.0;
        static constexpr double PinHitRadiusPixels = 9.0;

        struct NodeDragState final
        {
            kairo::assets::AssetID Document;
            GraphPoint MouseStart{};
            std::vector<DocumentNodePositionEdit> StartPositions;
        };

        struct MarqueeState final
        {
            kairo::assets::AssetID Document;
            GraphPoint Start{};
            GraphSelectionMode Mode = GraphSelectionMode::Replace;
            bool Moved = false;
        };

        struct NodePaletteState final
        {
            kairo::assets::AssetID Document;
            CanvasPosition Position;
            std::array<char, 257u> Search{};
        };

        const DocumentSchemaRegistry* m_Schemas;
        std::optional<NodeDragState> m_NodeDrag;
        std::optional<MarqueeState> m_Marquee;
        std::optional<NodePaletteState> m_Palette;
        std::optional<std::string> m_Error;
        std::vector<EditorAction> m_PendingActions;
        bool m_Focused = false;

        void HandleQueuedActions(ProjectSession& project, DocumentViewState& view,
            kairo::assets::AssetID documentID, const std::vector<GraphNodeLayout>& layouts,
            ImVec2 origin, ImVec2 size, ImVec2 mouse, bool hovered)
        {
            for (const EditorAction action : std::exchange(m_PendingActions, {}))
            {
                if (action == EditorAction::GraphAddNode)
                {
                    const GraphPoint screen = hovered ? GraphPoint{ mouse.x, mouse.y }
                        : GraphPoint{ origin.x + size.x * 0.5, origin.y + size.y * 0.5 };
                    OpenPalette(documentID, view.Viewport().ToDocument(screen, { origin.x, origin.y }));
                }
                else if (action == EditorAction::GraphDelete && view.Selection().Size() != 0u)
                {
                    RunOperation([&]
                    {
                        auto& document = project.EditDocument(documentID);
                        project.DocumentHistory().Execute(std::make_unique<RemoveDocumentNodesCommand>(
                            document, view.Selection().Snapshot()));
                        view.Selection().Clear();
                        view.Synchronize(document);
                    });
                }
                else if ((action == EditorAction::GraphFrameAll ||
                    action == EditorAction::GraphFrameSelection) && !layouts.empty())
                {
                    std::vector<GraphNodeLayout> framed;
                    for (const auto& layout : layouts)
                        if (action == EditorAction::GraphFrameAll || view.Selection().Contains(layout.ID))
                            framed.push_back(layout);
                    if (framed.empty()) continue;
                    GraphRect bounds = framed.front().Bounds;
                    for (const auto& layout : framed)
                    {
                        bounds.Minimum.x = std::min(bounds.Minimum.x, layout.Bounds.Minimum.x);
                        bounds.Minimum.y = std::min(bounds.Minimum.y, layout.Bounds.Minimum.y);
                        bounds.Maximum.x = std::max(bounds.Maximum.x, layout.Bounds.Maximum.x);
                        bounds.Maximum.y = std::max(bounds.Maximum.y, layout.Bounds.Maximum.y);
                    }
                    view.Viewport().Frame(bounds, { size.x, size.y }, 56.0);
                }
            }
        }

        void OpenPalette(kairo::assets::AssetID document, GraphPoint position)
        {
            m_Palette = NodePaletteState{ document, { position.x, position.y }, {} };
            ImGui::OpenPopup("Add Node##KairoGraphPalette");
        }

        void DrawPalette(ProjectSession& project, DocumentViewState& view,
            kairo::assets::AssetID documentID)
        {
            if (!ImGui::BeginPopup("Add Node##KairoGraphPalette")) return;
            if (!m_Palette.has_value() || m_Palette->Document != documentID)
            {
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }

            ImGui::SetNextItemWidth(360.0f);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##NodeSearch", "Search node types", m_Palette->Search.data(),
                m_Palette->Search.size());
            ImGui::Separator();
            const auto schemas = m_Schemas->Search(project.Document(documentID).Kind(), m_Palette->Search.data());
            if (schemas.empty()) ImGui::TextDisabled("No matching node types");

            std::map<std::string, std::vector<NodeSchema>, std::less<>> categories;
            for (const NodeSchema& schema : schemas) categories[schema.Category].push_back(schema);
            if (ImGui::BeginChild("##NodeResults", { 360.0f, 280.0f }, ImGuiChildFlags_None))
            {
                bool createdNode = false;
                for (const auto& [category, entries] : categories)
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("%s", category.c_str());
                    for (const NodeSchema& schema : entries)
                    {
                        const std::string label = schema.DisplayName + "###" + schema.TypeKey;
                        if (ImGui::Selectable(label.c_str()))
                        {
                            const CanvasPosition position = m_Palette->Position;
                            if (RunOperation([&]
                            {
                                auto& mutableDocument = project.EditDocument(documentID);
                                auto command = std::make_unique<AddDocumentNodeCommand>(
                                    mutableDocument, schema, position);
                                auto* created = command.get();
                                project.DocumentHistory().Execute(std::move(command));
                                view.Selection().Apply(created->CreatedNode(), GraphSelectionMode::Replace);
                                view.Synchronize(mutableDocument);
                            }))
                            {
                                m_Palette.reset();
                                ImGui::CloseCurrentPopup();
                                createdNode = true;
                                break;
                            }
                        }
                    }
                    if (createdNode) break;
                }
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }

        [[nodiscard]] static std::vector<GraphNodeLayout> BuildLayouts(
            const AuthoringDocument& document)
        {
            std::vector<GraphNodeLayout> layouts;
            layouts.reserve(document.NodeCount());
            for (const NodeID nodeID : document.NodeIDs())
            {
                const DocumentNode& node = document.Node(nodeID);
                const std::size_t rows = std::max<std::size_t>(node.Pins.size(), 1u);
                const double height = HeaderHeight + static_cast<double>(rows) * PinRowHeight +
                    static_cast<double>(node.Properties.size()) * 18.0 + NodeBottomPadding;
                GraphNodeLayout layout;
                layout.ID = nodeID;
                layout.Bounds = { { node.Position.X, node.Position.Y },
                    { node.Position.X + NodeWidth, node.Position.Y + height } };
                layout.HeaderHeight = HeaderHeight;
                layout.ZOrder = nodeID.Value;
                layout.Pins.reserve(node.Pins.size());
                for (std::size_t row = 0u; row < node.Pins.size(); ++row)
                {
                    const DocumentPin& pin = node.Pins[row];
                    layout.Pins.push_back({ pin.ID,
                        { pin.Direction == PinDirection::Input ? layout.Bounds.Minimum.x : layout.Bounds.Maximum.x,
                          layout.Bounds.Minimum.y + HeaderHeight + PinRowHeight * (static_cast<double>(row) + 0.5) } });
                }
                layouts.push_back(std::move(layout));
            }
            return layouts;
        }

        static void HandleNavigation(GraphViewport& viewport,
            const std::vector<GraphNodeLayout>& layouts, bool hovered, bool focused,
            ImVec2 origin, ImVec2 size, ImVec2 mouse)
        {
            const auto point = [](ImVec2 value) { return GraphPoint{ value.x, value.y }; };
            if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                viewport.PanByScreenDelta({ delta.x, delta.y });
            }
            if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
                viewport.ZoomBy(std::pow(1.15, static_cast<double>(ImGui::GetIO().MouseWheel)),
                    point(mouse), point(origin));

            (void)focused;
        }

        static void DrawGrid(ImDrawList& draw, const GraphViewport& viewport,
            ImVec2 origin, ImVec2 size)
        {
            double spacing = 32.0 * viewport.Zoom();
            while (spacing < 16.0) spacing *= 2.0;
            const GraphPoint documentOrigin = viewport.DocumentOrigin();
            const float xOffset = static_cast<float>(std::fmod(-documentOrigin.x * viewport.Zoom(), spacing));
            const float yOffset = static_cast<float>(std::fmod(-documentOrigin.y * viewport.Zoom(), spacing));
            const ImU32 minor = IM_COL32(54, 59, 68, 115);
            for (float x = xOffset; x < size.x; x += static_cast<float>(spacing))
                draw.AddLine({ origin.x + x, origin.y }, { origin.x + x, origin.y + size.y }, minor);
            for (float y = yOffset; y < size.y; y += static_cast<float>(spacing))
                draw.AddLine({ origin.x, origin.y + y }, { origin.x + size.x, origin.y + y }, minor);
        }

        static void DrawConnections(ImDrawList& draw, const AuthoringDocument& document,
            const GraphSpatialIndex& index, const GraphViewport& viewport, ImVec2 origin)
        {
            for (const DocumentConnection& connection : document.Connections())
            {
                const GraphPoint output = PinPosition(index, document, connection.Output);
                const GraphPoint input = PinPosition(index, document, connection.Input);
                DrawConnection(draw, ToScreen(viewport, output, origin), ToScreen(viewport, input, origin),
                    PinColor(document.Pin(connection.Output).Type), 2.5f);
            }
        }

        static void DrawNodes(ImDrawList& draw, const AuthoringDocument& document,
            const GraphSpatialIndex& index, const GraphSelection& selection,
            const GraphViewport& viewport, ImVec2 origin, ImVec2 size)
        {
            const GraphPoint visibleMinimum = viewport.ToDocument({ origin.x, origin.y }, { origin.x, origin.y });
            const GraphPoint visibleMaximum = viewport.ToDocument(
                { origin.x + size.x, origin.y + size.y }, { origin.x, origin.y });
            for (const NodeID nodeID : index.Query(NormalizeGraphRect(visibleMinimum, visibleMaximum)))
            {
                const DocumentNode& node = document.Node(nodeID);
                const GraphNodeLayout& layout = index.Layout(nodeID);
                const ImVec2 minimum = ToScreen(viewport, layout.Bounds.Minimum, origin);
                const ImVec2 maximum = ToScreen(viewport, layout.Bounds.Maximum, origin);
                const float rounding = 5.0f;
                draw.AddRectFilled(minimum, maximum, IM_COL32(28, 31, 37, 250), rounding);
                const ImVec2 headerMaximum{ maximum.x,
                    minimum.y + static_cast<float>(layout.HeaderHeight * viewport.Zoom()) };
                draw.AddRectFilled(minimum, headerMaximum, IM_COL32(40, 72, 101, 255), rounding,
                    ImDrawFlags_RoundCornersTop);
                draw.AddRect(minimum, maximum, selection.Contains(nodeID)
                    ? IM_COL32(75, 169, 235, 255) : IM_COL32(75, 81, 92, 255), rounding, 0,
                    selection.Contains(nodeID) ? 2.0f : 1.0f);

                if (viewport.Zoom() >= 0.45)
                {
                    const ImVec4 clip{ minimum.x + 8.0f, minimum.y, maximum.x - 8.0f, headerMaximum.y };
                    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                        { minimum.x + 10.0f, minimum.y + 8.0f }, IM_COL32(235, 239, 244, 255),
                        node.TypeKey.c_str(), nullptr, 0.0f, &clip);
                }
                for (std::size_t row = 0u; row < node.Pins.size(); ++row)
                {
                    const DocumentPin& pin = node.Pins[row];
                    const ImVec2 position = ToScreen(viewport, layout.Pins[row].Position, origin);
                    const float radius = static_cast<float>(std::clamp(5.0 * viewport.Zoom(), 3.5, 7.0));
                    draw.AddCircleFilled(position, radius, PinColor(pin.Type));
                    draw.AddCircle(position, radius, IM_COL32(230, 234, 240, 210), 12, 1.0f);
                    if (viewport.Zoom() >= 0.65)
                    {
                        const ImVec2 textSize = ImGui::CalcTextSize(pin.DisplayName.c_str());
                        const float y = position.y - textSize.y * 0.5f;
                        const float x = pin.Direction == PinDirection::Input
                            ? position.x + 10.0f : position.x - 10.0f - textSize.x;
                        draw.AddText({ x, y }, IM_COL32(190, 198, 208, 255), pin.DisplayName.c_str());
                    }
                }
            }
        }

        void HandleInteraction(ProjectSession& project, DocumentViewState& view,
            kairo::assets::AssetID documentID, const GraphSpatialIndex& index,
            bool hovered, ImVec2 origin, ImVec2 mouse)
        {
            const GraphPoint mouseDocument = view.Viewport().ToDocument(
                { mouse.x, mouse.y }, { origin.x, origin.y });
            const double pinRadius = PinHitRadiusPixels / view.Viewport().Zoom();
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                if (const auto pin = index.HitPin(mouseDocument, pinRadius); pin.has_value())
                {
                    view.ConnectionDrag().Begin(project.Document(documentID), *pin);
                    m_NodeDrag.reset();
                    m_Marquee.reset();
                    return;
                }
                if (const auto node = index.HitNode(mouseDocument); node.has_value())
                {
                    const GraphSelectionMode mode = SelectionMode();
                    if (!view.Selection().Contains(*node) || mode != GraphSelectionMode::Replace)
                        view.Selection().Apply(*node, mode);
                    NodeDragState drag{ documentID, mouseDocument, {} };
                    for (const NodeID selected : view.Selection().Snapshot())
                        drag.StartPositions.push_back({ selected, project.Document(documentID).Node(selected).Position });
                    m_NodeDrag = std::move(drag);
                    m_Marquee.reset();
                    return;
                }
                if (SelectionMode() == GraphSelectionMode::Replace) view.Selection().Clear();
                m_Marquee = MarqueeState{ documentID, mouseDocument, SelectionMode(), false };
                m_NodeDrag.reset();
            }

            if (m_Marquee.has_value() && m_Marquee->Document == documentID &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                const double dx = (mouseDocument.x - m_Marquee->Start.x) * view.Viewport().Zoom();
                const double dy = (mouseDocument.y - m_Marquee->Start.y) * view.Viewport().Zoom();
                m_Marquee->Moved = m_Marquee->Moved || dx * dx + dy * dy >= 9.0;
            }

            if (m_NodeDrag.has_value() && m_NodeDrag->Document == documentID &&
                ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f))
            {
                const GraphPoint delta{ mouseDocument.x - m_NodeDrag->MouseStart.x,
                    mouseDocument.y - m_NodeDrag->MouseStart.y };
                std::vector<DocumentNodePositionEdit> edits = m_NodeDrag->StartPositions;
                for (auto& edit : edits)
                {
                    edit.Position.X += delta.x;
                    edit.Position.Y += delta.y;
                }
                RunOperation([&]
                {
                    auto& document = project.EditDocument(documentID);
                    project.DocumentHistory().Execute(
                        std::make_unique<SetDocumentNodePositionsCommand>(document, std::move(edits)));
                    view.Synchronize(document);
                });
            }

            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                if (view.ConnectionDrag().Active())
                {
                    if (const auto target = index.HitPin(mouseDocument, pinRadius); target.has_value())
                    {
                        const auto connection = view.ConnectionDrag().Complete(project.Document(documentID), *target);
                        if (connection.has_value()) RunOperation([&]
                        {
                            auto& document = project.EditDocument(documentID);
                            project.DocumentHistory().Execute(std::make_unique<ConnectDocumentPinsCommand>(
                                document, connection->Output, connection->Input));
                            view.Synchronize(document);
                        });
                    }
                    else view.ConnectionDrag().Cancel();
                }
                if (m_Marquee.has_value() && m_Marquee->Document == documentID &&
                    m_Marquee->Moved)
                    view.Selection().ApplyMarquee(index,
                        NormalizeGraphRect(m_Marquee->Start, mouseDocument), m_Marquee->Mode);
                m_NodeDrag.reset();
                m_Marquee.reset();
            }
        }

        void DrawGestureOverlay(ImDrawList& draw, const AuthoringDocument& document,
            const DocumentViewState& view, const GraphSpatialIndex& index, ImVec2 origin, ImVec2 mouse)
        {
            if (view.ConnectionDrag().Active())
            {
                const PinID source = *view.ConnectionDrag().Source();
                const ImVec2 start = ToScreen(view.Viewport(), PinPosition(index, document, source), origin);
                const ImVec2 end = mouse;
                ImU32 color = PinColor(document.Pin(source).Type);
                const GraphPoint mouseDocument = view.Viewport().ToDocument(
                    { mouse.x, mouse.y }, { origin.x, origin.y });
                if (const auto target = index.HitPin(mouseDocument,
                    PinHitRadiusPixels / view.Viewport().Zoom()); target.has_value() &&
                    !view.ConnectionDrag().Preview(document, *target).Allowed)
                    color = IM_COL32(222, 82, 82, 255);
                DrawConnection(draw, start, end, color, 2.5f);
            }
            if (m_Marquee.has_value() && m_Marquee->Document == document.ID() && m_Marquee->Moved)
            {
                const ImVec2 start = ToScreen(view.Viewport(), m_Marquee->Start, origin);
                draw.AddRectFilled({ std::min(start.x, mouse.x), std::min(start.y, mouse.y) },
                    { std::max(start.x, mouse.x), std::max(start.y, mouse.y) },
                    IM_COL32(54, 137, 202, 35));
                draw.AddRect({ std::min(start.x, mouse.x), std::min(start.y, mouse.y) },
                    { std::max(start.x, mouse.x), std::max(start.y, mouse.y) },
                    IM_COL32(76, 164, 229, 220));
            }
        }

        template<class Function>
        bool RunOperation(Function&& function) noexcept
        {
            try
            {
                std::forward<Function>(function)();
                return true;
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
                m_NodeDrag.reset();
                m_Marquee.reset();
                return false;
            }
        }

        [[nodiscard]] static GraphSelectionMode SelectionMode() noexcept
        {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.KeyAlt) return GraphSelectionMode::Subtract;
            if (io.KeySuper || io.KeyCtrl) return GraphSelectionMode::Toggle;
            if (io.KeyShift) return GraphSelectionMode::Add;
            return GraphSelectionMode::Replace;
        }

        [[nodiscard]] static GraphPoint PinPosition(const GraphSpatialIndex& index,
            const AuthoringDocument& document, PinID pin)
        {
            const auto& layout = index.Layout(document.NodeForPin(pin));
            const auto found = std::ranges::find(layout.Pins, pin, &GraphPinLayout::ID);
            if (found == layout.Pins.end()) throw std::logic_error("Graph layout omitted a connected pin.");
            return found->Position;
        }

        [[nodiscard]] static ImVec2 ToScreen(const GraphViewport& viewport,
            GraphPoint point, ImVec2 origin) noexcept
        {
            const GraphPoint screen = viewport.ToScreen(point, { origin.x, origin.y });
            return { static_cast<float>(screen.x), static_cast<float>(screen.y) };
        }

        static void DrawConnection(ImDrawList& draw, ImVec2 output, ImVec2 input,
            ImU32 color, float thickness)
        {
            const float tangent = std::max(48.0f, std::abs(input.x - output.x) * 0.45f);
            draw.AddBezierCubic(output, { output.x + tangent, output.y },
                { input.x - tangent, input.y }, input, color, thickness);
        }

        [[nodiscard]] static ImU32 PinColor(ValueType type) noexcept
        {
            switch (type)
            {
                case ValueType::Flow: return IM_COL32(225, 228, 234, 255);
                case ValueType::Boolean: return IM_COL32(206, 86, 91, 255);
                case ValueType::Integer: return IM_COL32(89, 194, 172, 255);
                case ValueType::Float: return IM_COL32(116, 205, 124, 255);
                case ValueType::Vector2:
                case ValueType::Vector3:
                case ValueType::Vector4: return IM_COL32(219, 184, 83, 255);
                case ValueType::String: return IM_COL32(217, 114, 187, 255);
                case ValueType::Asset: return IM_COL32(105, 151, 226, 255);
                case ValueType::Entity: return IM_COL32(87, 190, 222, 255);
            }
            return IM_COL32(180, 180, 180, 255);
        }
    };
}
