export module Kairo.Editor.SceneRenderBridge;

import Kairo.Runtime.RealtimeSceneBridge;

/// Backward-compatible Editor namespace for extensions written before the
/// real-time bridge became a shared Editor/Player runtime boundary. All
/// implementation and invariants live in Kairo.Runtime.RealtimeSceneBridge.
export namespace kairo::editor
{
    using RenderMeshImport = kairo::runtime::renderbridge::RenderMeshImport;
    using RenderAssetBindings = kairo::runtime::renderbridge::RenderAssetBindings;
    using kairo::runtime::renderbridge::BuildRenderScene;
    using kairo::runtime::renderbridge::ImportRenderGltfScene;
    using kairo::runtime::renderbridge::ImportRenderMesh;
    using kairo::runtime::renderbridge::ImportRenderTexture;
    using kairo::runtime::renderbridge::LoadRenderMaterial;
    using kairo::runtime::renderbridge::MakeBuiltinRenderMesh;
    using kairo::runtime::renderbridge::MakeRenderLight;
    using kairo::runtime::renderbridge::SelectRenderCamera;
}
