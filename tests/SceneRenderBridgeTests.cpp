#include <catch2/catch_test_macros.hpp>

import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;
import Kairo.Renderer;

using namespace kairo::editor;

TEST_CASE("Engine scenes extract visible renderer draws in entity order", "[KairoEditor][RenderBridge]")
{
    kairo::engine::Scene scene;
    const auto first = scene.CreateEntity("First");
    const auto hidden = scene.CreateEntity("Hidden");
    const auto second = scene.CreateEntity("Second");
    scene.SetMeshRenderer(first, { "mesh/cube", "material/default", true });
    scene.SetMeshRenderer(hidden, { "mesh/cube", "material/default", false });
    scene.SetMeshRenderer(second, { "mesh/cube", "material/default", true });
    scene.Transform(first).Local.Translation = { -2.0f, 0.0f, 0.0f };
    scene.Transform(second).Local.Scale = { 0.5f, 2.0f, 0.5f };

    RenderAssetBindings assets;
    assets.BindMesh("mesh/cube", 7u);
    const auto renderScene = BuildRenderScene(scene, assets);

    REQUIRE(renderScene.Draws().size() == 2u);
    CHECK(renderScene.Draws()[0].Mesh == 7u);
    CHECK(renderScene.Draws()[0].Model(0u, 3u) == -2.0f);
    CHECK(renderScene.Draws()[1].Model(1u, 1u) == 2.0f);
}

TEST_CASE("Render asset bindings reject ambiguous and missing assets", "[KairoEditor][RenderBridge]")
{
    RenderAssetBindings assets;
    REQUIRE_THROWS_AS(assets.BindMesh("", 1u), std::invalid_argument);
    REQUIRE_THROWS_AS(assets.BindMesh("mesh/cube", kairo::renderer::InvalidMeshHandle), std::invalid_argument);
    assets.BindMesh("mesh/cube", 1u);
    REQUIRE_THROWS_AS(assets.BindMesh("mesh/cube", 2u), std::invalid_argument);

    kairo::engine::Scene scene;
    const auto entity = scene.CreateEntity("Missing mesh");
    scene.SetMeshRenderer(entity, { "mesh/missing", "material/default", true });
    REQUIRE_THROWS_AS(BuildRenderScene(scene, assets), std::out_of_range);
}
