module;

#include <stdexcept>
#include <string>
#include <unordered_map>

export module Kairo.Editor.SceneRenderBridge;

import Kairo.EngineCore;
import Kairo.Foundation.Math;
import Kairo.Renderer;

export namespace kairo::editor
{
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
                kairo::foundation::math::ToMatrix4(scene.Transform(entity).Local)
            });
        }
        return result;
    }
}
