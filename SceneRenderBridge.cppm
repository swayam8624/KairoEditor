module;

#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

export module Kairo.Editor.SceneRenderBridge;

import Kairo.EngineCore;
import Kairo.Foundation.Math;
import Kairo.Renderer;

export namespace kairo::editor
{
    /// Maps serialized EngineCore mesh asset keys to renderer-owned handles.
    ///
    /// Input: non-empty project asset keys and valid handles created by
    /// RendererRuntime::CreateMesh.
    /// Output: deterministic lookup for scene render extraction.
    /// Task: keep GPU ownership in KairoRenderer while preventing EngineCore
    /// scene components from depending on Vulkan or process-local handles.
    class RenderAssetBindings final
    {
    public:
        void BindMesh(std::string assetKey, kairo::renderer::MeshHandle handle)
        {
            if (assetKey.empty()) throw std::invalid_argument("A render mesh asset key cannot be empty.");
            if (handle == kairo::renderer::InvalidMeshHandle) throw std::invalid_argument("A render mesh binding requires a valid handle.");
            if (!m_Meshes.emplace(std::move(assetKey), handle).second)
                throw std::invalid_argument("A render mesh asset key is already bound.");
        }

        [[nodiscard]] kairo::renderer::MeshHandle ResolveMesh(std::string_view assetKey) const
        {
            const auto found = m_Meshes.find(std::string(assetKey));
            if (found == m_Meshes.end())
                throw std::out_of_range("No renderer mesh is bound for EngineCore asset key: " + std::string(assetKey));
            return found->second;
        }

    private:
        std::unordered_map<std::string, kairo::renderer::MeshHandle> m_Meshes;
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
            result.Add({
                assets.ResolveMesh(meshRenderer.MeshAsset),
                kairo::foundation::math::ToMatrix4(scene.Transform(entity).Local)
            });
        }
        return result;
    }
}
