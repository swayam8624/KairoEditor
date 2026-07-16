#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
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
        std::optional<std::filesystem::path> Document;
        std::optional<kairo::editor::AuthoringSurface> AuthoringSurface;
        std::optional<std::uint64_t> FrameLimit;
        std::optional<std::filesystem::path> Screenshot;
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
            throw std::invalid_argument("Usage: KairoEditorApp --project <file.kproject> "
                "[--document project-relative.kdoc] [--authoring code|graph|split] "
                "[--frames positive-count] [--screenshot output.ppm] [--no-layout-persistence]");
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
        const AppOptions options = ParseOptions(argc, argv);
        kairo::editor::ProjectSession project;
        project.OpenProject(options.Project);
        if (options.Document.has_value()) (void)project.OpenDocument(*options.Document);
        kairo::renderer::RendererRuntime renderer({ project.Descriptor().Name, 1600u, 1000u, true });
        kairo::editor::EditorState state(project.Scene());
        if (const auto entities = project.Scene().Entities(); !entities.empty()) state.Select(entities.front());
        kairo::editor::RenderAssetBindings renderAssets(project.Assets());
        kairo::assets::ImportDatabase meshImports;
        const kairo::assets::DerivedDataCache derivedCache(
            project.ProjectRoot() / ".kairo" / "derived-data");
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Mesh) continue;
            if (asset.Origin == kairo::assets::AssetOrigin::SourceFile)
            {
                auto imported = kairo::editor::ImportRenderMesh(
                    project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports, derivedCache);
                renderAssets.BindMesh({ asset.ID }, renderer.CreateMesh(imported.Geometry));
            }
            else if (const auto builtin = kairo::editor::MakeBuiltinRenderMesh(asset); builtin.has_value())
            {
                renderAssets.BindMesh({ asset.ID }, renderer.CreateMesh(*builtin));
            }
        }
        const std::filesystem::path layoutFile = options.PersistLayout
            ? project.ProjectRoot() / ".kairo" / "editor-layout.ini" : std::filesystem::path{};
        kairo::editor::ImGuiRuntime imgui(renderer, layoutFile);
        kairo::editor::ApplyKairoEditorTheme();
        kairo::editor::EditorShell shell(state, project);
        if (options.AuthoringSurface.has_value()) state.SetAuthoringSurface(*options.AuthoringSurface);

        std::uint64_t renderedFrames = 0u;
        std::optional<kairo::renderer::ViewportCapture> screenshot;
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
            imgui.EndFrame();
            const auto camera = shell.ViewportCamera();
            renderer.SetCameraPose({ camera.Position, camera.Target, camera.Up });
            renderer.SubmitRenderScene(kairo::editor::BuildRenderScene(shell.RenderScene(), renderAssets));
            renderer.SubmitDebugDraw(shell.PhysicsDebugDraw());
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
