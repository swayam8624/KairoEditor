#include <catch2/catch_test_macros.hpp>
#include <filesystem>

import Kairo.Assets;
import Kairo.Editor.AssetWorkspace;

TEST_CASE("Asset workspace sorts paths and exposes reverse dependencies")
{
    using namespace kairo::assets;
    using namespace kairo::editor;

    AssetRegistry registry;
    ImportDatabase imports;
    const AssetID texture = registry.Create({ AssetType::Texture2D, AssetOrigin::Generated,
        "Textures/Zebra.ktex", "kairo.generated", {} });
    const AssetID material = registry.Create({ AssetType::Material, AssetOrigin::Generated,
        "Materials/Main.kmat", "kairo.generated", { { texture, AssetType::Texture2D } } });
    const AssetID mesh = registry.Create({ AssetType::Mesh, AssetOrigin::Builtin,
        "Meshes/Cube.mesh", "kairo.builtin", {} });

    const AssetWorkspace workspace = AssetWorkspace::Build(std::filesystem::current_path(), registry, imports);
    REQUIRE(workspace.Entries().size() == 3u);
    CHECK(workspace.Entries()[0].Metadata.Path.generic_string() == "Materials/Main.kmat");
    CHECK(workspace.Entries()[1].Metadata.Path.generic_string() == "Meshes/Cube.mesh");
    CHECK(workspace.Entries()[2].Metadata.Path.generic_string() == "Textures/Zebra.ktex");
    CHECK(workspace.At(material).Status == AssetWorkspaceStatus::Generated);
    CHECK(workspace.At(mesh).Status == AssetWorkspaceStatus::Builtin);
    REQUIRE(workspace.DeleteBlockers(texture).size() == 1u);
    CHECK(workspace.DeleteBlockers(texture)[0].ID == material);
    CHECK_FALSE(workspace.At(texture).CanDelete());
    CHECK(workspace.At(material).CanDelete());
}

TEST_CASE("Asset workspace filters by type status and path text")
{
    using namespace kairo::assets;
    using namespace kairo::editor;

    AssetRegistry registry;
    ImportDatabase imports;
    registry.Create({ AssetType::Texture2D, AssetOrigin::Generated,
        "Textures/Brick_BaseColor.ktex", "kairo.generated", {} });
    registry.Create({ AssetType::Mesh, AssetOrigin::Builtin,
        "Meshes/Brick.mesh", "kairo.builtin", {} });
    registry.Create({ AssetType::Scene, AssetOrigin::SourceFile,
        "Scenes/Level.scene", "kairo.scene", {} });

    const AssetWorkspace workspace = AssetWorkspace::Build(std::filesystem::current_path(), registry, imports);
    AssetWorkspaceFilter filter;
    filter.Type = AssetType::Texture2D;
    filter.Search = "BRICK";
    const auto textures = workspace.Filter(filter);
    REQUIRE(textures.size() == 1u);
    CHECK(textures[0].Metadata.Type == AssetType::Texture2D);

    AssetWorkspaceFilter generatedOnly;
    generatedOnly.IncludeCurrent = false;
    generatedOnly.IncludeChanged = false;
    generatedOnly.IncludeMissing = false;
    generatedOnly.IncludeBuiltin = false;
    const auto generated = workspace.Filter(generatedOnly);
    REQUIRE(generated.size() == 1u);
    CHECK(generated[0].Status == AssetWorkspaceStatus::Generated);
}
