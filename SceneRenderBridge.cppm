module;

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

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

        /// Task: prove the authored material reference remains registered and
        /// correctly typed even while the M10 renderer uses factor-only PBR.
        void ValidateMaterial(kairo::assets::MaterialAssetHandle asset) const
        {
            (void)m_Registry.Resolve(asset);
        }

    private:
        const kairo::assets::AssetRegistry& m_Registry;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::MeshHandle,
            kairo::assets::AssetIDHash> m_Meshes;
    };

    /// Converts visible EngineCore mesh components into renderer-local draws.
    ///
    /// Coordinate convention: EngineCore and KairoRenderer share KairoMath's
    /// right-handed TRS representation, so no axis or handedness conversion is
    /// performed. MaterialAsset remains authored scene data until the renderer
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
            assets.ValidateMaterial(meshRenderer.MaterialAsset);
            result.Add({
                assets.ResolveMesh(meshRenderer.MeshAsset),
                kairo::foundation::math::ToMatrix4(scene.Transform(entity).Local),
                {},
                entity.Value
            });
        }
        return result;
    }
}
