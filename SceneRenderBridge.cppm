// TEACHING COMPATIBILITY LAYER: This module re-exports type aliases and
// functions from Kairo.Runtime.RealtimeSceneBridge. The actual conversion
// from EngineCore scene + KairoAssets into renderer-facing RenderScene and
// runtime GPU handles is shared by Editor and Player in KairoGameEngine.
//
// Why a shim? Older extension source can keep its Editor namespace without
// forking mesh/material/light conversion behavior. Do not implement another
// renderer upload cache here or claim this file owns a graphics device.
// For implementation details, follow the imported module's source:
/// https://github.com/swayam8624/KairoGameEngine/blob/main/Runtime/KairoRealtimeRenderBridge/RealtimeSceneBridge.cppm
export module Kairo.Editor.SceneRenderBridge;

import Kairo.Runtime.RealtimeSceneBridge;

/// Backward-compatible Editor namespace for extensions written before the
/// real-time bridge became a shared Editor/Player runtime boundary. All
/// implementation and invariants live in Kairo.Runtime.RealtimeSceneBridge.
export namespace kairo::editor
{
    using RenderMeshImport = kairo::runtime::renderbridge::RenderMeshImport;
    using RenderGltfSceneImport = kairo::runtime::renderbridge::RenderGltfSceneImport;
    using RenderAssetBindings = kairo::runtime::renderbridge::RenderAssetBindings;
    using SceneAnimationPlayback = kairo::runtime::renderbridge::SceneAnimationPlayback;
    using SceneAnimationOverrides = kairo::runtime::renderbridge::SceneAnimationOverrides;
    using kairo::runtime::renderbridge::BuildRenderScene;
    using kairo::runtime::renderbridge::ImportRenderGltfScene;
    using kairo::runtime::renderbridge::ImportRenderGltfSceneWithSource;
    using kairo::runtime::renderbridge::ImportRenderMesh;
    using kairo::runtime::renderbridge::ImportRenderTexture;
    using kairo::runtime::renderbridge::GltfImageSettings;
    using kairo::runtime::renderbridge::DecodeEmbeddedGltfTexture;
    using kairo::runtime::renderbridge::LoadRenderMaterial;
    using kairo::runtime::renderbridge::MakeBuiltinRenderMesh;
    using kairo::runtime::renderbridge::MakeRenderLight;
    using kairo::runtime::renderbridge::SelectRenderCamera;
}
