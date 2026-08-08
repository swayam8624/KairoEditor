module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.ProductionAuthoring;

import Kairo.Assets.EditableMesh;
import Kairo.Assets.UVAuthoring;
import Kairo.Assets.Sculpting;

export namespace kairo::editor
{
    enum class ModelingMode : std::uint8_t { Object, Edit };
    enum class MeshSelectionMode : std::uint8_t { Vertex, Edge, Face };
    enum class MaterialChannelPreview : std::uint8_t
    { Lit, BaseColor, Normal, Metallic, Roughness, Occlusion, Emissive, UVChecker };

    class ModelingWorkspace final
    {
    public:
        static constexpr std::size_t MaximumHistory = 128u;

        [[nodiscard]] kairo::assets::EditableMesh& Mesh() noexcept { return m_Mesh; }
        [[nodiscard]] const kairo::assets::EditableMesh& Mesh() const noexcept { return m_Mesh; }
        [[nodiscard]] ModelingMode Mode() const noexcept { return m_Mode; }
        [[nodiscard]] MeshSelectionMode SelectionMode() const noexcept { return m_SelectionMode; }
        void SetMode(ModelingMode mode) noexcept { m_Mode = mode; ClearSelection(); }
        void SetSelectionMode(MeshSelectionMode mode) noexcept { m_SelectionMode = mode; ClearSelection(); }

        void SelectVertex(kairo::assets::EditableVertexID vertex, bool additive = false)
        {
            (void)m_Mesh.Vertex(vertex);
            if (!additive) ClearSelection();
            m_SelectedVertices.insert(vertex);
        }
        void SelectFace(kairo::assets::EditableFaceID face, bool additive = false)
        {
            (void)m_Mesh.Face(face);
            if (!additive) ClearSelection();
            m_SelectedFaces.insert(face);
        }
        void SelectEdge(kairo::assets::EditableEdgeKey edge, bool additive = false)
        {
            (void)m_Mesh.Vertex(edge.A); (void)m_Mesh.Vertex(edge.B);
            if (!additive) ClearSelection();
            m_SelectedEdges.insert(edge);
        }
        void ClearSelection() noexcept
        { m_SelectedVertices.clear(); m_SelectedEdges.clear(); m_SelectedFaces.clear(); }

        [[nodiscard]] const std::set<kairo::assets::EditableVertexID>& SelectedVertices() const noexcept
        { return m_SelectedVertices; }
        [[nodiscard]] const std::set<kairo::assets::EditableEdgeKey>& SelectedEdges() const noexcept
        { return m_SelectedEdges; }
        [[nodiscard]] const std::set<kairo::assets::EditableFaceID>& SelectedFaces() const noexcept
        { return m_SelectedFaces; }

        void TranslateSelected(std::array<double,3u> delta)
        {
            RequireEditMode();
            Checkpoint();
            std::vector<kairo::assets::EditableVertexID> vertices(m_SelectedVertices.begin(), m_SelectedVertices.end());
            if (vertices.empty())
                for (const auto faceID : m_SelectedFaces)
                    for (const auto vertex : m_Mesh.Face(faceID).Vertices) vertices.push_back(vertex);
            m_Mesh.Translate(vertices, delta);
            FinishEdit();
        }

        [[nodiscard]] kairo::assets::EditableFaceID ExtrudeSelectedFace(std::array<double,3u> offset)
        {
            RequireSingleFace(); Checkpoint();
            const auto created = m_Mesh.ExtrudeFace(*m_SelectedFaces.begin(), offset);
            m_SelectedFaces = { created };
            FinishEdit();
            return created;
        }

        [[nodiscard]] kairo::assets::EditableFaceID InsetSelectedFace(double amount)
        {
            RequireSingleFace(); Checkpoint();
            const auto created = m_Mesh.InsetFace(*m_SelectedFaces.begin(), amount);
            m_SelectedFaces = { created };
            FinishEdit();
            return created;
        }

        void MergeSelectedVertices()
        {
            RequireEditMode();
            if (m_SelectedVertices.size() < 2u) throw std::logic_error("Merge requires at least two selected vertices.");
            Checkpoint();
            const auto keep = *m_SelectedVertices.begin();
            for (auto it = std::next(m_SelectedVertices.begin()); it != m_SelectedVertices.end(); ++it)
                m_Mesh.MergeVertices(keep, *it);
            m_SelectedVertices = { keep };
            FinishEdit();
        }

        void Subdivide()
        {
            RequireEditMode(); Checkpoint();
            kairo::assets::SubdivideEditableMesh(m_Mesh);
            ClearSelection(); FinishEdit();
        }

        bool Undo()
        {
            if (m_Undo.empty()) return false;
            m_Redo.push_back(m_Mesh);
            m_Mesh = std::move(m_Undo.back()); m_Undo.pop_back(); ClearSelection(); return true;
        }
        bool Redo()
        {
            if (m_Redo.empty()) return false;
            m_Undo.push_back(m_Mesh);
            m_Mesh = std::move(m_Redo.back()); m_Redo.pop_back(); ClearSelection(); return true;
        }
        [[nodiscard]] std::size_t UndoDepth() const noexcept { return m_Undo.size(); }

    private:
        kairo::assets::EditableMesh m_Mesh;
        ModelingMode m_Mode = ModelingMode::Object;
        MeshSelectionMode m_SelectionMode = MeshSelectionMode::Face;
        std::set<kairo::assets::EditableVertexID> m_SelectedVertices;
        std::set<kairo::assets::EditableEdgeKey> m_SelectedEdges;
        std::set<kairo::assets::EditableFaceID> m_SelectedFaces;
        std::vector<kairo::assets::EditableMesh> m_Undo;
        std::vector<kairo::assets::EditableMesh> m_Redo;

        void Checkpoint()
        {
            m_Undo.push_back(m_Mesh);
            if (m_Undo.size() > MaximumHistory) m_Undo.erase(m_Undo.begin());
            m_Redo.clear();
        }
        void FinishEdit()
        {
            const auto report = m_Mesh.Validate();
            if (!report.Valid) { (void)Undo(); throw std::runtime_error(report.Errors.front()); }
        }
        void RequireEditMode() const
        { if (m_Mode != ModelingMode::Edit) throw std::logic_error("Mesh operation requires Edit mode."); }
        void RequireSingleFace() const
        { RequireEditMode(); if (m_SelectedFaces.size()!=1u) throw std::logic_error("Operation requires exactly one selected face."); }
    };

    class UVWorkspace final
    {
    public:
        explicit UVWorkspace(kairo::assets::EditableMesh& mesh) : m_Mesh(mesh) {}
        [[nodiscard]] kairo::assets::UVLayout& Layout() noexcept { return m_Layout; }
        [[nodiscard]] const kairo::assets::UVLayout& Layout() const noexcept { return m_Layout; }
        void MarkSeam(kairo::assets::EditableEdgeKey edge) { m_Layout.MarkSeam(edge); }
        void Unwrap(kairo::assets::UVProjectionAxis axis = kairo::assets::UVProjectionAxis::Z)
        { kairo::assets::PlanarUnwrap(m_Mesh, m_Layout, axis); }
        void Pack(double padding = 0.01) { kairo::assets::NormalizeAndPackUVs(m_Layout, padding); }
        void SetPreview(MaterialChannelPreview preview) noexcept { m_Preview = preview; }
        [[nodiscard]] MaterialChannelPreview Preview() const noexcept { return m_Preview; }
        [[nodiscard]] kairo::assets::MeshArtifactData Cook() const
        { return kairo::assets::CookEditableMeshWithUV(m_Mesh, m_Layout); }
    private:
        kairo::assets::EditableMesh& m_Mesh;
        kairo::assets::UVLayout m_Layout;
        MaterialChannelPreview m_Preview = MaterialChannelPreview::UVChecker;
    };

    class SculptWorkspace final
    {
    public:
        explicit SculptWorkspace(kairo::assets::EditableMesh& mesh) : m_Session(mesh) {}
        void Mask(kairo::assets::EditableVertexID vertex, double amount) { m_Session.SetMask(vertex, amount); }
        [[nodiscard]] kairo::assets::SculptStroke Stroke(const kairo::assets::SculptBrush& brush)
        { return m_Session.Apply(brush); }
        bool Undo() { return m_Session.Undo(); }
        bool Redo() { return m_Session.Redo(); }
        [[nodiscard]] std::size_t UndoDepth() const noexcept { return m_Session.UndoDepth(); }
    private:
        kairo::assets::SculptSession m_Session;
    };

    enum class OfflineRenderWorkspaceStatus : std::uint8_t { Idle, Queued, Running, Completed, Cancelled, Failed };

    class OfflineRenderWorkspaceState final
    {
    public:
        void Queue(std::uint64_t jobID, std::uint32_t passes)
        {
            if (jobID == 0u || passes == 0u) throw std::invalid_argument("Offline render job ID and passes must be non-zero.");
            m_JobID = jobID; m_TotalPasses = passes; m_CompletedPasses = 0u;
            m_Status = OfflineRenderWorkspaceStatus::Queued; m_Diagnostics.clear(); m_Output.reset();
        }
        void SetRunning(std::uint32_t completed)
        {
            if (completed > m_TotalPasses) throw std::out_of_range("Offline render progress exceeds total passes.");
            m_CompletedPasses = completed; m_Status = OfflineRenderWorkspaceStatus::Running;
        }
        void Complete(std::filesystem::path output)
        {
            if (output.empty()) throw std::invalid_argument("Offline render output path cannot be empty.");
            m_CompletedPasses = m_TotalPasses; m_Output = std::move(output); m_Status = OfflineRenderWorkspaceStatus::Completed;
        }
        void Cancel() noexcept { m_Status = OfflineRenderWorkspaceStatus::Cancelled; }
        void Fail(std::string diagnostic) { m_Diagnostics.push_back(std::move(diagnostic)); m_Status = OfflineRenderWorkspaceStatus::Failed; }
        void AddDiagnostic(std::string diagnostic) { m_Diagnostics.push_back(std::move(diagnostic)); }

        [[nodiscard]] OfflineRenderWorkspaceStatus Status() const noexcept { return m_Status; }
        [[nodiscard]] std::uint64_t JobID() const noexcept { return m_JobID; }
        [[nodiscard]] double Progress() const noexcept
        { return m_TotalPasses == 0u ? 0.0 : static_cast<double>(m_CompletedPasses)/m_TotalPasses; }
        [[nodiscard]] const std::vector<std::string>& Diagnostics() const noexcept { return m_Diagnostics; }
        [[nodiscard]] const std::optional<std::filesystem::path>& Output() const noexcept { return m_Output; }

    private:
        OfflineRenderWorkspaceStatus m_Status = OfflineRenderWorkspaceStatus::Idle;
        std::uint64_t m_JobID = 0u;
        std::uint32_t m_TotalPasses = 0u;
        std::uint32_t m_CompletedPasses = 0u;
        std::vector<std::string> m_Diagnostics;
        std::optional<std::filesystem::path> m_Output;
    };
}
