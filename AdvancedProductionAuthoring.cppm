module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module Kairo.Editor.AdvancedProductionAuthoring;

import Kairo.Assets;

export namespace kairo::editor
{
    enum class AuthoringSelectionMode : std::uint8_t { Vertex, Edge, Face };

    /// Complete project-owned mesh authoring session. History snapshots include
    /// topology, UVs, material authoring state and modifier stack, so undo never
    /// leaves related authoring domains out of sync.
    class MeshDocumentWorkspace final
    {
    public:
        static constexpr std::size_t MaximumHistory = 64u;

        [[nodiscard]] kairo::assets::EditableMeshDocument& Document() noexcept { return m_Document; }
        [[nodiscard]] const kairo::assets::EditableMeshDocument& Document() const noexcept { return m_Document; }
        [[nodiscard]] kairo::assets::EditableMesh& Mesh() noexcept { return m_Document.Mesh; }
        [[nodiscard]] const kairo::assets::EditableMesh& Mesh() const noexcept { return m_Document.Mesh; }
        [[nodiscard]] kairo::assets::UVLayout& UVs() noexcept { return m_Document.UVs; }
        [[nodiscard]] kairo::assets::MaterialAuthoringState& Materials() noexcept { return m_Document.Materials; }
        [[nodiscard]] kairo::assets::EditableMeshModifierStack& Modifiers() noexcept { return m_Document.Modifiers; }

        void NewDocument(kairo::assets::EditableMeshDocument document = {})
        {
            const auto validation = document.Mesh.Validate();
            if (!validation.Valid) throw std::invalid_argument(validation.Errors.front());
            m_Document = std::move(document);
            m_Undo.clear();
            m_Redo.clear();
            ClearSelection();
            ResetSculptSession();
        }

        void Load(const std::filesystem::path& path)
        {
            NewDocument(kairo::assets::LoadEditableMeshDocument(path));
            m_Path = path;
        }

        void Save(const std::filesystem::path& path)
        {
            kairo::assets::SaveEditableMeshDocument(m_Document, path);
            m_Path = path;
        }

        void Save()
        {
            if (m_Path.empty()) throw std::logic_error("Mesh document has no save path.");
            Save(m_Path);
        }

        [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

        void SetSelectionMode(AuthoringSelectionMode mode) noexcept
        {
            m_SelectionMode = mode;
            ClearSelection();
        }
        [[nodiscard]] AuthoringSelectionMode SelectionMode() const noexcept { return m_SelectionMode; }

        void SelectVertex(kairo::assets::EditableVertexID id, bool additive = false)
        {
            (void)m_Document.Mesh.Vertex(id);
            if (!additive) ClearSelection();
            m_Vertices.insert(id);
        }
        void SelectEdge(kairo::assets::EditableEdgeKey edge, bool additive = false)
        {
            (void)m_Document.Mesh.Vertex(edge.A);
            (void)m_Document.Mesh.Vertex(edge.B);
            if (!additive) ClearSelection();
            m_Edges.insert(kairo::assets::EditableEdgeKey::Canonical(edge.A, edge.B));
        }
        void SelectFace(kairo::assets::EditableFaceID id, bool additive = false)
        {
            (void)m_Document.Mesh.Face(id);
            if (!additive) ClearSelection();
            m_Faces.insert(id);
        }
        void ClearSelection() noexcept
        {
            m_Vertices.clear();
            m_Edges.clear();
            m_Faces.clear();
        }

        [[nodiscard]] const std::set<kairo::assets::EditableVertexID>& SelectedVertices() const noexcept { return m_Vertices; }
        [[nodiscard]] const std::set<kairo::assets::EditableEdgeKey>& SelectedEdges() const noexcept { return m_Edges; }
        [[nodiscard]] const std::set<kairo::assets::EditableFaceID>& SelectedFaces() const noexcept { return m_Faces; }

        void TranslateSelectedVertices(std::array<double, 3u> delta)
        {
            if (m_Vertices.empty()) throw std::logic_error("Translate requires selected vertices.");
            Checkpoint();
            try
            {
                m_Document.Mesh.Translate(
                    std::vector<kairo::assets::EditableVertexID>(m_Vertices.begin(), m_Vertices.end()), delta);
                const auto report = m_Document.Mesh.Validate();
                if (!report.Valid) throw std::runtime_error(report.Errors.front());
                ResetSculptSession();
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableFaceID ExtrudeSelectedFace(std::array<double, 3u> offset)
        {
            RequireFaces(1u);
            Checkpoint();
            try
            {
                const auto created = m_Document.Mesh.ExtrudeFace(*m_Faces.begin(), offset);
                ClearSelection(); m_Faces.insert(created);
                FinishTopologyEdit();
                return created;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableFaceID InsetSelectedFace(double amount)
        {
            RequireFaces(1u);
            Checkpoint();
            try
            {
                const auto created = m_Document.Mesh.InsetFace(*m_Faces.begin(), amount);
                ClearSelection(); m_Faces.insert(created);
                FinishTopologyEdit();
                return created;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableFaceID BevelSelectedFace(double insetAmount, double depth)
        {
            RequireFaces(1u);
            Checkpoint();
            try
            {
                const auto created = kairo::assets::BevelFace(
                    m_Document.Mesh, *m_Faces.begin(), insetAmount, depth);
                ClearSelection(); m_Faces.insert(created);
                FinishTopologyEdit();
                return created;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::LoopCutResult LoopCutSelectedEdge(double t = 0.5)
        {
            RequireEdges(1u);
            Checkpoint();
            try
            {
                const auto result = kairo::assets::LoopCutQuadStrip(
                    m_Document.Mesh, *m_Edges.begin(), t);
                ClearSelection();
                m_Vertices.insert(result.CutVertices.begin(), result.CutVertices.end());
                FinishTopologyEdit();
                return result;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void MergeSelectedVertices()
        {
            if (m_Vertices.size() != 2u)
                throw std::logic_error("Merge requires exactly two selected vertices.");
            auto it = m_Vertices.begin();
            const auto keep = *it++;
            const auto remove = *it;
            Checkpoint();
            try
            {
                m_Document.Mesh.MergeVertices(keep, remove);
                ClearSelection(); m_Vertices.insert(keep);
                FinishTopologyEdit();
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableVertexNormalMap RecalculateNormals() const
        { return kairo::assets::RecalculateSmoothNormals(m_Document.Mesh); }

        [[nodiscard]] double SelectedFaceTexelDensity(std::uint32_t textureResolution) const
        {
            RequireFaces(1u);
            return kairo::assets::EstimateUVTexelDensity(
                m_Document.Mesh, m_Document.UVs, *m_Faces.begin(), textureResolution);
        }

        [[nodiscard]] kairo::assets::EditableVertexID SplitSelectedEdge(double t = 0.5)
        {
            RequireEdges(1u);
            Checkpoint();
            try
            {
                const auto created = kairo::assets::SplitEdge(m_Document.Mesh, *m_Edges.begin(), t);
                m_Vertices = { created };
                m_Edges.clear(); m_Faces.clear();
                FinishTopologyEdit();
                return created;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] std::pair<kairo::assets::EditableFaceID, kairo::assets::EditableFaceID>
        KnifeSelectedFace(double firstT = 0.5, double secondT = 0.5)
        {
            RequireFaces(1u); RequireEdges(2u);
            Checkpoint();
            try
            {
                auto edge = m_Edges.begin();
                const auto first = *edge++;
                const auto second = *edge;
                const auto created = kairo::assets::KnifeFace(
                    m_Document.Mesh, *m_Faces.begin(), first, second, firstT, secondT);
                m_Faces = { created.first, created.second };
                m_Vertices.clear(); m_Edges.clear();
                FinishTopologyEdit();
                return created;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void TriangulateSelectedFaces()
        {
            if (m_Faces.empty()) throw std::logic_error("Triangulate requires selected faces.");
            const std::vector<kairo::assets::EditableFaceID> selected(m_Faces.begin(), m_Faces.end());
            Checkpoint();
            try
            {
                std::set<kairo::assets::EditableFaceID> created;
                for (const auto face : selected)
                {
                    const auto triangles = kairo::assets::TriangulateFace(m_Document.Mesh, face);
                    created.insert(triangles.begin(), triangles.end());
                }
                m_Faces = std::move(created);
                FinishTopologyEdit();
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableFaceID DissolveSelectedEdge()
        {
            RequireEdges(1u);
            Checkpoint();
            try
            {
                const auto face = kairo::assets::DissolveEdge(m_Document.Mesh, *m_Edges.begin());
                ClearSelection(); m_Faces.insert(face);
                FinishTopologyEdit();
                return face;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] std::vector<kairo::assets::EditableFaceID> DuplicateSelectedFaces(
            std::array<double, 3u> offset = {})
        {
            if (m_Faces.empty()) throw std::logic_error("Duplicate requires selected faces.");
            Checkpoint();
            try
            {
                const std::vector<kairo::assets::EditableFaceID> source(m_Faces.begin(), m_Faces.end());
                const auto result = kairo::assets::DuplicateFaces(m_Document.Mesh, source, offset);
                m_Faces = std::set<kairo::assets::EditableFaceID>(result.begin(), result.end());
                m_Vertices.clear(); m_Edges.clear();
                FinishTopologyEdit();
                return result;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] std::vector<kairo::assets::EditableFaceID> Bridge(
            const std::vector<kairo::assets::EditableVertexID>& firstLoop,
            const std::vector<kairo::assets::EditableVertexID>& secondLoop,
            std::uint32_t materialSlot = 0u)
        {
            Checkpoint();
            try
            {
                const auto result = kairo::assets::BridgeLoops(
                    m_Document.Mesh, firstLoop, secondLoop, materialSlot);
                m_Faces = std::set<kairo::assets::EditableFaceID>(result.begin(), result.end());
                m_Vertices.clear(); m_Edges.clear();
                FinishTopologyEdit();
                return result;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableFaceID Fill(
            std::vector<kairo::assets::EditableVertexID> orderedBoundary,
            std::uint32_t materialSlot = 0u)
        {
            Checkpoint();
            try
            {
                const auto result = kairo::assets::FillBoundary(
                    m_Document.Mesh, std::move(orderedBoundary), materialSlot);
                ClearSelection(); m_Faces.insert(result);
                FinishTopologyEdit();
                return result;
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void FlipSelectedNormals()
        {
            if (m_Faces.empty()) throw std::logic_error("Normal flip requires selected faces.");
            Checkpoint();
            try
            {
                for (const auto face : m_Faces) kairo::assets::FlipFaceNormal(m_Document.Mesh, face);
                FinishTopologyEdit();
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void AssignSelectedMaterial(std::uint32_t slot)
        {
            if (m_Faces.empty()) throw std::logic_error("Material assignment requires selected faces.");
            Checkpoint();
            try
            {
                kairo::assets::AssignMaterialSlot(
                    m_Document.Mesh,
                    std::vector<kairo::assets::EditableFaceID>(m_Faces.begin(), m_Faces.end()),
                    slot);
                FinishTopologyEdit();
            }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void Unwrap(kairo::assets::UVProjectionAxis axis = kairo::assets::UVProjectionAxis::Z)
        {
            Checkpoint();
            try { kairo::assets::PlanarUnwrap(m_Document.Mesh, m_Document.UVs, axis); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void MarkSeam(kairo::assets::EditableEdgeKey edge)
        {
            Checkpoint();
            try { m_Document.UVs.MarkSeam(edge); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void PackUVIslands(double padding = 0.01)
        {
            Checkpoint();
            try { kairo::assets::PackUVIslands(m_Document.Mesh, m_Document.UVs, padding); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] std::vector<kairo::assets::UVIsland> UVIslands() const
        { return kairo::assets::BuildUVIslands(m_Document.Mesh, m_Document.UVs); }

        [[nodiscard]] std::size_t AddMaterial(kairo::assets::MaterialArtifactData material)
        {
            Checkpoint();
            try { return m_Document.Materials.AddMaterial(std::move(material)); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void BindTexture(kairo::assets::TextureAssetHandle texture,
            kairo::assets::TextureAuthoringSettings settings)
        {
            Checkpoint();
            try { m_Document.Materials.BindTexture(texture, settings); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        void AddModifier(kairo::assets::EditableMeshModifier modifier)
        {
            Checkpoint();
            try { m_Document.Modifiers.Add(std::move(modifier)); }
            catch (...) { RollbackFailedEdit(); throw; }
        }

        [[nodiscard]] kairo::assets::EditableMesh EvaluatedMesh() const
        { return m_Document.Modifiers.Evaluate(m_Document.Mesh); }

        void StartSculpt(kairo::assets::SculptSessionBudget budget = {})
        {
            m_Sculpt = std::make_unique<kairo::assets::ProductionSculptSession>(m_Document.Mesh, budget);
        }

        [[nodiscard]] kairo::assets::SculptViewportUpdate Sculpt(
            const kairo::assets::SculptBrush& brush)
        {
            if (!m_Sculpt) StartSculpt();
            Checkpoint();
            try { return m_Sculpt->Apply(brush); }
            catch (...) { RollbackFailedEdit(); ResetSculptSession(); throw; }
        }

        [[nodiscard]] kairo::assets::SculptViewportUpdate RemeshForSculpt(
            const kairo::assets::SculptRemeshSettings& settings)
        {
            Checkpoint();
            try
            {
                auto update = kairo::assets::RemeshForSculpt(m_Document.Mesh, settings);
                ResetSculptSession();
                ClearSelection();
                return update;
            }
            catch (...) { RollbackFailedEdit(); ResetSculptSession(); throw; }
        }

        [[nodiscard]] kairo::assets::SculptMultiresolution CreateMultiresolution() const
        { return kairo::assets::SculptMultiresolution(m_Document.Mesh); }

        bool Undo()
        {
            if (m_Undo.empty()) return false;
            m_Redo.push_back(kairo::assets::SerializeEditableMeshDocument(m_Document));
            m_Document = kairo::assets::ParseEditableMeshDocument(m_Undo.back());
            m_Undo.pop_back();
            Trim(m_Redo);
            ClearSelection(); ResetSculptSession();
            return true;
        }

        bool Redo()
        {
            if (m_Redo.empty()) return false;
            m_Undo.push_back(kairo::assets::SerializeEditableMeshDocument(m_Document));
            m_Document = kairo::assets::ParseEditableMeshDocument(m_Redo.back());
            m_Redo.pop_back();
            Trim(m_Undo);
            ClearSelection(); ResetSculptSession();
            return true;
        }

        [[nodiscard]] std::size_t UndoDepth() const noexcept { return m_Undo.size(); }
        [[nodiscard]] std::size_t RedoDepth() const noexcept { return m_Redo.size(); }

    private:
        kairo::assets::EditableMeshDocument m_Document;
        std::filesystem::path m_Path;
        AuthoringSelectionMode m_SelectionMode = AuthoringSelectionMode::Face;
        std::set<kairo::assets::EditableVertexID> m_Vertices;
        std::set<kairo::assets::EditableEdgeKey> m_Edges;
        std::set<kairo::assets::EditableFaceID> m_Faces;
        std::vector<std::string> m_Undo;
        std::vector<std::string> m_Redo;
        std::unique_ptr<kairo::assets::ProductionSculptSession> m_Sculpt;

        static void Trim(std::vector<std::string>& history)
        {
            if (history.size() > MaximumHistory)
                history.erase(history.begin(), history.begin() +
                    static_cast<std::ptrdiff_t>(history.size() - MaximumHistory));
        }

        void Checkpoint()
        {
            m_Undo.push_back(kairo::assets::SerializeEditableMeshDocument(m_Document));
            Trim(m_Undo);
            m_Redo.clear();
        }

        void RollbackFailedEdit()
        {
            if (m_Undo.empty()) return;
            m_Document = kairo::assets::ParseEditableMeshDocument(m_Undo.back());
            m_Undo.pop_back();
        }

        void FinishTopologyEdit()
        {
            const auto report = m_Document.Mesh.Validate();
            if (!report.Valid) throw std::runtime_error(report.Errors.front());
            // Topology edits invalidate per-corner UV assignments until the next unwrap.
            m_Document.UVs.Clear();
            ResetSculptSession();
        }

        void ResetSculptSession() noexcept { m_Sculpt.reset(); }
        void RequireEdges(std::size_t count) const
        { if (m_Edges.size() != count) throw std::logic_error("Operation requires a different number of selected edges."); }
        void RequireFaces(std::size_t count) const
        { if (m_Faces.size() != count) throw std::logic_error("Operation requires a different number of selected faces."); }
    };
}
