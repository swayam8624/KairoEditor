#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;
import Kairo.Renderer;
import Kairo.Foundation.Math;

using namespace kairo::editor;

namespace
{
    const auto MeshID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000201");
    const auto MaterialID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000202");
    const auto SceneID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000203");
    const auto TextureID = kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000204");

    void RegisterRenderAssets(kairo::assets::AssetRegistry& registry)
    {
        registry.Insert({ MeshID, kairo::assets::AssetType::Mesh, kairo::assets::AssetOrigin::Builtin,
            "builtin/cube", "kairo.builtin", 1u, {} });
        registry.Insert({ MaterialID, kairo::assets::AssetType::Material, kairo::assets::AssetOrigin::Builtin,
            "builtin/default-material", "kairo.builtin", 1u, {} });
        registry.Insert({ SceneID, kairo::assets::AssetType::Scene,
            kairo::assets::AssetOrigin::SourceFile, "Scenes/model.glb",
            "kairo.gltf.scene", 1u, {} });
        registry.Insert({ TextureID, kairo::assets::AssetType::Texture2D,
            kairo::assets::AssetOrigin::SourceFile, "Textures/studio.hdr",
            "kairo.texture.stb", 1u, {} });
    }
}

TEST_CASE("Imported scene instances expand shared primitive bindings with instance transforms",
    "[KairoEditor][RenderBridge][glTF]")
{
    kairo::assets::AssetRegistry registry;
    RegisterRenderAssets(registry);
    RenderAssetBindings assets(registry);
    kairo::renderer::PBRMaterial material;
    material.BaseColor = { 0.7f, 0.2f, 0.1f };
    auto local = kairo::foundation::math::Mat4f::Identity();
    local(1u, 3u) = 2.0f;
    assets.BindScene({ SceneID }, { { 41u, material, local },
        { 42u, material, kairo::foundation::math::Mat4f::Identity() } });

    kairo::engine::Scene scene;
    const auto entity = scene.CreateEntity("Imported");
    scene.SetSceneInstance(entity, { { SceneID }, true, false, true, 1u });
    scene.Transform(entity).Local.Translation = { 3.0f, 4.0f, 5.0f };
    const auto extracted = BuildRenderScene(scene, assets);
    REQUIRE(extracted.Draws().size() == 2u);
    CHECK(extracted.Draws()[0].Mesh == 41u);
    CHECK(extracted.Draws()[0].Model(0u, 3u) == 3.0f);
    CHECK(extracted.Draws()[0].Model(1u, 3u) == 6.0f);
    CHECK(extracted.Draws()[0].ObjectID == entity.Value);
    CHECK_FALSE(extracted.Draws()[0].CastShadows);
    CHECK(extracted.Draws()[1].Mesh == 42u);
    CHECK(BuildRenderScene(scene, assets, 2u).Draws().empty());
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
    scene.MeshRenderer(first).RenderLayers = 0x1u;
    scene.MeshRenderer(second).RenderLayers = 0x2u;
    scene.Transform(first).Local.Translation = { -2.0f, 0.0f, 0.0f };
    scene.Transform(second).Local.Translation = { 3.0f, 0.0f, 0.0f };
    scene.Transform(second).Local.Scale = { 0.5f, 2.0f, 0.5f };
    scene.SetParent(second, first);

    kairo::assets::AssetRegistry registry;
    RegisterRenderAssets(registry);
    RenderAssetBindings assets(registry);
    assets.BindMesh({ MeshID }, 7u);
    kairo::renderer::PBRMaterial material;
    material.BaseColor = { 0.2f, 0.4f, 0.8f };
    material.Metallic = 0.65f;
    assets.BindMaterial({ MaterialID }, material);
    const auto renderScene = BuildRenderScene(scene, assets);

    REQUIRE(renderScene.Draws().size() == 2u);
    CHECK(renderScene.Draws()[0].Mesh == 7u);
    CHECK(renderScene.Draws()[0].ObjectID == first.Value);
    CHECK(renderScene.Draws()[0].Model(0u, 3u) == -2.0f);
    CHECK(renderScene.Draws()[0].Material.BaseColor.z == 0.8f);
    CHECK(renderScene.Draws()[0].Material.Metallic == 0.65f);
    CHECK(renderScene.Draws()[1].ObjectID == second.Value);
    CHECK(renderScene.Draws()[1].Model(0u, 3u) == 1.0f);
    CHECK(renderScene.Draws()[1].Model(1u, 1u) == 2.0f);
    const auto firstLayer = BuildRenderScene(scene, assets, 0x1u);
    REQUIRE(firstLayer.Draws().size() == 1u);
    CHECK(firstLayer.Draws()[0].ObjectID == first.Value);
    REQUIRE_THROWS_AS(BuildRenderScene(scene, assets, 0u), std::invalid_argument);
}

TEST_CASE("Engine scenes extract authored lights environments and shadow policy",
    "[KairoEditor][RenderBridge][Lighting]")
{
    kairo::engine::Scene scene;
    const auto lightEntity = scene.CreateEntity("Key Light");
    kairo::engine::LightComponent light;
    light.Type = kairo::engine::LightType::Spot;
    light.Unit = kairo::engine::PhotometricUnit::Candela;
    light.Intensity = 800.0f;
    light.Shadows = kairo::engine::ShadowPolicy::Disabled;
    light.RenderLayers = 0x4u;
    scene.SetLight(lightEntity, light);
    scene.Transform(lightEntity).Local.Translation = { 2.0f, 3.0f, 4.0f };
    const auto environmentEntity = scene.CreateEntity("World");
    kairo::engine::EnvironmentComponent environment;
    environment.BackgroundColor = { 0.1f, 0.2f, 0.3f };
    environment.AmbientIntensity = 0.25f;
    environment.EnvironmentIntensity = 2.0f;
    environment.ExposureEV100 = 1.5f;
    environment.EnvironmentTexture = kairo::assets::TextureAssetHandle{ TextureID };
    scene.SetEnvironment(environmentEntity, environment);

    kairo::assets::AssetRegistry registry;
    RegisterRenderAssets(registry);
    RenderAssetBindings bindings(registry);
    bindings.BindTexture({ TextureID }, 91u);
    const auto extracted = BuildRenderScene(scene, bindings);
    REQUIRE(extracted.Lights().size() == 1u);
    CHECK(extracted.Lights()[0].Type == kairo::renderer::RenderLightType::Spot);
    CHECK(extracted.Lights()[0].Intensity == 8.0f);
    CHECK_FALSE(extracted.Lights()[0].CastShadows);
    CHECK(extracted.Environment().BackgroundColor.y == 0.2f);
    CHECK(extracted.Environment().AmbientIntensity == 0.5f);
    CHECK(extracted.Environment().ExposureEV100 == 1.5f);
    CHECK(extracted.Environment().EnvironmentIntensity == 2.0f);
    CHECK(extracted.Environment().EnvironmentTexture == 91u);
    CHECK(BuildRenderScene(scene, bindings, 0x2u).Lights().empty());
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

TEST_CASE("Builtin primitive metadata maps to renderer geometry once", "[KairoEditor][RenderBridge][Builtin]")
{
    kairo::assets::AssetMetadata plane{ MeshID, kairo::assets::AssetType::Mesh,
        kairo::assets::AssetOrigin::Builtin, "builtin/plane", "kairo.builtin.plane", 1u, {} };
    const auto mesh = MakeBuiltinRenderMesh(plane);
    REQUIRE(mesh.has_value());
    CHECK(mesh->Indices().size() == 6u);

    plane.Importer = "kairo.builtin.cube";
    const auto cube = MakeBuiltinRenderMesh(plane);
    REQUIRE(cube.has_value());
    CHECK(cube->Indices().size() == 36u);

    plane.Importer = "kairo.builtin.unknown";
    CHECK_FALSE(MakeBuiltinRenderMesh(plane).has_value());
    plane.Type = kairo::assets::AssetType::Material;
    CHECK_FALSE(MakeBuiltinRenderMesh(plane).has_value());
}

TEST_CASE("Source OBJ meshes import through shared assets into renderer geometry", "[KairoEditor][RenderBridge][Import]")
{
    const auto root = std::filesystem::temp_directory_path() /
        ("kairo-editor-mesh-import-" + kairo::assets::GenerateAssetID().ToString());
    std::filesystem::create_directories(root / "Meshes");
    struct Cleanup final
    {
        std::filesystem::path Root;
        ~Cleanup() { std::error_code error; std::filesystem::remove_all(Root, error); }
    } cleanup{ root };
    {
        std::ofstream source(root / "Meshes" / "Triangle.obj");
        REQUIRE(source.good());
        source << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }

    kairo::assets::AssetRegistry registry;
    registry.Insert({ MeshID, kairo::assets::AssetType::Mesh,
        kairo::assets::AssetOrigin::SourceFile, "Meshes/Triangle.obj", "kairo.obj", 1u, {} });
    kairo::assets::ImportDatabase imports;
    const kairo::assets::DerivedDataCache cache(root / ".kairo" / "derived-data");

    const auto first = ImportRenderMesh(root, { MeshID }, registry, imports, cache);
    REQUIRE(first.Geometry.Vertices().size() == 3u);
    CHECK(first.Geometry.Indices() == std::vector<std::uint32_t>{ 0u, 1u, 2u });
    CHECK_FALSE(first.CacheHit);
    CHECK(cache.Contains(first.CacheKey));

    const auto second = ImportRenderMesh(root, { MeshID }, registry, imports, cache);
    CHECK(second.CacheHit);
    CHECK(second.CacheKey == first.CacheKey);
    CHECK(second.Geometry.Vertices().size() == first.Geometry.Vertices().size());

    kairo::assets::AssetRegistry unsupportedRegistry;
    unsupportedRegistry.Insert({ MeshID, kairo::assets::AssetType::Mesh,
        kairo::assets::AssetOrigin::SourceFile, "Meshes/Triangle.obj", "kairo.unknown", 1u, {} });
    REQUIRE_THROWS_AS(ImportRenderMesh(
        root, { MeshID }, unsupportedRegistry, imports, cache), std::invalid_argument);
}
