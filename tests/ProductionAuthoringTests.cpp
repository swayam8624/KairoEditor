#include <array>
#include <filesystem>

#include <catch2/catch_test_macros.hpp>

import Kairo.Editor.ProductionAuthoring;

TEST_CASE("Modeling workspace supports edit selection operations and undo")
{
    using namespace kairo::editor;
    ModelingWorkspace workspace;
    auto& mesh = workspace.Mesh();
    const auto a = mesh.AddVertex({ -1.0, 0.0, -1.0 });
    const auto b = mesh.AddVertex({  1.0, 0.0, -1.0 });
    const auto c = mesh.AddVertex({  1.0, 0.0,  1.0 });
    const auto d = mesh.AddVertex({ -1.0, 0.0,  1.0 });
    const auto face = mesh.AddFace({ a, b, c, d });

    workspace.SetMode(ModelingMode::Edit);
    workspace.SetSelectionMode(MeshSelectionMode::Face);
    workspace.SelectFace(face);
    const auto top = workspace.ExtrudeSelectedFace({ 0.0, 1.0, 0.0 });
    CHECK(mesh.Face(top).Vertices.size() == 4u);
    CHECK(workspace.UndoDepth() == 1u);
    REQUIRE(workspace.Undo());
    CHECK(workspace.Mesh().Faces().size() == 1u);
    REQUIRE(workspace.Redo());
    CHECK(workspace.Mesh().Faces().size() == 5u);
}

TEST_CASE("UV and sculpt workspaces share the editable mesh")
{
    using namespace kairo::editor;
    ModelingWorkspace modeling;
    auto& mesh = modeling.Mesh();
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto face = mesh.AddFace({ a, b, c });

    UVWorkspace uv(mesh);
    uv.MarkSeam(kairo::assets::EditableEdgeKey::Canonical(a, b));
    uv.Unwrap();
    uv.Pack(0.02);
    uv.SetPreview(MaterialChannelPreview::UVChecker);
    const auto cooked = uv.Cook();
    CHECK(cooked.HasTexCoords);
    CHECK(uv.Layout().Contains({ face, 0u }));

    SculptWorkspace sculpt(mesh);
    kairo::assets::SculptBrush brush;
    brush.Mode = kairo::assets::SculptBrushMode::Grab;
    brush.Center = { 0.0, 0.0, 0.0 };
    brush.Delta = { 0.0, 0.0, 1.0 };
    brush.Radius = 0.25;
    brush.Strength = 1.0;
    REQUIRE_FALSE(sculpt.Stroke(brush).Deltas.empty());
    REQUIRE(sculpt.Undo());
    CHECK(mesh.Vertex(a).Position[2] == 0.0);
}

TEST_CASE("Offline render workspace tracks progress diagnostics and output")
{
    using namespace kairo::editor;
    OfflineRenderWorkspaceState state;
    state.Queue(7u, 4u);
    CHECK(state.Status() == OfflineRenderWorkspaceStatus::Queued);
    state.SetRunning(2u);
    CHECK(state.Progress() == 0.5);
    state.AddDiagnostic("Unsupported translucent material will use fallback.");
    REQUIRE(state.Diagnostics().size() == 1u);
    state.Complete(std::filesystem::path("renders/final.exr"));
    CHECK(state.Status() == OfflineRenderWorkspaceStatus::Completed);
    CHECK(state.Progress() == 1.0);
    REQUIRE(state.Output().has_value());
}
