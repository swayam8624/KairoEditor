#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

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

TEST_CASE("Builtin primitive metadata maps to renderer geometry once", "[KairoEditor][RenderBridge][Builtin]")
{
    kairo::assets::AssetMetadata plane{ MeshID, kairo::assets::AssetType::Mesh,
        kairo::assets::AssetOrigin::Builtin, "builtin/plane", "kairo.builtin.plane", 1u, {} };
    const auto mesh = MakeBuiltinRenderMesh(plane);
    REQUIRE(mesh.has_value());
    CHECK(mesh->Indices().size() == 6u);

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
