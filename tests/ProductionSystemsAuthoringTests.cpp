#include <filesystem>

#include <catch2/catch_test_macros.hpp>

import Kairo.Editor.ProductionSystemsAuthoring;

TEST_CASE("production systems authoring shares Core workload budgets")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    ProductionPerformanceBudget budget;
    budget.MaximumParticles = 128u;
    ProductionSystemsAuthoringWorkspace workspace(budget);
    workspace.SetParticles(ProductionParticleDescriptor{ 64u });
    CHECK(workspace.Workload().ParticleCapacity == 64u);
    CHECK_THROWS_AS(workspace.SetParticles(ProductionParticleDescriptor{ 129u }), std::length_error);
    CHECK(workspace.Workload().ParticleCapacity == 64u);
}

TEST_CASE("production systems authoring persists and builds a live preview runtime")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    ProductionSystemsAuthoringWorkspace workspace;
    workspace.SetTerrain(ProductionTerrainDescriptor{ 8u, 8u, 1.0 });
    workspace.SetFoliage(ProductionFoliageDescriptor{ 12u, 99u });
    workspace.SetParticles(ProductionParticleDescriptor{ 32u });
    workspace.SetFluid(ProductionFluidDescriptor{ 8u, 8u, 0.05 });
    workspace.SetStreaming(ProductionStreamingDescriptor{ 32.0, 1 });

    ProductionAnimationDescriptor animation;
    animation.Name = "Preview";
    animation.Duration = 1.0;
    animation.Keys.push_back({ "Root", { 0.0, { 0.0, 0.0, 0.0 } } });
    animation.Keys.push_back({ "Root", { 1.0, { 1.0, 0.0, 0.0 } } });
    workspace.AddAnimation(std::move(animation));

    auto preview = workspace.CreatePreviewRuntime();
    CHECK(preview.Foliage().size() == 12u);
    CHECK(preview.SampleAnimation("Preview", "Root", 0.5, false).X == 0.5);
    preview.Step(0.016, 32.0, 0.0);
    CHECK(preview.Profile().Frames == 1u);

    const auto root = std::filesystem::temp_directory_path() / "kairo-editor-production-authoring-test";
    const auto path = root / DefaultProductionSystemsManifestPath;
    std::filesystem::remove_all(root);
    workspace.Save(path);
    ProductionSystemsAuthoringWorkspace reopened;
    reopened.Load(path);
    CHECK(reopened.Workload().FoliageInstances == 12u);
    CHECK(reopened.Manifest().Animations.size() == 1u);
    std::filesystem::remove_all(root);
}