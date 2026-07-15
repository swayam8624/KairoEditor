#include <catch2/catch_test_macros.hpp>

import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;
import Kairo.Renderer;

using namespace kairo::editor;

namespace
{
    const auto MeshID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000201");
    const auto MaterialID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000202");

    void RegisterRenderAssets(kairo::assets::AssetRegistry& registry)
    {
        registry.Insert({ MeshID, kairo::assets::AssetType::Mesh, kairo::assets::AssetOrigin::Builtin,
            "builtin/cube", "kairo.builtin", 1u, {} });
        registry.Insert({ MaterialID, kairo::assets::AssetType::Material, kairo::assets::AssetOrigin::Builtin,
            "builtin/default-material", "kairo.builtin", 1u, {} });
    }
}

TEST_CASE("Engine scenes extract visible renderer draws in entity order", "[KairoEditor][RenderBridge]")
{
    kairo::engine::Scene scene;
    const auto first = scene.CreateEntity("First");
    const auto hidden = scene.CreateEntity("Hidden");
    const auto second = scene.CreateEntity("Second");
    scene.SetMeshRenderer(first, { { MeshID }, { MaterialID }, true });
    scene.SetMeshRenderer(hidden, { { MeshID }, { MaterialID }, false });
    scene.SetMeshRenderer(second, { { MeshID }, { MaterialID }, true });
    scene.Transform(first).Local.Translation = { -2.0f, 0.0f, 0.0f };
    scene.Transform(second).Local.Scale = { 0.5f, 2.0f, 0.5f };

    kairo::assets::AssetRegistry registry;
    RegisterRenderAssets(registry);
    RenderAssetBindings assets(registry);
    assets.BindMesh({ MeshID }, 7u);
    const auto renderScene = BuildRenderScene(scene, assets);

    REQUIRE(renderScene.Draws().size() == 2u);
    CHECK(renderScene.Draws()[0].Mesh == 7u);
    CHECK(renderScene.Draws()[0].Model(0u, 3u) == -2.0f);
    CHECK(renderScene.Draws()[1].Model(1u, 1u) == 2.0f);
}

TEST_CASE("Render asset bindings reject ambiguous and missing assets", "[KairoEditor][RenderBridge]")
{
    kairo::assets::AssetRegistry registry;
    RegisterRenderAssets(registry);
    RenderAssetBindings assets(registry);
    REQUIRE_THROWS_AS(assets.BindMesh({}, 1u), std::invalid_argument);
    REQUIRE_THROWS_AS(assets.BindMesh({ MeshID }, kairo::renderer::InvalidMeshHandle), std::invalid_argument);
    assets.BindMesh({ MeshID }, 1u);
    REQUIRE_THROWS_AS(assets.BindMesh({ MeshID }, 2u), std::invalid_argument);

    kairo::engine::Scene scene;
    const auto entity = scene.CreateEntity("Missing mesh");
    const auto missing = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000299");
    scene.SetMeshRenderer(entity, { { missing }, { MaterialID }, true });
    REQUIRE_THROWS_AS(BuildRenderScene(scene, assets), std::out_of_range);
}
