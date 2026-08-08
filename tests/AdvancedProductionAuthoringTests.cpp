#include <filesystem>
#include <memory>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets;
import Kairo.Editor.AdvancedProductionAuthoring;
import Kairo.Editor.NativeGameplayAuthoring;
import Kairo.Editor.OfflineRenderAuthoring;
import Kairo.EngineCore;

namespace
{
    class EditorNativeSystem final : public kairo::engine::NativeGameplaySystem {};

    kairo::engine::NativeGameplayRegistry MakeRegistry()
    {
        using namespace kairo::engine;
        NativeGameplayRegistry registry;
        NativeGameplayTypeInfo type;
        type.TypeName = "Mover";
        type.Properties.push_back({ "speed", NativeGameplayPropertyType::Number, 1.0, true, 0.0, 10.0 });
        registry.Register(type, [] { return std::make_unique<EditorNativeSystem>(); });
        return registry;
    }

    class MockOfflineRenderService final : public kairo::editor::OfflineRenderService
    {
    public:
        void Submit(kairo::editor::OfflineRenderRequest request) override
        {
            Request = std::move(request);
            Progress.JobID = Request.JobID;
            Progress.TotalPasses = Request.Passes;
            Progress.Status = kairo::editor::OfflineRenderWorkspaceStatus::Queued;
        }
        void Cancel(std::uint64_t jobID) override
        {
            REQUIRE(jobID == Request.JobID);
            Progress.Status = kairo::editor::OfflineRenderWorkspaceStatus::Cancelled;
        }
        [[nodiscard]] kairo::editor::OfflineRenderServiceProgress Poll(std::uint64_t jobID) override
        {
            REQUIRE(jobID == Request.JobID);
            return Progress;
        }

        kairo::editor::OfflineRenderRequest Request;
        kairo::editor::OfflineRenderServiceProgress Progress;
    };
}

TEST_CASE("advanced mesh workspace shares topology UV modifier and persistence contracts")
{
    using namespace kairo::assets;
    using namespace kairo::editor;
    MeshDocumentWorkspace workspace;
    auto& mesh = workspace.Mesh();
    const auto a = mesh.AddVertex({ 0.0, 0.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, 0.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ 0.0, 1.0, 0.0 });
    const auto face = mesh.AddFace({ a, b, c, d });

    workspace.SelectFace(face);
    workspace.TriangulateSelectedFaces();
    CHECK(workspace.Mesh().Faces().size() == 2u);
    REQUIRE(workspace.Undo());
    CHECK(workspace.Mesh().Faces().size() == 1u);
    REQUIRE(workspace.Redo());
    CHECK(workspace.Mesh().Faces().size() == 2u);

    workspace.Unwrap();
    workspace.AddModifier(TranslateModifier{ { 0.0, 0.0, 2.0 } });
    CHECK(workspace.EvaluatedMesh().Vertex(a).Position[2] == 2.0);

    const auto root = std::filesystem::temp_directory_path() / "kairo-editor-mesh-workspace-test";
    const auto path = root / "Meshes" / "quad.kmeshdoc";
    std::filesystem::remove_all(root);
    workspace.Save(path);
    MeshDocumentWorkspace reopened;
    reopened.Load(path);
    CHECK(reopened.Mesh().Faces().size() == 2u);
    CHECK(reopened.UVs().Coordinates().size() == 6u);
    CHECK(reopened.Modifiers().Modifiers().size() == 1u);
    std::filesystem::remove_all(root);
}

TEST_CASE("advanced sculpt workspace returns incremental dirty vertices and bounded remesh")
{
    using namespace kairo::assets;
    using namespace kairo::editor;
    MeshDocumentWorkspace workspace;
    auto& mesh = workspace.Mesh();
    const auto a = mesh.AddVertex({ -1.0, -1.0, 0.0 });
    const auto b = mesh.AddVertex({ 1.0, -1.0, 0.0 });
    const auto c = mesh.AddVertex({ 1.0, 1.0, 0.0 });
    const auto d = mesh.AddVertex({ -1.0, 1.0, 0.0 });
    (void)mesh.AddFace({ a, b, c, d });

    SculptBrush brush;
    brush.Mode = SculptBrushMode::Inflate;
    brush.Center = { 0.0, 0.0, 0.0 };
    brush.Radius = 2.0;
    brush.Strength = 0.2;
    const auto update = workspace.Sculpt(brush);
    CHECK(update.Vertices.size() == 4u);
    CHECK_FALSE(update.TopologyChanged);

    SculptRemeshSettings settings;
    settings.MinimumFaces = 4u;
    settings.MaximumFaces = 16u;
    settings.MaximumSubdivisionPasses = 2u;
    const auto remesh = workspace.RemeshForSculpt(settings);
    CHECK(remesh.TopologyChanged);
    CHECK(workspace.Mesh().Faces().size() >= 4u);
}

TEST_CASE("native gameplay inspector authors typed reflected overrides and persists them")
{
    using namespace kairo::engine;
    using namespace kairo::editor;
    Scene scene;
    const Entity actor = scene.CreateEntity("Actor");
    const auto registry = MakeRegistry();
    NativeGameplayAuthoringWorkspace workspace(scene, registry);
    workspace.Attach(actor, "Mover");
    workspace.SetProperty(actor, "Mover", "speed", 4.5);
    const auto inspector = workspace.Inspect(actor);
    REQUIRE(inspector.size() == 1u);
    REQUIRE(inspector.front().Properties.size() == 1u);
    CHECK(std::get<double>(inspector.front().Properties.front().Value) == 4.5);
    CHECK_THROWS_AS(workspace.SetProperty(actor, "Mover", "speed", 11.0), std::out_of_range);

    const auto root = std::filesystem::temp_directory_path() / "kairo-editor-native-workspace-test";
    const auto path = root / DefaultNativeGameplayManifestPath;
    std::filesystem::remove_all(root);
    workspace.Save(path);
    NativeGameplayAuthoringWorkspace reopened(scene, registry);
    reopened.Load(path);
    REQUIRE(reopened.Inspect(actor).size() == 1u);
    std::filesystem::remove_all(root);
}

TEST_CASE("offline render controller tracks async service progress completion and cancellation")
{
    using namespace kairo::editor;
    const auto service = std::make_shared<MockOfflineRenderService>();
    OfflineRenderAuthoringController controller(service);
    OfflineRenderRequest request;
    request.JobID = 42u;
    request.Width = 64u;
    request.Height = 32u;
    request.Passes = 8u;
    request.ProjectRoot = std::filesystem::temp_directory_path();
    request.RelativeOutput = "Renders/test.ppm";
    controller.Submit(request);
    CHECK(controller.State().Status() == OfflineRenderWorkspaceStatus::Queued);

    service->Progress.Status = OfflineRenderWorkspaceStatus::Running;
    service->Progress.CompletedPasses = 3u;
    controller.Refresh();
    CHECK(controller.State().Status() == OfflineRenderWorkspaceStatus::Running);
    CHECK(controller.State().CompletedPasses() == 3u);

    service->Progress.Status = OfflineRenderWorkspaceStatus::Completed;
    service->Progress.CompletedPasses = 8u;
    service->Progress.Output = request.ProjectRoot / request.RelativeOutput;
    controller.Refresh();
    CHECK(controller.State().Status() == OfflineRenderWorkspaceStatus::Completed);
    REQUIRE(controller.State().Output().has_value());

    OfflineRenderRequest second = request;
    second.JobID = 43u;
    controller.Submit(second);
    controller.Cancel();
    CHECK(controller.State().Status() == OfflineRenderWorkspaceStatus::Cancelled);
}