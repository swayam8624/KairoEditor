#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
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
    struct AppOptions final
    {
        std::filesystem::path Project;
        std::optional<std::uint64_t> FrameLimit;
    };

    [[nodiscard]] AppOptions ParseOptions(int argc, char** argv)
    {
        AppOptions options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--project")
            {
                if (++index == argc) throw std::invalid_argument("--project requires a .kproject path.");
                options.Project = argv[index];
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
        if (options.Project.empty())
            throw std::invalid_argument("Usage: KairoEditorApp --project <file.kproject> [--frames positive-count]");
        return options;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const AppOptions options = ParseOptions(argc, argv);
        kairo::editor::ProjectSession project;
        project.OpenProject(options.Project);
        kairo::renderer::RendererRuntime renderer({ project.Descriptor().Name, 1600u, 1000u, true });
        kairo::editor::EditorState state(project.Scene());
        if (const auto entities = project.Scene().Entities(); !entities.empty()) state.Select(entities.front());
        kairo::editor::RenderAssetBindings renderAssets(project.Assets());
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Mesh) continue;
            if (asset.Origin != kairo::assets::AssetOrigin::Builtin || asset.Path != "builtin/cube")
                throw std::runtime_error("KairoEditor has no runtime mesh importer for asset: " + asset.Path.generic_string());
            renderAssets.BindMesh({ asset.ID }, renderer.CreateMesh(kairo::renderer::Mesh::MakeCube()));
        }
        kairo::editor::ImGuiRuntime imgui(renderer);
        kairo::editor::ApplyKairoEditorTheme();
        kairo::editor::EditorShell shell(state, project);

        std::uint64_t renderedFrames = 0u;
        while (!renderer.NativeWindow().ShouldClose() && (!options.FrameLimit.has_value() || renderedFrames < *options.FrameLimit))
        {
            renderer.NativeWindow().PollEvents();
            imgui.BeginFrame();
            shell.Draw();
            imgui.EndFrame();
            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(project.Scene(), renderAssets));
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
