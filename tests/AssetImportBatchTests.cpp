#include <catch2/catch_test_macros.hpp>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <system_error>
#include <vector>

import Kairo.Editor;
import Kairo.Editor.AssetImport;
import Kairo.Assets;

TEST_CASE("Batch import validates assets atomically and registers compound dependencies",
    "[KairoEditor][AssetImport]")
{
    using namespace kairo::editor;
    using namespace kairo::assets;
    const auto root=std::filesystem::temp_directory_path()/("kairo-batch-"+GenerateAssetID().ToString());
    struct Cleanup{std::filesystem::path Root;~Cleanup(){std::error_code ec;std::filesystem::remove_all(Root,ec);}} cleanup{root};
    std::filesystem::create_directories(root/"Sources");
    ProjectSession project;
    project.CreateProject(root/"Project","Import regression");
    const auto obj=root/"Sources/triangle.obj";
    const auto tga=root/"Sources/pixel.tga";
    const auto invalid=root/"Sources/bad.wav";
    {
        std::ofstream out(obj);out<<"v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    {
        const std::array<unsigned char,22> pixels{0,0,2,0,0,0,0,0,0,0,0,0,1,0,1,0,32,0x28,0,0,255,255};
        std::ofstream out(tga,std::ios::binary);
        out.write(reinterpret_cast<const char*>(pixels.data()),sizeof(pixels));
    }
    {std::ofstream out(invalid);out<<"not a WAV";}
    const auto baseline=SerializeAssetManifest(project.Assets());
    const std::array badBatch{obj,invalid};
    CHECK_THROWS(ImportAssetBatch(project,badBatch));
    AssetRegistry persisted;
    const auto manifest=project.ProjectRoot()/project.Descriptor().AssetManifest;
    LoadAssetManifest(manifest,persisted);
    CHECK(SerializeAssetManifest(persisted)==baseline);
    const auto imports=project.ProjectRoot()/"Assets/Imported";
    CHECK(std::filesystem::is_empty(imports));
    const std::array goodBatch{obj,tga};
    const auto ids=ImportAssetBatch(project,goodBatch);
    REQUIRE(ids.size()==2);
    LoadAssetManifest(manifest,persisted);
    CHECK(persisted.At(ids[0]).Type==AssetType::Mesh);
    CHECK(persisted.At(ids[1]).Type==AssetType::Texture2D);
    CHECK(std::filesystem::is_regular_file(project.ProjectRoot()/persisted.At(ids[0]).Path));
    CHECK_THROWS(ImportAssetBatch(project,goodBatch)); // stale open registry
    project.OpenProject(project.ProjectFile());
    const auto gltf=root/"Sources/model.gltf";
    {
        std::ofstream out(gltf);
        out<<R"json({"asset":{"version":"2.0"},"buffers":[{"byteLength":42,"uri":"mesh.bin"}],
          "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
          "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
          {"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
          "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
          "nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})json";
    }
    {
        const std::array<float,9> positions{0,0,0,1,0,0,0,1,0};
        const std::array<unsigned short,3> indices{0,1,2};
        std::ofstream out(root/"Sources/mesh.bin",std::ios::binary);
        out.write(reinterpret_cast<const char*>(positions.data()),sizeof(positions));
        out.write(reinterpret_cast<const char*>(indices.data()),sizeof(indices));
    }
    const std::array modelBatch{gltf};
    const auto models=ImportAssetBatch(project,modelBatch);
    REQUIRE(models.size()==1);
    LoadAssetManifest(manifest,persisted);
    const auto model=persisted.At(models[0]);
    CHECK(model.Type==AssetType::Scene);
    REQUIRE(model.Dependencies.size()==1);
    CHECK(model.Dependencies.front().Type==AssetType::Other);
    CHECK(std::filesystem::is_regular_file(project.ProjectRoot()/persisted.At(model.Dependencies.front().ID).Path));
    CHECK_FALSE(std::filesystem::exists(project.ProjectRoot()/model.Path.parent_path()/".validation-cache"));
}
