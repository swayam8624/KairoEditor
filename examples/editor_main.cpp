#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#if !defined(_WIN32)
#include <unistd.h>
#endif

import Kairo.Editor;
import Kairo.AI;
import Kairo.Editor.Theme;
import Kairo.Editor.ImGuiRuntime;
import Kairo.Editor.ImGuiShell;
import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;
import Kairo.Renderer;
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
import Kairo.Runtime.RenderBridge.EditorOfflineService;
import Kairo.Runtime.RenderBridge.SceneSnapshot;
import Kairo.Foundation.RayTracer;
import Kairo.Foundation.Math;
import Kairo.Foundation.Geometry.Triangle;
#endif
#if defined(KAIRO_EDITOR_HAS_AI_CLOUD)
import Kairo.AI.Cloud;
#endif

namespace
{
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
    [[nodiscard]] kairo::foundation::raytracer::Material MakeOfflineMaterial(
        const kairo::renderer::PBRMaterial& source)
    {
        kairo::foundation::raytracer::Material result;
        result.Name = "EngineCore PBR material";
        result.Type = kairo::foundation::raytracer::MaterialType::PBR;
        result.Albedo = { source.BaseColor.x, source.BaseColor.y, source.BaseColor.z };
        result.Emission = { source.Emissive.x, source.Emissive.y, source.Emissive.z };
        result.Roughness = source.Roughness;
        result.Metallic = source.Metallic;
        return result;
    }

    [[nodiscard]] kairo::foundation::raytracer::TriangleMesh MakeOfflineMesh(
        const kairo::renderer::Mesh& source,
        const kairo::foundation::math::Mat4f& model)
    {
        namespace math = kairo::foundation::math;
        namespace ray = kairo::foundation::raytracer;
        ray::TriangleMesh result;
        result.Triangles.reserve(source.Indices().size() / 3u);
        for (std::size_t offset = 0u; offset < source.Indices().size(); offset += 3u)
        {
            const auto a = source.Indices()[offset];
            const auto b = source.Indices()[offset + 1u];
            const auto c = source.Indices()[offset + 2u];
            const auto& va = source.Vertices().at(a);
            const auto& vb = source.Vertices().at(b);
            const auto& vc = source.Vertices().at(c);
            ray::MeshTriangle triangle;
            triangle.Triangle = kairo::foundation::geometry::Trianglef::FromPoints(
                math::TransformPoint(model, va.Position),
                math::TransformPoint(model, vb.Position),
                math::TransformPoint(model, vc.Position));
            triangle.NormalA = math::TransformNormal(model, va.Normal);
            triangle.NormalB = math::TransformNormal(model, vb.Normal);
            triangle.NormalC = math::TransformNormal(model, vc.Normal);
            triangle.UVA = va.TexCoord;
            triangle.UVB = vb.TexCoord;
            triangle.UVC = vc.TexCoord;
            triangle.HasVertexNormals = true;
            triangle.HasUVs = true;
            result.Triangles.push_back(std::move(triangle));
        }
        return result;
    }

    [[nodiscard]] float DecodeHalf(std::uint16_t bits)
    {
        const float sign = (bits & 0x8000u) != 0u ? -1.0f : 1.0f;
        const std::uint16_t exponent = (bits >> 10u) & 0x1fu;
        const std::uint16_t mantissa = bits & 0x03ffu;
        if (exponent == 0u)
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        if (exponent == 31u)
            throw std::invalid_argument("Offline texture contains a non-finite binary16 texel.");
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
            static_cast<int>(exponent) - 15);
    }

    [[nodiscard]] kairo::foundation::raytracer::Texture2D MakeOfflineTexture(
        const kairo::assets::TextureArtifactData& source,
        std::string name)
    {
        kairo::assets::ValidateTextureArtifactData(source);
        const auto& mip = source.Mips.front();
        kairo::foundation::raytracer::Texture2D result;
        result.Name = std::move(name);
        result.Width = mip.Width;
        result.Height = mip.Height;
        result.Pixels.reserve(static_cast<std::size_t>(mip.Width) * mip.Height);
        const auto byte = [&](std::size_t index)
        {
            return std::to_integer<std::uint8_t>(mip.Pixels.at(index));
        };
        const auto linear = [&](float value)
        {
            if (source.ColorSpace != kairo::assets::TextureColorSpace::SRGB) return value;
            return value <= 0.04045f ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        const std::size_t count = static_cast<std::size_t>(mip.Width) * mip.Height;
        for (std::size_t pixel = 0u; pixel < count; ++pixel)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (source.Format == kairo::assets::TexturePixelFormat::R8)
            {
                r = g = b = static_cast<float>(byte(pixel)) / 255.0f;
            }
            else if (source.Format == kairo::assets::TexturePixelFormat::RG8)
            {
                r = static_cast<float>(byte(pixel * 2u)) / 255.0f;
                g = static_cast<float>(byte(pixel * 2u + 1u)) / 255.0f;
            }
            else if (source.Format == kairo::assets::TexturePixelFormat::RGBA8)
            {
                r = static_cast<float>(byte(pixel * 4u)) / 255.0f;
                g = static_cast<float>(byte(pixel * 4u + 1u)) / 255.0f;
                b = static_cast<float>(byte(pixel * 4u + 2u)) / 255.0f;
            }
            else
            {
                const auto half = [&](std::size_t channel)
                {
                    const std::size_t offset = pixel * 8u + channel * 2u;
                    return DecodeHalf(static_cast<std::uint16_t>(byte(offset)) |
                        (static_cast<std::uint16_t>(byte(offset + 1u)) << 8u));
                };
                r = half(0u); g = half(1u); b = half(2u);
            }
            result.Pixels.push_back({ linear(r), linear(g), linear(b) });
        }
        return result;
    }
#endif

    struct AIHost final
    {
        std::shared_ptr<kairo::ai::Provider> Provider;
        std::string Model;
    };

    [[nodiscard]] AIHost CreateAIHost()
    {
#if defined(KAIRO_EDITOR_HAS_AI_CLOUD)
        const char* key = std::getenv("KAIRO_AI_API_KEY");
        const char* model = std::getenv("KAIRO_AI_MODEL");
        if (key == nullptr || *key == '\0' || model == nullptr || *model == '\0') return {};
        kairo::ai::OpenAICompatibleConfig config;
        config.APIKey = key;
        if (const char* endpoint = std::getenv("KAIRO_AI_ENDPOINT");
            endpoint != nullptr && *endpoint != '\0')
            config.Endpoint = endpoint;
        return { std::make_shared<kairo::ai::OpenAICompatibleProvider>(
            std::move(config), std::make_shared<kairo::ai::CprChatTransport>()), model };
#else
        return {};
#endif
    }

    struct AppOptions final
    {
        std::filesystem::path Project;
        std::optional<std::filesystem::path> Document;
        std::optional<std::filesystem::path> RecoverySnapshot;
        std::optional<kairo::editor::AuthoringSurface> AuthoringSurface;
        std::optional<std::uint64_t> FrameLimit;
        std::optional<std::filesystem::path> Screenshot;
        std::optional<kairo::renderer::ViewportShadingMode> ViewportShading;
        std::optional<kairo::renderer::GraphicsBackend> BackendOverride;
        bool PersistLayout = true;
    };

    [[nodiscard]] AppOptions ParseOptions(int argc, char** argv)
    {
        AppOptions options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--no-layout-persistence")
            {
                options.PersistLayout = false;
                continue;
            }
            if (argument == "--project")
            {
                if (++index == argc) throw std::invalid_argument("--project requires a .kproject path.");
                options.Project = argv[index];
                continue;
            }
            if (argument == "--document")
            {
                if (++index == argc) throw std::invalid_argument("--document requires a project-relative .kdoc path.");
                options.Document = std::filesystem::path(argv[index]);
                continue;
            }
            if (argument == "--recovery-snapshot")
            {
                if (++index == argc) throw std::invalid_argument(
                    "--recovery-snapshot requires a published snapshot directory.");
                options.RecoverySnapshot = std::filesystem::path(argv[index]);
                options.PersistLayout = false;
                continue;
            }
            if (argument == "--authoring")
            {
                if (++index == argc) throw std::invalid_argument("--authoring requires code, graph, or split.");
                const std::string_view surface(argv[index]);
                if (surface == "code") options.AuthoringSurface = kairo::editor::AuthoringSurface::Code;
                else if (surface == "graph") options.AuthoringSurface = kairo::editor::AuthoringSurface::Graph;
                else if (surface == "split") options.AuthoringSurface = kairo::editor::AuthoringSurface::CodeAndGraph;
                else throw std::invalid_argument("--authoring requires code, graph, or split.");
                continue;
            }
            if (argument == "--screenshot")
            {
                if (++index == argc) throw std::invalid_argument("--screenshot requires an output .ppm path.");
                options.Screenshot = std::filesystem::path(argv[index]);
                continue;
            }
            if (argument == "--viewport-mode")
            {
                if (++index == argc) throw std::invalid_argument("--viewport-mode requires lit, unlit, normals, or lighting.");
                const std::string_view mode(argv[index]);
                if (mode == "lit") options.ViewportShading = kairo::renderer::ViewportShadingMode::Lit;
                else if (mode == "unlit") options.ViewportShading = kairo::renderer::ViewportShadingMode::Unlit;
                else if (mode == "normals") options.ViewportShading = kairo::renderer::ViewportShadingMode::Normals;
                else if (mode == "lighting") options.ViewportShading = kairo::renderer::ViewportShadingMode::Lighting;
                else throw std::invalid_argument("--viewport-mode requires lit, unlit, normals, or lighting.");
                continue;
            }
            if (argument == "--renderer")
            {
                if (++index == argc)
                    throw std::invalid_argument(
                        "--renderer requires auto, vulkan, metal, d3d12, or opengl.");
                options.BackendOverride =
                    kairo::renderer::ParseGraphicsBackend(argv[index]);
                continue;
            }
            if (argument != "--frames")
                throw std::invalid_argument("Unknown option: " + std::string(argument));
            if (++index == argc) throw std::invalid_argument("--frames requires a positive integer.");

            std::uint64_t value = 0u;
            const std::string_view text(argv[index]);
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc{} || end != text.data() + text.size() || value == 0u)
                throw std::invalid_argument("--frames requires a positive integer.");
            options.FrameLimit = value;
        }
        return options;
    }

    void WriteCapture(const std::filesystem::path& path,
        const kairo::renderer::ViewportCapture& capture)
    {
        if (!capture.IsVisuallyNonUniform())
            throw std::runtime_error("Viewport screenshot rejected a blank or uniform render target.");
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Cannot create viewport screenshot: " + path.string());
        output << "P6\n" << capture.Width << ' ' << capture.Height << "\n255\n";
        for (std::size_t index = 0u; index < capture.RGBA.size(); index += 4u)
            output.write(reinterpret_cast<const char*>(capture.RGBA.data() + index), 3);
        if (!output) throw std::runtime_error("Failed while writing viewport screenshot: " + path.string());
    }
}

int main(int argc, char** argv)
{
    try
    {
        AppOptions options = ParseOptions(argc, argv);
        if (options.Project.empty())
        {
            if (argc != 1)
                throw std::invalid_argument("--project is required when automation options are supplied.");
            const auto selected = kairo::editor::ChooseProjectFile();
            if (!selected.has_value()) return 0;
            options.Project = *selected;
        }
        const auto recentProjectsPath = kairo::editor::DefaultRecentProjectsPath();
        if (!recentProjectsPath.empty())
        {
            try
            {
                auto recent = kairo::editor::RecentProjects::Load(recentProjectsPath);
                recent.Touch(options.Project);
                recent.PruneMissing();
                recent.Save(recentProjectsPath);
            }
            catch (const std::exception& error)
            {
                std::cerr << "KairoEditor recent-projects warning: " << error.what() << '\n';
            }
        }
        kairo::editor::ProjectSession project;
        project.OpenProject(options.Project);
        std::optional<kairo::editor::RecoverySnapshot> recovered;
        if (options.RecoverySnapshot.has_value())
        {
            recovered = kairo::editor::LoadRecoverySnapshot(*options.RecoverySnapshot);
            const auto requested = kairo::editor::CanonicalExistingFile(
                options.Project, "requested project descriptor");
            const auto recorded = std::filesystem::canonical(
                recovered->OriginalProjectRoot / recovered->ProjectFile);
            if (requested != recorded)
                throw std::invalid_argument(
                    "Recovery snapshot belongs to a different project descriptor.");
            project.RestoreRecoveryPoint(recovered->Directory,
                kairo::editor::UnsavedChangesPolicy::Discard);
        }
        if (options.Document.has_value()) (void)project.OpenDocument(*options.Document);
        const kairo::renderer::GraphicsBackend backend =
            options.BackendOverride.value_or(
                kairo::renderer::ParseGraphicsBackend(
                    project.Descriptor().GraphicsBackend));
        kairo::renderer::RendererRuntime renderer({
            project.Descriptor().Name, 1600u, 1000u, true, backend });
        kairo::editor::EditorState state(project.Scene());
        if (const auto entities = project.Scene().Entities(); !entities.empty()) state.Select(entities.front());
        kairo::editor::RenderAssetBindings renderAssets(project.Assets());
        kairo::assets::ImportDatabase meshImports;
        const kairo::assets::DerivedDataCache derivedCache(
            project.ProjectRoot() / ".kairo" / "derived-data");
        std::unordered_map<kairo::assets::AssetID, kairo::assets::MaterialArtifactData,
            kairo::assets::AssetIDHash> materialArtifacts;
        std::unordered_map<kairo::assets::AssetID, kairo::assets::TextureImportSettings,
            kairo::assets::AssetIDHash> textureSettings;
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::Mesh,
            kairo::assets::AssetIDHash> offlineMeshes;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::PBRMaterial,
            kairo::assets::AssetIDHash> offlineMaterials;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::GltfRenderAsset,
            kairo::assets::AssetIDHash> offlineScenes;
        std::unordered_map<kairo::assets::AssetID, kairo::foundation::raytracer::Texture2D,
            kairo::assets::AssetIDHash> offlineTextures;
        std::unordered_map<kairo::renderer::TextureHandle,
            kairo::foundation::raytracer::Texture2D> offlineTextureHandles;
#endif
        const auto requireTexture = [&](const std::optional<kairo::assets::TextureAssetHandle>& texture,
            kairo::assets::TextureColorSpace colorSpace, bool normalMap)
        {
            if (!texture.has_value()) return;
            kairo::assets::TextureImportSettings settings;
            settings.ColorSpace = colorSpace;
            settings.NormalMap = normalMap;
            const auto [found, inserted] = textureSettings.emplace(texture->ID, settings);
            if (!inserted && (found->second.ColorSpace != settings.ColorSpace ||
                found->second.NormalMap != settings.NormalMap))
                throw std::invalid_argument("One texture asset is referenced with incompatible color/data semantics: " +
                    texture->ID.ToString());
        };
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Material ||
                asset.Origin == kairo::assets::AssetOrigin::Builtin) continue;
            auto material = kairo::editor::LoadRenderMaterial(
                project.ProjectRoot(), { asset.ID }, project.Assets());
            requireTexture(material.Textures.BaseColor, kairo::assets::TextureColorSpace::SRGB, false);
            requireTexture(material.Textures.Normal, kairo::assets::TextureColorSpace::Linear, true);
            requireTexture(material.Textures.MetallicRoughness, kairo::assets::TextureColorSpace::Linear, false);
            requireTexture(material.Textures.Emissive, kairo::assets::TextureColorSpace::SRGB, false);
            requireTexture(material.Textures.Occlusion, kairo::assets::TextureColorSpace::Linear, false);
            materialArtifacts.emplace(asset.ID, std::move(material));
        }
        if (const auto environmentEntity = project.Scene().ActiveEnvironment();
            environmentEntity.has_value())
            requireTexture(project.Scene().Environment(*environmentEntity).EnvironmentTexture,
                kairo::assets::TextureColorSpace::Linear, false);
        for (const auto& [id, settings] : textureSettings)
        {
            const auto texture = kairo::editor::ImportRenderTexture(project.ProjectRoot(), { id },
                settings, project.Assets(), meshImports, derivedCache);
            const auto handle = renderer.CreateTexture(texture);
            renderAssets.BindTexture({ id }, handle);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            auto offline = MakeOfflineTexture(texture, id.ToString());
            offlineTextureHandles.emplace(handle, offline);
            offlineTextures.emplace(id, std::move(offline));
#endif
        }
        for (const auto& [id, artifact] : materialArtifacts)
        {
            const auto material = kairo::renderer::MakePBRMaterial(artifact,
                [&renderAssets](kairo::assets::TextureAssetHandle texture)
                {
                    return renderAssets.ResolveTexture(texture);
                });
            renderAssets.BindMaterial({ id }, material);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            offlineMaterials.emplace(id, material);
#endif
        }
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Mesh) continue;
            if (asset.Origin == kairo::assets::AssetOrigin::SourceFile)
            {
                auto imported = kairo::editor::ImportRenderMesh(
                    project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports, derivedCache);
                renderAssets.BindMesh({ asset.ID }, renderer.CreateMesh(imported.Geometry));
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                offlineMeshes.emplace(asset.ID, std::move(imported.Geometry));
#endif
            }
            else if (const auto builtin = kairo::editor::MakeBuiltinRenderMesh(asset); builtin.has_value())
            {
                renderAssets.BindMesh({ asset.ID }, renderer.CreateMesh(*builtin));
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                offlineMeshes.emplace(asset.ID, *builtin);
#endif
            }
        }
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Scene ||
                asset.Origin != kairo::assets::AssetOrigin::SourceFile) continue;
            const auto resolveGltfTexture = [&](std::string_view uri,
                kairo::assets::TextureSemantic semantic)
            {
                const auto path = (asset.Path.parent_path() /
                    std::filesystem::path(uri)).lexically_normal();
                const auto metadata = project.Assets().FindByPath(path);
                if (!metadata.has_value() ||
                    metadata->Type != kairo::assets::AssetType::Texture2D)
                    throw std::invalid_argument(
                        "glTF image URI is not registered as a project texture asset: " +
                        path.generic_string());
                if (const auto existing = textureSettings.find(metadata->ID);
                    existing != textureSettings.end())
                    return renderAssets.ResolveTexture({ metadata->ID });
                kairo::assets::TextureImportSettings settings;
                settings.ColorSpace = semantic == kairo::assets::TextureSemantic::Color
                    ? kairo::assets::TextureColorSpace::SRGB
                    : kairo::assets::TextureColorSpace::Linear;
                settings.NormalMap = semantic == kairo::assets::TextureSemantic::Normal;
                const auto texture = kairo::editor::ImportRenderTexture(
                    project.ProjectRoot(), { metadata->ID }, settings,
                    project.Assets(), meshImports, derivedCache);
                textureSettings.emplace(metadata->ID, settings);
                const auto handle = renderer.CreateTexture(texture);
                renderAssets.BindTexture({ metadata->ID }, handle);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                auto offline = MakeOfflineTexture(texture, metadata->ID.ToString());
                offlineTextureHandles.emplace(handle, offline);
                offlineTextures.emplace(metadata->ID, std::move(offline));
#endif
                return handle;
            };
            const auto imported = kairo::editor::ImportRenderGltfScene(
                project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports,
                derivedCache, resolveGltfTexture);
            std::vector<kairo::editor::RenderAssetBindings::ScenePrimitive> primitives;
            primitives.reserve(imported.Primitives.size());
            for (const auto& primitive : imported.Primitives)
                primitives.push_back({ renderer.CreateMesh(primitive.Geometry),
                    primitive.Material, primitive.LocalToAsset });
            renderAssets.BindScene({ asset.ID }, std::move(primitives));
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            offlineScenes.emplace(asset.ID, std::move(imported));
#endif
        }
        const std::filesystem::path layoutFile = options.PersistLayout
            ? project.ProjectRoot() / ".kairo" / "editor-layout.ini" : std::filesystem::path{};
        const kairo::editor::EditorLayoutPlan layoutPlan = kairo::editor::PrepareEditorLayout(layoutFile);
        kairo::editor::ImGuiRuntime imgui(renderer, layoutFile);
        kairo::editor::ApplyKairoEditorTheme();
        const std::filesystem::path keymapSettings = kairo::editor::DefaultKeymapSettingsPath();
        kairo::editor::EditorKeymapSettings keymap;
        try { keymap = kairo::editor::LoadEditorKeymapSettings(keymapSettings); }
        catch (const std::exception& error)
        {
            std::cerr << "KairoEditor keymap settings warning: " << error.what()
                << " Using the Kairo profile.\n";
        }
        const std::filesystem::path navigationSettingsPath =
            kairo::editor::DefaultNavigationSettingsPath();
        kairo::editor::NavigationSettings navigationSettings;
        try { navigationSettings = kairo::editor::LoadNavigationSettings(navigationSettingsPath); }
        catch (const std::exception& error)
        {
            std::cerr << "KairoEditor navigation settings warning: " << error.what()
                << " Using defaults.\n";
        }
        AIHost ai = CreateAIHost();
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
        kairo::runtime::renderbridge::OfflineSceneAssetResolver offlineAssets;
        const auto offlineAlbedo = [&offlineTextureHandles](
            const kairo::renderer::PBRMaterial& material)
            -> std::optional<kairo::foundation::raytracer::Texture2D>
        {
            if (material.BaseColorTexture == kairo::renderer::InvalidTextureHandle)
                return std::nullopt;
            if (const auto found = offlineTextureHandles.find(material.BaseColorTexture);
                found != offlineTextureHandles.end()) return found->second;
            throw std::out_of_range("Offline renderer cannot resolve base-color texture handle " +
                std::to_string(material.BaseColorTexture));
        };
        offlineAssets.ResolveMeshRenderer = [&offlineMeshes, &offlineMaterials, offlineAlbedo](
            const kairo::engine::MeshRendererComponent& component,
            const kairo::foundation::math::Transformf& world)
        {
            const auto mesh = offlineMeshes.find(component.MeshAsset.ID);
            if (mesh == offlineMeshes.end())
                throw std::out_of_range("Offline renderer cannot resolve mesh asset " +
                    component.MeshAsset.ID.ToString());
            kairo::renderer::PBRMaterial material;
            const auto materialAsset = component.MaterialForSlot(0u);
            if (const auto found = offlineMaterials.find(materialAsset.ID);
                found != offlineMaterials.end()) material = found->second;
            kairo::runtime::renderbridge::OfflineResolvedGeometry result;
            result.Submeshes.push_back({ MakeOfflineMaterial(material),
                MakeOfflineMesh(mesh->second, kairo::foundation::math::ToMatrix4(world)),
                offlineAlbedo(material) });
            return result;
        };
        offlineAssets.ResolveSceneInstance = [&offlineScenes, offlineAlbedo](
            const kairo::engine::SceneInstanceComponent& component,
            const kairo::foundation::math::Transformf& world)
        {
            const auto scene = offlineScenes.find(component.SceneAsset.ID);
            if (scene == offlineScenes.end())
                throw std::out_of_range("Offline renderer cannot resolve scene asset " +
                    component.SceneAsset.ID.ToString());
            kairo::runtime::renderbridge::OfflineResolvedGeometry result;
            const auto instanceWorld = kairo::foundation::math::ToMatrix4(world);
            result.Submeshes.reserve(scene->second.Primitives.size());
            for (const auto& primitive : scene->second.Primitives)
                result.Submeshes.push_back({ MakeOfflineMaterial(primitive.Material),
                    MakeOfflineMesh(primitive.Geometry, instanceWorld * primitive.LocalToAsset),
                    offlineAlbedo(primitive.Material) });
            return result;
        };
        offlineAssets.ResolveTexture = [&offlineTextures](kairo::assets::TextureAssetHandle texture)
        {
            if (const auto found = offlineTextures.find(texture.ID);
                found != offlineTextures.end()) return found->second;
            throw std::out_of_range("Offline renderer cannot resolve texture asset " +
                texture.ID.ToString());
        };
        auto offlineRender = std::make_shared<kairo::runtime::renderbridge::EditorOfflineRenderService>(
            [&project]() -> const kairo::engine::Scene& { return project.Scene(); },
            std::move(offlineAssets));
#else
        std::shared_ptr<kairo::editor::OfflineRenderService> offlineRender;
#endif
        auto& nativeGameplayRegistry = kairo::editor::EditorNativeGameplayRegistry();
        kairo::editor::EditorShell shell(state, project, layoutPlan.ShouldRebuild(),
            std::move(keymap), keymapSettings, navigationSettings, navigationSettingsPath,
            std::move(ai.Provider), std::move(ai.Model), std::move(offlineRender),
            &nativeGameplayRegistry, &meshImports);
        if (recovered.has_value()) shell.RestoreRecoveryDrafts(*recovered);
        if (options.ViewportShading.has_value()) shell.SetViewportShading(*options.ViewportShading);
        if (options.AuthoringSurface.has_value()) state.SetAuthoringSurface(*options.AuthoringSurface);

        std::uint64_t renderedFrames = 0u;
        std::optional<kairo::renderer::ViewportCapture> screenshot;
        std::optional<std::filesystem::path> projectTransition;
        while (!renderer.NativeWindow().ShouldClose() && (!options.FrameLimit.has_value() || renderedFrames < *options.FrameLimit))
        {
            renderer.NativeWindow().PollEvents();
            if (auto capture = renderer.TakeViewportCapture(); capture.has_value())
                screenshot = std::move(capture);
            if (const auto picked = renderer.TakeViewportPickResult(); picked.has_value())
                shell.ApplyViewportPick(*picked);
            imgui.BeginFrame();
            shell.SetViewportTexture(imgui.ViewportTexture());
            shell.Draw();
            renderer.NativeWindow().SetCursorCaptured(shell.ViewportCursorCaptured());
            imgui.EndFrame();
            bool assetReloadRequested = false;
            for (const auto& request : shell.TakeAssetBrowserRequests())
            {
                if (request.Kind != kairo::editor::AssetBrowserRequestKind::Reimport) continue;
                const auto metadata = project.Assets().At(request.Asset);
                if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile)
                    throw std::logic_error("Only source-file assets can be reimported.");
                projectTransition = project.ProjectFile();
                assetReloadRequested = true;
                break;
            }
            if (assetReloadRequested) break;
            if (auto transition = shell.TakeProjectTransitionRequest(); transition.has_value())
            {
                projectTransition = std::move(transition);
                break;
            }
            const auto camera = shell.ViewportCamera();
            renderer.SetCameraPose({ camera.Position, camera.Target, camera.Up });
            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(
                shell.RenderScene(), renderAssets, shell.ViewportRenderLayers()));
            renderer.SubmitDebugDraw(shell.PhysicsDebugDraw());
            renderer.SetViewportShadingMode(shell.ViewportShading());
            if (options.Screenshot.has_value() && renderedFrames == 1u)
                renderer.RequestViewportCapture();
            if (const auto pick = shell.TakeViewportPickRequest(); pick.has_value())
                renderer.RequestViewportPick(pick->first, pick->second);
            renderer.DrawFrame();
            if (auto capture = renderer.TakeViewportCapture(); capture.has_value())
                screenshot = std::move(capture);
            const auto [viewportWidth, viewportHeight] = shell.RequestedViewportExtent();
            renderer.ResizeViewport(viewportWidth, viewportHeight);
            ++renderedFrames;
        }
        if (options.Screenshot.has_value())
        {
            if (!screenshot.has_value())
                throw std::runtime_error("Viewport screenshot requires at least three rendered frames.");
            WriteCapture(*options.Screenshot, *screenshot);
        }
        if (projectTransition.has_value())
        {
#if defined(_WIN32)
            throw std::runtime_error("Project switching requires restarting KairoEditorApp on this platform build.");
#else
            const std::string executable = std::filesystem::absolute(argv[0]).string();
            const std::string projectPath = projectTransition->string();
            execl(executable.c_str(), executable.c_str(), "--project", projectPath.c_str(),
                static_cast<char*>(nullptr));
            throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#endif
        }
        return 0;
    }
    catch (const kairo::renderer::PresentationUnavailableError& error)
    {
        std::cerr << "KairoEditor skipped: " << error.what() << '\n';
        return 77;
    }
    catch (const std::exception& error)
    {
        std::cerr << "KairoEditor error: " << error.what() << '\n';
        return 1;
    }
}
