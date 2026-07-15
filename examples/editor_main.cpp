#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>

import Kairo.Editor;
import Kairo.Editor.Theme;
import Kairo.Editor.ImGuiRuntime;
import Kairo.Editor.ImGuiShell;
import Kairo.Editor.SceneRenderBridge;
import Kairo.EngineCore;
import Kairo.Renderer;

namespace
{
    [[nodiscard]] std::optional<std::uint64_t> ParseFrameLimit(int argc, char** argv)
    {
        if (argc == 1) return std::nullopt;
        if (argc != 3 || std::string_view(argv[1]) != "--frames")
            throw std::invalid_argument("Usage: KairoEditorApp [--frames positive-count]");

        std::uint64_t value = 0u;
        const std::string_view text(argv[2]);
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size() || value == 0u)
            throw std::invalid_argument("--frames requires a positive integer.");
        return value;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto frameLimit = ParseFrameLimit(argc, argv);
        kairo::renderer::RendererRuntime renderer({ "KairoEditor", 1600u, 1000u, true });
        kairo::engine::Scene scene;
        const auto cube = scene.CreateEntity("Cube");
        scene.SetMeshRenderer(cube, { "builtin:cube", "builtin:default", true });
        scene.Transform(cube).Local.Scale = { 0.55f, 0.55f, 0.55f };
        kairo::editor::EditorState state(scene);
        state.Select(cube);
        const auto cubeMesh = renderer.CreateMesh(kairo::renderer::Mesh::MakeCube());
        kairo::editor::RenderAssetBindings renderAssets;
        renderAssets.BindMesh("builtin:cube", cubeMesh);
        kairo::editor::ImGuiRuntime imgui(renderer);
        kairo::editor::ApplyKairoEditorTheme();
        kairo::editor::EditorShell shell(state, scene);

        std::uint64_t renderedFrames = 0u;
        while (!renderer.NativeWindow().ShouldClose() && (!frameLimit.has_value() || renderedFrames < *frameLimit))
        {
            renderer.NativeWindow().PollEvents();
            imgui.BeginFrame();
            shell.Draw();
            imgui.EndFrame();
            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(scene, renderAssets));
            renderer.DrawFrame();
            ++renderedFrames;
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "KairoEditor error: " << error.what() << '\n';
        return 1;
    }
}
