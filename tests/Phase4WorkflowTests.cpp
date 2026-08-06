#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

import Kairo.Assets;
import Kairo.Editor.AssetBrowserModel;
import Kairo.Editor.Diagnostics;
import Kairo.Editor.ProjectDescriptor;
import Kairo.Editor.ProjectLifecycle;
import Kairo.Editor.SceneFragment;
import Kairo.Editor.SceneSelection;
import Kairo.EngineCore;

namespace
{
    [[nodiscard]] std::filesystem::path TemporaryRoot(std::string_view role)
    {
        return std::filesystem::temp_directory_path() /
            ("kairo-phase4-" + std::string(role) + "-" +
             kairo::assets::GenerateAssetID().ToString());
    }

    void WriteText(const std::filesystem::path& path, std::string_view text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
    }
}

TEST_CASE("Asset browser exposes import state, filtering and deletion blockers")
{
    using namespace kairo::assets;
    using namespace kairo::editor;
    const auto root = TemporaryRoot("assets");
    const AssetID texture = AssetID::Parse("00000000-0000-4000-8000-000000000201");
    const AssetID material = AssetID::Parse("00000000-0000-4000-8000-000000000202");
    WriteText(root / "Textures/paint.tga", "source-v1");

    AssetRegistry registry;
    registry.Insert({ texture, AssetType::Texture2D, AssetOrigin::SourceFile,
        "Textures/paint.kasset", "kairo.texture.stb", 1u, {} });
    registry.Insert({ material, AssetType::Material, AssetOrigin::Generated,
        "Materials/paint.kasset", "kairo.material", 1u,
        { { texture, AssetType::Texture2D } } });
    ImportDatabase imports;
    const std::string source = "source-v1";
    imports.Upsert({ texture, "Textures/paint.tga", "kairo.texture.stb", "1", {},
        FingerprintBytes(std::as_bytes(std::span(source.data(), source.size()))), 1u }, registry);

    const auto entries = BuildAssetBrowserEntries(root, registry, imports,
        { .Folder = "Textures", .Search = "PAINT", .Type = AssetType::Texture2D });
    REQUIRE(entries.size() == 1u);
    CHECK(entries.front().State == AssetBrowserState::Current);
    CHECK(entries.front().DirectDependentCount == 1u);
    const AssetDeletePlan plan = PlanAssetDeletion(registry, texture);
    CHECK_FALSE(plan.CanDelete());
    REQUIRE(plan.DirectDependents.size() == 1u);
    CHECK(plan.DirectDependents.front().ID == material);
    CHECK(AssetDependencyClosure(registry, material).front().ID == texture);

    WriteText(root / "Textures/paint.tga", "source-v2");
    const auto changedEntries = BuildAssetBrowserEntries(root, registry, imports,
        { .Folder = "Textures", .Type = AssetType::Texture2D });
    REQUIRE(changedEntries.size() == 1u);
    CHECK(changedEntries.front().State == AssetBrowserState::SourceChanged);
    std::filesystem::remove_all(root);
}

TEST_CASE("Scene selection supports toggle, ranges, marquee and pruning")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    Scene scene;
    const Entity first = scene.CreateEntity("First");
    const Entity second = scene.CreateEntity("Second");
    const Entity third = scene.CreateEntity("Third");
    SceneSelection selection;
    selection.SelectOnly(first);
    selection.SelectRange({ first, second, third }, third);
    CHECK(selection.Entities() == std::vector<Entity>{ first, second, third });
    selection.Toggle(second);
    CHECK_FALSE(selection.Contains(second));
    selection.ReplaceFromMarquee({ second, third, third }, false);
    CHECK(selection.Entities() == std::vector<Entity>{ second, third });
    scene.DestroyEntity(third);
    selection.Prune(scene);
    CHECK(selection.Entities() == std::vector<Entity>{ second });
    CHECK(selection.Active() == second);
}

TEST_CASE("Diagnostics replace producer output and preserve navigation targets")
{
    using namespace kairo::editor;
    DiagnosticStore store;
    store.ReplaceProducer("compiler", {
        { {}, "W001", DiagnosticSeverity::Warning, "Unused node",
            GraphDiagnosticTarget{
                kairo::assets::AssetID::Parse("00000000-0000-4000-8000-000000000211"),
                9u, 2u } },
        { {}, "E001", DiagnosticSeverity::Error, "Broken link",
            SourceDiagnosticTarget{ "Logic/Player.kdoc", 12u, 4u } }
    });
    CHECK(store.Count(DiagnosticSeverity::Error) == 1u);
    CHECK(store.Snapshot().front().Code == "E001");
    CHECK(store.NextNavigable().has_value());
    store.ReplaceProducer("compiler", {
        { {}, "I001", DiagnosticSeverity::Information, "Compiled", {} }
    });
    CHECK(store.Snapshot().size() == 1u);
    CHECK(store.Snapshot().front().Code == "I001");
}

TEST_CASE("Scene fragments and prefabs preserve hierarchy and overrides atomically")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    Scene source;
    const Entity root = source.CreateEntity("Root");
    const Entity child = source.CreateEntity("Child");
    source.SetParent(child, root);
    source.AddTag(child, "collectible");
    source.Transform(child).Local.Translation.x = 4.0f;
    const SceneFragment fragment = CaptureSceneFragment(source, { root });
    REQUIRE(fragment.Entities.size() == 2u);

    Scene destination;
    const Entity container = destination.CreateEntity("Container");
    PrefabTemplate prefab{ "Pickup", 1u, fragment };
    const auto created = InstantiatePrefab(destination, prefab,
        { { 1u, std::string("Overridden Child"), std::nullopt, false } }, container);
    REQUIRE(created.size() == 2u);
    CHECK(destination.Parent(created[0]) == container);
    CHECK(destination.Parent(created[1]) == created[0]);
    CHECK(destination.Name(created[1]).Value == "Overridden Child");
    CHECK_FALSE(destination.IsEnabled(created[1]));
    CHECK(destination.HasTag(created[1], "collectible"));
    CHECK(destination.Transform(created[1]).Local.Translation.x == 4.0f);
}

TEST_CASE("Project lifecycle clones templates and persists bounded recents")
{
    using namespace kairo::editor;
    const auto root = TemporaryRoot("project");
    const auto source = root / "Template";
    const auto destination = root / "Clone";
    std::filesystem::create_directories(source / "Scenes");
    ProjectDescriptor descriptor{ "Template", "Assets.kassets", "Scenes/Main.kscene" };
    SaveProjectDescriptor(source / "Project.kproject", descriptor);
    WriteText(source / "Assets.kassets", "kairo-assets 1\n");
    WriteText(source / "Scenes/Main.kscene", "kairo-scene 2\n");
    CloneProjectDirectory(source / "Project.kproject", destination);
    CHECK(std::filesystem::is_regular_file(destination / "Project.kproject"));

    RecentProjects recents;
    recents.Touch(destination / "Project.kproject");
    recents.Touch(source / "Project.kproject");
    const auto recentFile = root / "settings/recent.txt";
    recents.Save(recentFile);
    const RecentProjects restored = RecentProjects::Load(recentFile);
    REQUIRE(restored.Entries().size() == 2u);
    CHECK(restored.Entries().front().filename() == "Project.kproject");
    std::filesystem::remove_all(root);
}
