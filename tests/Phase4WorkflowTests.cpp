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
import Kairo.Editor.SceneCommands;
import Kairo.Editor.ProjectSession;
import Kairo.Editor.CommandHistory;
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
        { .Folder = "Textures", .Search = "", .Type = AssetType::Texture2D });
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

TEST_CASE("Rendering component commands add edit remove and undo complete values")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    const auto root = TemporaryRoot("render-components");
    ProjectSession project;
    project.CreateProject(root, "Rendering Components");
    const Entity entity = project.EditScene().CreateEntity("Rendering");
    CommandHistory history;

    CameraComponent camera;
    camera.Primary = true;
    camera.VerticalFovRadians = 0.8f;
    history.Execute(std::make_unique<SetCameraComponentCommand>(project, entity, camera));
    REQUIRE(project.Scene().HasCamera(entity));
    CHECK(project.Scene().Camera(entity).Primary);
    history.Undo();
    CHECK_FALSE(project.Scene().HasCamera(entity));
    history.Redo();
    CHECK(project.Scene().Camera(entity).VerticalFovRadians == 0.8f);

    LightComponent light;
    light.Type = LightType::Point;
    light.Unit = PhotometricUnit::Candela;
    light.Intensity = 650.0f;
    history.Execute(std::make_unique<SetLightComponentCommand>(project, entity, light));
    REQUIRE(project.Scene().HasLight(entity));
    CHECK(project.Scene().Light(entity).Intensity == 650.0f);

    EnvironmentComponent environment;
    environment.Priority = 7;
    environment.ExposureEV100 = 1.25f;
    history.Execute(std::make_unique<SetEnvironmentComponentCommand>(
        project, entity, environment));
    REQUIRE(project.Scene().HasEnvironment(entity));
    history.Execute(std::make_unique<SetEnvironmentComponentCommand>(
        project, entity, std::nullopt));
    CHECK_FALSE(project.Scene().HasEnvironment(entity));
    history.Undo();
    CHECK(project.Scene().Environment(entity).Priority == 7);

    const auto importedID = kairo::assets::AssetID::Parse(
        "00000000-0000-4000-8000-000000000299");
    SceneInstanceComponent instance;
    instance.SceneAsset = { importedID };
    instance.CastShadows = false;
    history.Execute(std::make_unique<SetSceneInstanceComponentCommand>(
        project, entity, instance));
    REQUIRE(project.Scene().HasSceneInstance(entity));
    CHECK_FALSE(project.Scene().SceneInstance(entity).CastShadows);
    history.Undo();
    CHECK_FALSE(project.Scene().HasSceneInstance(entity));
    history.Redo();
    CHECK(project.Scene().SceneInstance(entity).SceneAsset.ID == importedID);
    std::filesystem::remove_all(root);
}

TEST_CASE("linked prefab refresh preserves stable IDs, overrides, and unrelated children")
{
    using namespace kairo::editor;
    using namespace kairo::engine;
    Scene source;
    const auto root=source.CreateEntity("Root");
    const auto child=source.CreateEntity("Child");
    source.SetParent(child,root);
    source.AddTag(child,"old");
    PrefabTemplate prefab{"Pickup",1,CaptureSceneFragment(source,{root})};
    Scene destination;
    const auto container=destination.CreateEntity("Container");
    auto a=InstantiateLinkedPrefab(destination,prefab,{{1,std::string("Custom"),std::nullopt,false}},container);
    auto b=InstantiateLinkedPrefab(destination,prefab,{},container);
    const auto aMembers=a.Members,bMembers=b.Members;
    const auto extra=destination.CreateEntity("Unrelated");
    destination.SetParent(extra,a.Members[0]);
    prefab.Version=2;
    prefab.Fragment.Entities[1].Name="Updated";
    prefab.Fragment.Entities[1].Transform.Local.Translation.x=9.f;
    prefab.Fragment.Entities[1].Tags={"new"};
    RefreshLinkedPrefab(destination,a,prefab);
    RefreshLinkedPrefab(destination,b,prefab);
    CHECK(a.Members==aMembers);CHECK(b.Members==bMembers);
    CHECK(a.Version==2);CHECK(b.Version==2);
    CHECK(destination.Parent(extra)==a.Members[0]);
    CHECK(destination.Name(a.Members[1]).Value=="Custom");
    CHECK_FALSE(destination.IsEnabled(a.Members[1]));
    CHECK(destination.Name(b.Members[1]).Value=="Updated");
    CHECK(destination.IsEnabled(b.Members[1]));
    CHECK(destination.Transform(a.Members[1]).Local.Translation.x==9.f);
    CHECK(destination.HasTag(a.Members[1],"new"));
    CHECK_FALSE(destination.HasTag(a.Members[1],"old"));
    RefreshLinkedPrefab(destination,a,prefab,{});
    CHECK(destination.Name(a.Members[1]).Value=="Updated");
    CHECK(destination.IsEnabled(a.Members[1]));
    auto malformed=prefab;malformed.Version=3;malformed.Fragment.Entities[1].Parent=-1;
    CHECK_THROWS_AS(RefreshLinkedPrefab(destination,a,malformed),std::invalid_argument);
    CHECK(a.Version==2);
    CHECK(destination.Parent(a.Members[1])==a.Members[0]);
    destination.SetParent(b.Members[1],container);
    CHECK_THROWS_AS(RefreshLinkedPrefab(destination,b,prefab),std::invalid_argument);
    CHECK(destination.Parent(b.Members[1])==container);
}
