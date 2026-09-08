from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"expected patch anchor missing in {path}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "CMakeLists.txt",
    "set(KAIRO_EDITOR_CORE_REVISION 2a9441749b386ba9afb8e39ad3b5c3b4a265c7ee)\nset(KAIRO_EDITOR_RENDERER_REVISION af289df8f26e7d4fb78e66bb3bee1b1fcf3caab2)\nset(KAIRO_EDITOR_REALTIME_BRIDGE_REVISION 7b131d06078c8ff3c648d53536c046d6f7891f99)",
    "set(KAIRO_EDITOR_CORE_REVISION ecc4a96ef9829d7a7563aeb047a95c0638af0d1c)\nset(KAIRO_EDITOR_RENDERER_REVISION 3758270e792ff00ca8c05429a83e619f03cc4455)\nset(KAIRO_EDITOR_REALTIME_BRIDGE_REVISION ac8591b6884de03aaca395859ce3d796a0413af5)",
)

replace_once(
    "CMakeLists.txt",
    "        KairoUI.cppm EditorTheme.cppm ImGuiRuntime.cppm ImGuiGraphCanvas.cppm TransformGizmo.cppm\n        ImGuiReflectionInspector.cppm EditorShell.cppm)",
    "        KairoUI.cppm EditorTheme.cppm ImGuiRuntime.cppm ImGuiGraphCanvas.cppm TransformGizmo.cppm\n        ImGuiReflectionInspector.cppm AnimationPreview.cppm EditorShell.cppm)",
)

replace_once(
    "examples/editor_main.cpp",
    "import Kairo.Editor.SceneRenderBridge;\nimport Kairo.EngineCore;",
    "import Kairo.Editor.SceneRenderBridge;\nimport Kairo.Editor.AnimationPreview;\nimport Kairo.EngineCore;",
)

replace_once(
    "examples/editor_main.cpp",
    """            const auto imported = kairo::editor::ImportRenderGltfScene(
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
""",
    """            auto imported = kairo::editor::ImportRenderGltfSceneWithSource(
                project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports,
                derivedCache, resolveGltfTexture);
            std::vector<kairo::renderer::MeshHandle> meshHandles;
            meshHandles.reserve(imported.RenderAsset.Primitives.size());
            for (const auto& primitive : imported.RenderAsset.Primitives)
                meshHandles.push_back(renderer.CreateMesh(primitive.Geometry));
            renderAssets.BindGltfScene({ asset.ID }, std::move(imported.Source),
                imported.RenderAsset, meshHandles);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            offlineScenes.emplace(asset.ID, std::move(imported.RenderAsset));
#endif
""",
)

replace_once(
    "examples/editor_main.cpp",
    """        if (options.AuthoringSurface.has_value()) state.SetAuthoringSurface(*options.AuthoringSurface);

        std::uint64_t renderedFrames = 0u;
""",
    """        if (options.AuthoringSurface.has_value()) state.SetAuthoringSurface(*options.AuthoringSurface);
        kairo::editor::AnimationPreviewController animationPreview;

        std::uint64_t renderedFrames = 0u;
""",
)

replace_once(
    "examples/editor_main.cpp",
    """            shell.SetViewportTexture(imgui.ViewportTexture());
            shell.Draw();
            renderer.NativeWindow().SetCursorCaptured(shell.ViewportCursorCaptured());
""",
    """            shell.SetViewportTexture(imgui.ViewportTexture());
            shell.Draw();
            animationPreview.Draw(state.SelectedEntity(), shell.RenderScene(), renderAssets);
            renderer.NativeWindow().SetCursorCaptured(shell.ViewportCursorCaptured());
""",
)

replace_once(
    "examples/editor_main.cpp",
    """            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(
                shell.RenderScene(), renderAssets, shell.ViewportRenderLayers()));
""",
    """            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(
                shell.RenderScene(), renderAssets, animationPreview.Overrides(),
                shell.ViewportRenderLayers()));
""",
)

for transient in (
    ".github/animation-preview-integration-trigger",
    ".github/workflows/apply-animation-preview.yml",
    ".github/workflows/materialize-animation-preview.yml",
    ".github/scripts/materialize_animation_preview.py",
):
    Path(transient).unlink(missing_ok=True)
