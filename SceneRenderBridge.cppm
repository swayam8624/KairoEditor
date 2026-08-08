module;

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module Kairo.Editor.SceneRenderBridge;

import Kairo.EngineCore;
import Kairo.Assets;
import Kairo.Foundation.Math;
import Kairo.Renderer;

export namespace kairo::editor
{
    /// Complete CPU-side result of preparing one source mesh for rendering.
    /// The cache identity and hit flag are retained for editor diagnostics;
    /// Geometry remains renderer-neutral until RendererRuntime uploads it.
    struct RenderMeshImport final
    {
        kairo::renderer::Mesh Geometry;
        kairo::assets::DerivedDataKey CacheKey;
        bool CacheHit = false;
    };

    /// Input: project root, registered source mesh, mutable import provenance,
    /// and the project's content-addressed derived-data cache.
    /// Output: validated renderer geometry plus reproducibility diagnostics.
    /// Task: execute the existing KairoAssets OBJ transaction and adapt its
    /// portable mesh artifact at the KairoRenderer boundary. No source parser,
    /// cache format, or GPU resource lifetime is duplicated in the editor.
    /// Degeneracy: builtin/generated assets and unsupported importer identities
    /// fail explicitly; malformed OBJ diagnostics preserve source line/column.
    [[nodiscard]] inline RenderMeshImport ImportRenderMesh(
        const std::filesystem::path& projectRoot,
        kairo::assets::MeshAssetHandle asset,
        const kairo::assets::AssetRegistry& registry,
        kairo::assets::ImportDatabase& imports,
        const kairo::assets::DerivedDataCache& cache)
    {
        const kairo::assets::AssetMetadata metadata = registry.Resolve(asset);
        if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile)
            throw std::invalid_argument("Render mesh import requires a source-file asset.");
        kairo::assets::OBJMeshImporter importer;
        if (metadata.Importer != importer.Identifier())
            throw std::invalid_argument("Unsupported render mesh importer: " + metadata.Importer);

        kairo::assets::ImportRecord record{
            metadata.ID,
            metadata.Path,
            importer.Identifier(),
            importer.Version(),
            {},
            {},
            1u
        };
        auto outcome = kairo::assets::ImportSourceAsset(
            projectRoot, std::move(record), importer, registry, imports, cache);
        auto mesh = kairo::assets::ParseMeshDerivedArtifact(outcome.Artifact);
        return { kairo::renderer::Mesh::FromArtifact(mesh), outcome.Key, outcome.CacheHit };
    }

    /// Imports one hierarchy-preserving glTF/GLB scene through KairoAssets and
    /// converts it using KairoRenderer's shared Editor/Player adapter.
    [[nodiscard]] inline kairo::renderer::GltfRenderAsset ImportRenderGltfScene(
        const std::filesystem::path& projectRoot,
        kairo::assets::SceneAssetHandle asset,
        const kairo::assets::AssetRegistry& registry,
        kairo::assets::ImportDatabase& imports,
        const kairo::assets::DerivedDataCache& cache,
        const kairo::renderer::GltfTextureResolver& resolveTexture = {})
    {
        const auto metadata = registry.Resolve(asset);
        kairo::assets::GltfSceneImporter importer;
        if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile ||
            metadata.Importer != importer.Identifier())
            throw std::invalid_argument(
                "Render scene import requires a source glTF asset using kairo.gltf.scene.");
        kairo::assets::ImportRecord record{ metadata.ID, metadata.Path,
            importer.Identifier(), importer.Version(), {}, {}, 1u };
        auto outcome = kairo::assets::ImportSourceAsset(projectRoot, std::move(record),
            importer, registry, imports, cache);
        return kairo::renderer::MakeGltfRenderAsset(
            kairo::assets::ParseGltfSceneDerivedArtifact(outcome.Artifact),
            resolveTexture);
    }

    /// Imports one source texture through the same content-addressed pipeline
    /// as meshes. Callers choose linear/sRGB and normal-map semantics from the
    /// material slot before upload; those settings participate in the cache key.
    [[nodiscard]] inline kairo::assets::TextureArtifactData ImportRenderTexture(
        const std::filesystem::path& projectRoot,
        kairo::assets::TextureAssetHandle asset,
        const kairo::assets::TextureImportSettings& settings,
        const kairo::assets::AssetRegistry& registry,
        kairo::assets::ImportDatabase& imports,
        const kairo::assets::DerivedDataCache& cache)
    {
        const auto metadata = registry.Resolve(asset);
        kairo::assets::StbTextureImporter importer;
        if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile ||
            metadata.Importer != importer.Identifier())
            throw std::invalid_argument("Render texture import requires a source texture using kairo.texture.stb.");
        kairo::assets::ImportRecord record{ metadata.ID, metadata.Path,
            importer.Identifier(), importer.Version(),
            kairo::assets::CanonicalTextureImportSettings(settings), {}, 1u };
        auto outcome = kairo::assets::ImportSourceAsset(projectRoot, std::move(record),
            importer, registry, imports, cache);
        return kairo::assets::ParseTextureDerivedArtifact(outcome.Artifact);
    }

    /// Loads a generated material's versioned derived-artifact envelope.
    /// Source materials are not parsed ad hoc: tools must author the canonical
    /// KairoAssets binary format recorded by their registry metadata.
    [[nodiscard]] inline kairo::assets::MaterialArtifactData LoadRenderMaterial(
        const std::filesystem::path& projectRoot,
        kairo::assets::MaterialAssetHandle asset,
        const kairo::assets::AssetRegistry& registry)
    {
        const auto metadata = registry.Resolve(asset);
        if (metadata.Type != kairo::assets::AssetType::Material ||
            metadata.Origin == kairo::assets::AssetOrigin::Builtin)
            throw std::invalid_argument("Render material loading requires a non-builtin material asset.");
        const auto path = projectRoot / metadata.Path;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) throw std::runtime_error("Cannot open material artifact: " + path.string());
        const auto length = input.tellg();
        if (length < 0) throw std::runtime_error("Cannot measure material artifact: " + path.string());
        std::vector<std::byte> bytes(static_cast<std::size_t>(length));
        input.seekg(0);
        if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), length);
        if (!input) throw std::runtime_error("Cannot read material artifact: " + path.string());
        return kairo::assets::ParseMaterialDerivedArtifact(
            kairo::assets::ParseDerivedArtifact(bytes));
    }

    /// Input: one registered builtin mesh metadata record.
    /// Output: procedural renderer geometry when Kairo owns that identifier,
    /// otherwise std::nullopt so callers can continue normal asset handling.
    /// Task: bind persistent primitive asset IDs to one renderer mesh factory
    /// rather than storing duplicate OBJ source files or GPU handles in scenes.
    [[nodiscard]] inline std::optional<kairo::renderer::Mesh> MakeBuiltinRenderMesh(
        const kairo::assets::AssetMetadata& metadata)
    {
        if (metadata.Type != kairo::assets::AssetType::Mesh ||
            metadata.Origin != kairo::assets::AssetOrigin::Builtin)
            return std::nullopt;
        if (metadata.Importer == "kairo.builtin.cube") return kairo::renderer::Mesh::MakeCube();
        if (metadata.Importer == "kairo.builtin.plane") return kairo::renderer::Mesh::MakePlane();
        if (metadata.Importer == "kairo.builtin.uv-sphere") return kairo::renderer::Mesh::MakeUVSphere();
        if (metadata.Importer == "kairo.builtin.cylinder") return kairo::renderer::Mesh::MakeCylinder();
        return std::nullopt;
    }

    /// Maps registered persistent mesh assets to renderer-owned GPU handles.
    ///
    /// Input: a live project registry and valid handles created by
    /// RendererRuntime::CreateMesh.
    /// Output: deterministic lookup for scene render extraction.
    /// Task: keep GPU ownership in KairoRenderer while preventing EngineCore
    /// scene components from depending on Vulkan or process-local handles.
    class RenderAssetBindings final
    {
    public:
        struct ScenePrimitive final
        {
            kairo::renderer::MeshHandle Mesh = kairo::renderer::InvalidMeshHandle;
            kairo::renderer::PBRMaterial Material;
            kairo::foundation::math::Mat4f LocalToAsset =
                kairo::foundation::math::Mat4f::Identity();
        };

        explicit RenderAssetBindings(const kairo::assets::AssetRegistry& registry) noexcept
            : m_Registry(registry) {}

        void BindMesh(kairo::assets::MeshAssetHandle asset, kairo::renderer::MeshHandle handle)
        {
            (void)m_Registry.Resolve(asset);
            if (handle == kairo::renderer::InvalidMeshHandle) throw std::invalid_argument("A render mesh binding requires a valid handle.");
            if (!m_Meshes.emplace(asset.ID, handle).second)
                throw std::invalid_argument("A render mesh asset is already bound.");
        }

        [[nodiscard]] kairo::renderer::MeshHandle ResolveMesh(kairo::assets::MeshAssetHandle asset) const
        {
            (void)m_Registry.Resolve(asset);
            const auto found = m_Meshes.find(asset.ID);
            if (found == m_Meshes.end())
                throw std::out_of_range("No renderer mesh is bound for asset ID: " + asset.ID.ToString());
            return found->second;
        }

        /// Input: a persistent material asset and its fully resolved renderer
        /// descriptor. Output: deterministic material lookup during extraction.
        void BindMaterial(kairo::assets::MaterialAssetHandle asset,
            kairo::renderer::PBRMaterial material)
        {
            (void)m_Registry.Resolve(asset);
            material.Validate();
            if (!m_Materials.emplace(asset.ID, material).second)
                throw std::invalid_argument("A render material asset is already bound.");
        }

        void BindTexture(kairo::assets::TextureAssetHandle asset,
            kairo::renderer::TextureHandle handle)
        {
            (void)m_Registry.Resolve(asset);
            if (handle == kairo::renderer::InvalidTextureHandle)
                throw std::invalid_argument("A render texture binding requires a valid handle.");
            if (!m_Textures.emplace(asset.ID, handle).second)
                throw std::invalid_argument("A render texture asset is already bound.");
        }

        [[nodiscard]] kairo::renderer::TextureHandle ResolveTexture(
            kairo::assets::TextureAssetHandle asset) const
        {
            (void)m_Registry.Resolve(asset);
            const auto found = m_Textures.find(asset.ID);
            if (found == m_Textures.end())
                throw std::out_of_range("No renderer texture is bound for asset ID: " + asset.ID.ToString());
            return found->second;
        }

        [[nodiscard]] kairo::renderer::PBRMaterial ResolveMaterial(
            kairo::assets::MaterialAssetHandle asset) const
        {
            const auto metadata = m_Registry.Resolve(asset);
            const auto found = m_Materials.find(asset.ID);
            if (found != m_Materials.end()) return found->second;
            if (metadata.Origin == kairo::assets::AssetOrigin::Builtin)
                return {};
            throw std::out_of_range(
                "No renderer material is bound for asset ID: " + asset.ID.ToString());
        }

        void BindScene(kairo::assets::SceneAssetHandle asset,
            std::vector<ScenePrimitive> primitives)
        {
            (void)m_Registry.Resolve(asset);
            if (primitives.empty())
                throw std::invalid_argument("A render scene binding requires primitives.");
            for (const auto& primitive : primitives)
            {
                if (primitive.Mesh == kairo::renderer::InvalidMeshHandle)
                    throw std::invalid_argument(
                        "A render scene primitive requires a valid mesh handle.");
                primitive.Material.Validate();
            }
            if (!m_Scenes.emplace(asset.ID, std::move(primitives)).second)
                throw std::invalid_argument("A render scene asset is already bound.");
        }

        [[nodiscard]] const std::vector<ScenePrimitive>& ResolveScene(
            kairo::assets::SceneAssetHandle asset) const
        {
            (void)m_Registry.Resolve(asset);
            const auto found = m_Scenes.find(asset.ID);
            if (found == m_Scenes.end())
                throw std::out_of_range(
                    "No renderer scene is bound for asset ID: " + asset.ID.ToString());
            return found->second;
        }

    private:
        const kairo::assets::AssetRegistry& m_Registry;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::MeshHandle,
            kairo::assets::AssetIDHash> m_Meshes;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::PBRMaterial,
            kairo::assets::AssetIDHash> m_Materials;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::TextureHandle,
            kairo::assets::AssetIDHash> m_Textures;
        std::unordered_map<kairo::assets::AssetID, std::vector<ScenePrimitive>,
            kairo::assets::AssetIDHash> m_Scenes;
    };

    /// Input: one renderer-neutral EngineCore light and its world transform.
    /// Output: the Vulkan forward-pass descriptor using a documented exposure
    /// calibration (10 klux or 100 cd maps to one scene-radiance unit).
    /// Task: centralize coordinate and photometric conversion for the editor.
    [[nodiscard]] inline kairo::renderer::RenderLight MakeRenderLight(
        const kairo::engine::LightComponent& source,
        const kairo::foundation::math::Transformf& world)
    {
        source.Validate();
        kairo::renderer::RenderLight result;
        result.Position = world.Translation;
        result.Color = source.Color;
        result.Range = source.Range;
        result.InnerConeRadians = source.InnerConeRadians;
        result.OuterConeRadians = source.OuterConeRadians;
        result.CastShadows = source.Shadows != kairo::engine::ShadowPolicy::Disabled;
        switch (source.Type)
        {
            case kairo::engine::LightType::Directional:
                result.Type = kairo::renderer::RenderLightType::Directional;
                result.Direction = -world.Forward();
                result.Intensity = source.Intensity / 10'000.0f;
                break;
            case kairo::engine::LightType::Point:
                result.Type = kairo::renderer::RenderLightType::Point;
                result.Direction = world.Forward();
                result.Intensity = source.Intensity / 100.0f;
                break;
            case kairo::engine::LightType::Spot:
                result.Type = kairo::renderer::RenderLightType::Spot;
                result.Direction = world.Forward();
                result.Intensity = source.Intensity / 100.0f;
                break;
            case kairo::engine::LightType::RectangleArea:
                result.Type = kairo::renderer::RenderLightType::RectangleArea;
                result.Direction = world.Forward();
                result.Intensity = source.Intensity / 100.0f;
                result.AreaWidth = source.AreaWidth;
                result.AreaHeight = source.AreaHeight;
                break;
        }
        result.Validate();
        return result;
    }

    /// Converts visible EngineCore mesh components into renderer-local draws.
    ///
    /// Coordinate convention: EngineCore and KairoRenderer share KairoMath's
    /// right-handed TRS representation, so no axis or handedness conversion is
    /// performed. Material slots remain authored scene data until the renderer
    /// material registry lands; the current forward pass uses a neutral tint.
    /// Degeneracy: missing assets and singular transforms fail before the
    /// renderer records GPU commands.
    [[nodiscard]] inline kairo::renderer::RenderScene BuildRenderScene(
        const kairo::engine::Scene& scene,
        const RenderAssetBindings& assets)
    {
        kairo::renderer::RenderScene result;
        for (const kairo::engine::Entity entity : scene.RenderableEntities())
        {
            const auto& meshRenderer = scene.MeshRenderer(entity);
            result.Add({ .Mesh = assets.ResolveMesh(meshRenderer.MeshAsset),
                .Model = kairo::foundation::math::ToMatrix4(scene.WorldTransform(entity)),
                .Material = assets.ResolveMaterial(meshRenderer.MaterialForSlot(0u)),
                .ObjectID = entity.Value,
                .CastShadows = meshRenderer.CastShadows,
                .ReceiveShadows = meshRenderer.ReceiveShadows });
        }
        for (const kairo::engine::Entity entity : scene.SceneInstanceEntities())
        {
            const auto& instance = scene.SceneInstance(entity);
            const auto instanceWorld =
                kairo::foundation::math::ToMatrix4(scene.WorldTransform(entity));
            for (const auto& primitive : assets.ResolveScene(instance.SceneAsset))
                result.Add({ .Mesh = primitive.Mesh,
                    .Model = instanceWorld * primitive.LocalToAsset,
                    .Material = primitive.Material,
                    .ObjectID = entity.Value,
                    .CastShadows = instance.CastShadows,
                    .ReceiveShadows = instance.ReceiveShadows });
        }
        for (const kairo::engine::Entity entity : scene.LightEntities())
            result.AddLight(MakeRenderLight(scene.Light(entity), scene.WorldTransform(entity)));
        if (const auto active = scene.ActiveEnvironment(); active.has_value())
        {
            const auto& source = scene.Environment(*active);
            kairo::renderer::RenderEnvironment environment;
            environment.BackgroundColor = source.BackgroundColor;
            environment.AmbientIntensity = source.AmbientIntensity * source.EnvironmentIntensity;
            environment.ExposureEV100 = source.ExposureEV100;
            result.SetEnvironment(environment);
        }
        return result;
    }
}
