#include "../detail/SelectionBounds.hpp"
#include "../detail/SelectionContour.hpp"
#include "../detail/ViewportInteraction.hpp"
#include "../detail/AssetPreviewPixels.hpp"
#include "../detail/LogicTraceCapture.hpp"
#include "../detail/WindowsProcessArguments.hpp"
#include <algorithm>
#include <imgui.h>
#include <vector>
#include <array>
#include <charconv>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <span>
#include <system_error>
#include <thread>
#include <unordered_map>
#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

import Kairo.Editor;
import Kairo.Editor.ProjectLogicBuild;
import Kairo.AI;
import Kairo.Editor.Theme;
import Kairo.Editor.ImGuiRuntime;
import Kairo.Editor.ImGuiShell;
import Kairo.Editor.SceneRenderBridge;
import Kairo.Editor.AssetImport;
import Kairo.Editor.AnimationPreview;
import Kairo.EngineCore;
import Kairo.Renderer;
import Kairo.Foundation.Math;
import Kairo.Renderer.DebugDraw;
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
import Kairo.Runtime.RenderBridge.EditorOfflineService;
import Kairo.Runtime.RenderBridge.SceneSnapshot;
import Kairo.Foundation.RayTracer;
import Kairo.Foundation.Geometry.Triangle;
#endif
#if defined(KAIRO_EDITOR_HAS_AI_CLOUD)
import Kairo.AI.Cloud;
#endif

namespace
{
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
    [[nodiscard]] kairo::foundation::raytracer::Material MakeOfflineMaterial(
        const kairo::renderer::PBRMaterial& source)
    {
        kairo::foundation::raytracer::Material result;
        result.Name = "EngineCore PBR material";
        result.Type = kairo::foundation::raytracer::MaterialType::PBR;
        result.Albedo = { source.BaseColor.x, source.BaseColor.y, source.BaseColor.z };
        result.Emission = { source.Emissive.x, source.Emissive.y, source.Emissive.z };
        result.Roughness = source.Roughness;
        result.Metallic = source.Metallic;
        return result;
    }

    [[nodiscard]] kairo::foundation::raytracer::TriangleMesh MakeOfflineMesh(
        const kairo::renderer::Mesh& source,
        const kairo::foundation::math::Mat4f& model)
    {
        namespace math = kairo::foundation::math;
        namespace ray = kairo::foundation::raytracer;
        ray::TriangleMesh result;
        result.Triangles.reserve(source.Indices().size() / 3u);
        for (std::size_t offset = 0u; offset < source.Indices().size(); offset += 3u)
        {
            const auto a = source.Indices()[offset];
            const auto b = source.Indices()[offset + 1u];
            const auto c = source.Indices()[offset + 2u];
            const auto& va = source.Vertices().at(a);
            const auto& vb = source.Vertices().at(b);
            const auto& vc = source.Vertices().at(c);
            ray::MeshTriangle triangle;
            triangle.Triangle = kairo::foundation::geometry::Trianglef::FromPoints(
                math::TransformPoint(model, va.Position),
                math::TransformPoint(model, vb.Position),
                math::TransformPoint(model, vc.Position));
            triangle.NormalA = math::TransformNormal(model, va.Normal);
            triangle.NormalB = math::TransformNormal(model, vb.Normal);
            triangle.NormalC = math::TransformNormal(model, vc.Normal);
            triangle.UVA = va.TexCoord;
            triangle.UVB = vb.TexCoord;
            triangle.UVC = vc.TexCoord;
            triangle.HasVertexNormals = true;
            triangle.HasUVs = true;
            result.Triangles.push_back(std::move(triangle));
        }
        return result;
    }

    [[nodiscard]] float DecodeHalf(std::uint16_t bits)
    {
        const float sign = (bits & 0x8000u) != 0u ? -1.0f : 1.0f;
        const std::uint16_t exponent = (bits >> 10u) & 0x1fu;
        const std::uint16_t mantissa = bits & 0x03ffu;
        if (exponent == 0u)
            return sign * std::ldexp(static_cast<float>(mantissa), -24);
        if (exponent == 31u)
            throw std::invalid_argument("Offline texture contains a non-finite binary16 texel.");
        return sign * std::ldexp(1.0f + static_cast<float>(mantissa) / 1024.0f,
            static_cast<int>(exponent) - 15);
    }

    [[nodiscard]] kairo::foundation::raytracer::Texture2D MakeOfflineTexture(
        const kairo::assets::TextureArtifactData& source,
        std::string name)
    {
        kairo::assets::ValidateTextureArtifactData(source);
        const auto& mip = source.Mips.front();
        kairo::foundation::raytracer::Texture2D result;
        result.Name = std::move(name);
        result.Width = mip.Width;
        result.Height = mip.Height;
        result.Pixels.reserve(static_cast<std::size_t>(mip.Width) * mip.Height);
        const auto byte = [&](std::size_t index)
        {
            return std::to_integer<std::uint8_t>(mip.Pixels.at(index));
        };
        const auto linear = [&](float value)
        {
            if (source.ColorSpace != kairo::assets::TextureColorSpace::SRGB) return value;
            return value <= 0.04045f ? value / 12.92f
                : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        const std::size_t count = static_cast<std::size_t>(mip.Width) * mip.Height;
        for (std::size_t pixel = 0u; pixel < count; ++pixel)
        {
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (source.Format == kairo::assets::TexturePixelFormat::R8)
            {
                r = g = b = static_cast<float>(byte(pixel)) / 255.0f;
            }
            else if (source.Format == kairo::assets::TexturePixelFormat::RG8)
            {
                r = static_cast<float>(byte(pixel * 2u)) / 255.0f;
                g = static_cast<float>(byte(pixel * 2u + 1u)) / 255.0f;
            }
            else if (source.Format == kairo::assets::TexturePixelFormat::RGBA8)
            {
                r = static_cast<float>(byte(pixel * 4u)) / 255.0f;
                g = static_cast<float>(byte(pixel * 4u + 1u)) / 255.0f;
                b = static_cast<float>(byte(pixel * 4u + 2u)) / 255.0f;
            }
            else
            {
                const auto half = [&](std::size_t channel)
                {
                    const std::size_t offset = pixel * 8u + channel * 2u;
                    return DecodeHalf(static_cast<std::uint16_t>(byte(offset)) |
                        (static_cast<std::uint16_t>(byte(offset + 1u)) << 8u));
                };
                r = half(0u); g = half(1u); b = half(2u);
            }
            result.Pixels.push_back({ linear(r), linear(g), linear(b) });
        }
        return result;
    }
#endif

    struct AIHost final
    {
        std::shared_ptr<kairo::ai::Provider> Provider;
        std::string Model;
    };

    [[nodiscard]] AIHost CreateAIHost()
    {
#if defined(KAIRO_EDITOR_HAS_AI_CLOUD)
        const char* key = std::getenv("KAIRO_AI_API_KEY");
        const char* model = std::getenv("KAIRO_AI_MODEL");
        if (key == nullptr || *key == '\0' || model == nullptr || *model == '\0') return {};
        kairo::ai::OpenAICompatibleConfig config;
        config.APIKey = key;
        if (const char* endpoint = std::getenv("KAIRO_AI_ENDPOINT");
            endpoint != nullptr && *endpoint != '\0')
            config.Endpoint = endpoint;
        return { std::make_shared<kairo::ai::OpenAICompatibleProvider>(
            std::move(config), std::make_shared<kairo::ai::CprChatTransport>()), model };
#else
        return {};
#endif
    }

    struct AppOptions final
    {
        std::filesystem::path Project;
        std::optional<std::filesystem::path> Document;
        std::optional<std::filesystem::path> RecoverySnapshot;
        std::optional<kairo::editor::AuthoringSurface> AuthoringSurface;
        std::optional<std::uint64_t> FrameLimit;
        std::optional<std::filesystem::path> Screenshot;
        std::optional<kairo::renderer::ViewportShadingMode> ViewportShading;
        std::optional<kairo::renderer::GraphicsBackend> BackendOverride;
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
            if (argument == "--recovery-snapshot")
            {
                if (++index == argc) throw std::invalid_argument(
                    "--recovery-snapshot requires a published snapshot directory.");
                options.RecoverySnapshot = std::filesystem::path(argv[index]);
                options.PersistLayout = false;
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
            if (argument == "--viewport-mode")
            {
                if (++index == argc) throw std::invalid_argument("--viewport-mode requires lit, unlit, normals, or lighting.");
                const std::string_view mode(argv[index]);
                if (mode == "lit") options.ViewportShading = kairo::renderer::ViewportShadingMode::Lit;
                else if (mode == "unlit") options.ViewportShading = kairo::renderer::ViewportShadingMode::Unlit;
                else if (mode == "normals") options.ViewportShading = kairo::renderer::ViewportShadingMode::Normals;
                else if (mode == "lighting") options.ViewportShading = kairo::renderer::ViewportShadingMode::Lighting;
                else throw std::invalid_argument("--viewport-mode requires lit, unlit, normals, or lighting.");
                continue;
            }
            if (argument == "--renderer")
            {
                if (++index == argc)
                    throw std::invalid_argument(
                        "--renderer requires auto, vulkan, metal, d3d12, or opengl.");
                options.BackendOverride =
                    kairo::renderer::ParseGraphicsBackend(argv[index]);
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

    [[nodiscard]] bool IsWithinRoot(const std::filesystem::path& root,
        const std::filesystem::path& candidate) noexcept
    {
        auto rootIt = root.begin();
        auto candidateIt = candidate.begin();
        for (; rootIt != root.end(); ++rootIt, ++candidateIt)
            if (candidateIt == candidate.end() || *candidateIt != *rootIt)
                return false;
        return true;
    }

    [[nodiscard]] std::filesystem::path ResolveRuntimeExecutable(
        const kairo::editor::ProjectSession& project,
        const std::filesystem::path& editorExecutable)
    {
        std::error_code error;
        if (project.Descriptor().PlayExecutable.has_value())
        {
            const auto root = std::filesystem::weakly_canonical(project.ProjectRoot(), error);
            if (error) throw std::runtime_error(
                "Cannot resolve project root for Play: " + error.message());
            const auto candidate = std::filesystem::weakly_canonical(
                root / *project.Descriptor().PlayExecutable, error);
            if (error || !IsWithinRoot(root, candidate))
                throw std::runtime_error(
                    "Project Play executable escapes or cannot be resolved inside the project.");
            if (!std::filesystem::is_regular_file(candidate, error) || error)
                throw std::runtime_error(
                    "Project Play executable is missing. Build the game first: " +
                    candidate.string());
            return candidate;
        }

        if (const char* overridePath = std::getenv("KAIRO_PLAYER_EXECUTABLE");
            overridePath != nullptr && *overridePath != '\0')
        {
            const auto candidate = std::filesystem::weakly_canonical(overridePath, error);
            if (!error && std::filesystem::is_regular_file(candidate, error) && !error)
                return candidate;
            throw std::runtime_error(
                "KAIRO_PLAYER_EXECUTABLE does not name a runnable file.");
        }

        const auto editor = std::filesystem::weakly_canonical(editorExecutable, error);
        if (!error)
        {
            const auto buildRoot = editor.parent_path().parent_path().parent_path();
#if defined(_WIN32)
            const auto candidate = buildRoot / "Runtime" / "KairoPlayer" / "KairoPlayer.exe";
#else
            const auto candidate = buildRoot / "Runtime" / "KairoPlayer" / "KairoPlayer";
#endif
            if (std::filesystem::is_regular_file(candidate, error) && !error)
                return candidate;
        }

        throw std::runtime_error(
            "No project Play executable is configured and KairoPlayer could not be located. "
            "Set play-executable in the .kproject or KAIRO_PLAYER_EXECUTABLE.");
    }

    // This inspector reads a recorded execution, not the live VM. Selecting
    // a row never replays host effects or changes the player's physics state.
    class LogicTracePanel
    {
        bool m_Enabled=false,m_BreakFirst=false,m_UseBreakpoint=false;
        std::optional<kairo::editor::detail::TraceRow> m_Breakpoint;
        std::optional<kairo::assets::AssetID> m_Session;
        std::uint32_t m_LiveStop=0,m_SentStop=0;
        std::string m_LiveState,m_LiveError;
        std::optional<kairo::editor::detail::TraceRow> m_LiveRow;
        kairo::editor::detail::TraceCapture m_Capture;
        std::array<char,128> m_Filter{};
        std::size_t m_Selected=0;
        std::string m_Status;
        std::uintmax_t m_LastSize=0;
        std::chrono::steady_clock::time_point m_LastPoll{};
    public:
        [[nodiscard]] std::optional<kairo::assets::AssetID> NextSession(const std::filesystem::path& root) const
        {
            if(!m_Enabled)return std::nullopt;
            const auto id=kairo::assets::GenerateAssetID();
            if(m_BreakFirst||(m_UseBreakpoint&&m_Breakpoint))
            {
                const auto base=root/".kairo",logs=base/"logs";
                const auto path=logs/("logic-"+id.ToString()+".debug");
                if(std::filesystem::is_symlink(base)||std::filesystem::is_symlink(logs)||
                    std::filesystem::is_symlink(path)||std::filesystem::exists(path))
                    throw std::runtime_error("Debug configuration path rejected.");
                const std::string config=m_BreakFirst?"first\n":
                    "break "+m_Breakpoint->Document+" "+m_Breakpoint->Revision+" "+
                    std::to_string(m_Breakpoint->PC)+"\n";
                kairo::engine::SaveTextFileAtomically(path,config,"logic debug configuration");
            }
            return id;
        }
        void Start(kairo::assets::AssetID id)
        {
            m_Session=id;m_Capture={};m_Selected=0;m_LastSize=0;
            m_Status="Waiting for Player trace.";
            m_LastPoll={};m_LiveStop=m_SentStop=0;m_LiveState.clear();
            m_LiveError.clear();m_LiveRow.reset();
        }
        void Draw(const std::filesystem::path& root,bool supported)
        {
            using namespace kairo::editor::detail;
            if(!ImGui::Begin("Logic Trace")){ImGui::End();return;}
            if(!supported)m_Enabled=false;
            ImGui::BeginDisabled(!supported);
            ImGui::Checkbox("Capture next Play session",&m_Enabled);
            ImGui::BeginDisabled(!m_Enabled);
            if(ImGui::Checkbox("Break on first instruction",&m_BreakFirst)&&m_BreakFirst)m_UseBreakpoint=false;
            if(ImGui::Checkbox("Use selected breakpoint on next Play",&m_UseBreakpoint)&&m_UseBreakpoint)m_BreakFirst=false;
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            if(m_Breakpoint)ImGui::TextWrapped("Breakpoint: %s | PC %u",m_Breakpoint->Document.c_str(),m_Breakpoint->PC);
            else if(m_UseBreakpoint)ImGui::TextDisabled("Select a recorded instruction first.");
            ImGui::TextDisabled("Recorded steps | pre-instruction registers | max 4096 records");
            if(!supported)ImGui::TextDisabled("Custom Play executable: capture unavailable.");
            if(!m_Session){ImGui::TextDisabled("Enable capture, then press Play.");ImGui::End();return;}
            ImGui::TextDisabled("Session %s",m_Session->ToString().c_str());
            const bool refresh=ImGui::Button("Refresh");
            const auto now=std::chrono::steady_clock::now();
            if(refresh||now-m_LastPoll>=std::chrono::milliseconds(250))
            {
                m_LastPoll=now;
                try
                {
                    const auto base=root/".kairo";
                    const auto logs=base/"logs";
                    const auto file=logs/("logic-"+m_Session->ToString()+".ktrace");
                    if(std::filesystem::is_symlink(base)||std::filesystem::is_symlink(logs)||
                        std::filesystem::is_symlink(file))throw std::runtime_error("Linked trace path rejected.");
                    if(std::filesystem::exists(file))
                    {
                        if(!std::filesystem::is_regular_file(file))throw std::runtime_error("Trace is not a regular file.");
                        const auto size=std::filesystem::file_size(file);
                        if(size>TraceMaxBytes)throw std::length_error("Trace exceeds size budget.");
                        if(size&& (refresh||size!=m_LastSize))
                        {
                            std::ifstream in(file,std::ios::binary);
                            std::string bytes(static_cast<std::size_t>(size),'\0');
                            if(!in||!in.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))
                                throw std::runtime_error("Trace read failed.");
                            m_Capture=ParseTrace(bytes,m_Session->ToString());
                            m_LastSize=size;
                            m_Status.clear();
                        }
                    }
                }
                catch(const std::exception& e){m_Status=e.what();}
                try{ReadLive(root);}catch(const std::exception& e){m_LiveError=e.what();}
            }
            DrawLive(root);
            if(!m_Status.empty())ImGui::TextWrapped("%s",m_Status.c_str());
            if(m_Capture.LimitReached)ImGui::TextDisabled("Capture limit reached.");
            if(!m_Capture.Error.empty())ImGui::TextWrapped("Runtime error: %s",m_Capture.Error.c_str());
            ImGui::InputText("Filter",m_Filter.data(),m_Filter.size());
            if(m_Capture.Rows.empty()){ImGui::End();return;}
            m_Selected=std::min(m_Selected,m_Capture.Rows.size()-1);
            ImGui::BeginDisabled(m_Selected==0);
            if(ImGui::Button("Previous recorded step"))--m_Selected;
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_Selected+1>=m_Capture.Rows.size());
            if(ImGui::Button("Next recorded step"))++m_Selected;
            ImGui::EndDisabled();
            const auto& selected=m_Capture.Rows[m_Selected];
            if(ImGui::Button("Set selected instruction as breakpoint"))
            {m_Breakpoint=selected;m_UseBreakpoint=true;m_BreakFirst=false;m_Enabled=true;}
            ImGui::Text("Step %u | Entity %u | %s | PC %u | %s",
                selected.Sequence,selected.Entity,selected.Event.c_str(),selected.PC,selected.Opcode.c_str());
            ImGui::TextWrapped("Document: %s",selected.Document.c_str());
            ImGui::TextWrapped("Source SHA-256: %s",selected.Revision.c_str());
            ImGui::Text("Operands A B C D: %s",selected.Operands.c_str());
            ImGui::TextWrapped("Registers before instruction: %s",selected.Registers.c_str());
            const std::string_view filter=m_Filter.data();
            std::vector<std::size_t> visible;
            for(std::size_t i=0;i<m_Capture.Rows.size();++i)
            {
                const auto& row=m_Capture.Rows[i];
                if(filter.empty()||row.Opcode.find(filter)!=std::string::npos||
                    row.Event.find(filter)!=std::string::npos||row.Document.find(filter)!=std::string::npos||
                    std::to_string(row.Entity).find(filter)!=std::string::npos)
                    visible.push_back(i);
            }
            ImGui::TextDisabled("%zu matching / %zu captured",visible.size(),m_Capture.Rows.size());
            if(ImGui::BeginTable("Trace steps",5,ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg|
                ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_Resizable,ImVec2(0,160)))
            {
                ImGui::TableSetupColumn("Step");ImGui::TableSetupColumn("Entity");
                ImGui::TableSetupColumn("Event");ImGui::TableSetupColumn("PC");ImGui::TableSetupColumn("Opcode");
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;clipper.Begin(static_cast<int>(visible.size()));
                while(clipper.Step())
                    for(int i=clipper.DisplayStart;i<clipper.DisplayEnd;++i)
                    {
                        const auto index=visible[static_cast<std::size_t>(i)];
                        const auto& row=m_Capture.Rows[index];
                        ImGui::PushID(static_cast<int>(index));ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if(ImGui::Selectable(std::to_string(row.Sequence).c_str(),index==m_Selected,
                            ImGuiSelectableFlags_SpanAllColumns))m_Selected=index;
                        ImGui::TableSetColumnIndex(1);ImGui::Text("%u",row.Entity);
                        ImGui::TableSetColumnIndex(2);ImGui::TextUnformatted(row.Event.c_str());
                        ImGui::TableSetColumnIndex(3);ImGui::Text("%u",row.PC);
                        ImGui::TableSetColumnIndex(4);ImGui::TextUnformatted(row.Opcode.c_str());
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
            ImGui::End();
        }
    private:
        std::filesystem::path LivePath(const std::filesystem::path& root,std::string_view suffix) const
        {
            const auto base=root/".kairo",logs=base/"logs";
            const auto path=logs/("logic-"+m_Session->ToString()+std::string(suffix));
            if(std::filesystem::is_symlink(base)||std::filesystem::is_symlink(logs)||std::filesystem::is_symlink(path))
                throw std::runtime_error("Linked debug path rejected.");
            return path;
        }
        void ReadLive(const std::filesystem::path& root)
        {
            using namespace kairo::editor::detail;
            const auto path=LivePath(root,".pause");
            if(!std::filesystem::exists(path))return;
            if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Invalid live debug state.");
            std::ifstream in(path,std::ios::binary);
            std::array<char,2049> buffer{};
            if(!in)throw std::runtime_error("Cannot open live debug state.");
            in.read(buffer.data(),static_cast<std::streamsize>(buffer.size()));
            if(in.bad()||in.gcount()>2048)throw std::runtime_error("Live debug state exceeds budget.");
            std::string_view bytes(buffer.data(),static_cast<std::size_t>(in.gcount()));
            const auto first=bytes.find('\n');
            if(first==std::string_view::npos||bytes.substr(0,first)!="kairo.logic.pause.v1 "+m_Session->ToString())
                throw std::runtime_error("Live debug session mismatch.");
            bytes.remove_prefix(first+1);
            const auto second=bytes.find('\n');
            if(second==std::string_view::npos)throw std::runtime_error("Incomplete live debug state.");
            auto line=bytes.substr(0,second);
            const auto state=TraceToken(line);
            const auto stop=TraceNumber(line);
            bytes.remove_prefix(second+1);
            if(stop==0||stop>4096||stop<m_LiveStop||
                (state!="paused"&&state!="running"&&state!="detached"&&state!="timeout"))
                throw std::runtime_error("Invalid live debug state.");
            std::optional<TraceRow> row;
            if(state=="paused")
            {
                auto capture=ParseTrace("kairo.logic.trace.v1 "+m_Session->ToString()+"\n"+std::string(bytes),
                    m_Session->ToString());
                if(capture.Rows.size()!=1)throw std::runtime_error("Invalid paused instruction.");
                row=std::move(capture.Rows.front());
            }
            else if(!bytes.empty())throw std::runtime_error("Unexpected live debug record.");
            m_LiveStop=stop;m_LiveState=state;m_LiveRow=std::move(row);m_LiveError.clear();
        }
        void SendLive(const std::filesystem::path& root,std::string_view action)
        {
            try
            {
                if(m_LiveState!="paused"||m_SentStop==m_LiveStop)return;
                const auto path=LivePath(root,".cmd-"+std::to_string(m_LiveStop));
                if(std::filesystem::exists(path))throw std::runtime_error("Command already exists for this stop.");
                kairo::engine::SaveTextFileAtomically(path,std::to_string(m_LiveStop)+" "+std::string(action)+"\n","logic debug command");
                m_SentStop=m_LiveStop;
            }
            catch(const std::exception& e){m_LiveError=e.what();}
        }
        void DrawLive(const std::filesystem::path& root)
        {
            if(!m_LiveState.empty())ImGui::Text("Live debugger: %s | Stop %u",m_LiveState.c_str(),m_LiveStop);
            if(!m_LiveError.empty())ImGui::TextWrapped("%s",m_LiveError.c_str());
            if(m_LiveState!="paused"||!m_LiveRow)return;
            const auto& row=*m_LiveRow;
            ImGui::TextWrapped("Paused BEFORE %s at PC %u | Entity %u",row.Opcode.c_str(),row.PC,row.Entity);
            ImGui::TextWrapped("Document: %s | Revision: %s",row.Document.c_str(),row.Revision.c_str());
            ImGui::TextWrapped("Registers: %s",row.Registers.c_str());
            ImGui::TextDisabled("Player thread suspended; auto-detach after 120 seconds.");
            ImGui::BeginDisabled(m_SentStop==m_LiveStop);
            if(ImGui::Button("Continue"))SendLive(root,"continue");
            ImGui::SameLine();
            if(ImGui::Button("Step instruction"))SendLive(root,"step");
            ImGui::SameLine();
            if(ImGui::Button("Detach debugger"))SendLive(root,"detach");
            ImGui::EndDisabled();
        }
    };

    [[nodiscard]] std::intptr_t LaunchProjectRuntime(const kairo::editor::ProjectSession& project,
        const std::filesystem::path& editorExecutable,
        std::optional<kairo::renderer::GraphicsBackend> backendOverride,
        std::optional<kairo::assets::AssetID> traceSession=std::nullopt)
    {
        const auto executable = ResolveRuntimeExecutable(project, editorExecutable);
        const auto descriptor = project.ProjectFile();
        std::string executableText = executable.string();
        std::string descriptorText = descriptor.string();
        // Explicit editor renderer preference applies to the Play child too.
        // Without this, the editor can display with OpenGL while Play silently
        // attempts the project default (possibly unsupported) backend.
        std::string backendText;
        if (backendOverride.has_value() && !project.Descriptor().PlayExecutable.has_value())
        {
            switch (*backendOverride)
            {
                case kairo::renderer::GraphicsBackend::Automatic: backendText = "auto"; break;
                case kairo::renderer::GraphicsBackend::Vulkan: backendText = "vulkan"; break;
                case kairo::renderer::GraphicsBackend::Metal: backendText = "metal"; break;
                case kairo::renderer::GraphicsBackend::Direct3D12: backendText = "d3d12"; break;
                case kairo::renderer::GraphicsBackend::OpenGL: backendText = "opengl"; break;
            }
        }
        // Custom project-owned Play executables have their own argv contracts.
        backendOverride = backendText.empty() ? std::nullopt : backendOverride;
        if(traceSession&&project.Descriptor().PlayExecutable)
            throw std::invalid_argument("Logic tracing requires the built-in KairoPlayer executable.");
        std::string traceText=traceSession?traceSession->ToString():std::string{};

#if defined(_WIN32)
        std::vector<std::string> arguments{executableText,descriptorText};
        if(backendOverride){arguments.emplace_back("--renderer");arguments.push_back(backendText);}
        if(traceSession){arguments.emplace_back("--logic-trace");arguments.push_back(traceText);}
        const auto encoded=kairo::editor::detail::EncodeWindowsArguments(arguments);
        std::vector<const char*> args;
        args.reserve(encoded.size()+1);
        for(const auto& value:encoded)args.push_back(value.c_str());
        args.push_back(nullptr);
        const intptr_t child=_spawnv(_P_NOWAIT,executableText.c_str(),args.data());
        if (child == -1)
            throw std::runtime_error(
                "Cannot launch project runtime: " + executableText);
        std::thread([child]
        {
            (void)_cwait(nullptr, child, _WAIT_CHILD);
        }).detach();
        return 0;
#else
        pid_t child = 0;
        std::vector<char*> childArguments{executableText.data(),descriptorText.data()};
        if(backendOverride){childArguments.push_back(const_cast<char*>("--renderer"));childArguments.push_back(backendText.data());}
        if(traceSession){childArguments.push_back(const_cast<char*>("--logic-trace"));childArguments.push_back(traceText.data());}
        childArguments.push_back(nullptr);
        const auto logPath = project.ProjectRoot() / ".kairo" / "logs" / "player-last-run.log";
        std::filesystem::create_directories(logPath.parent_path());
        const int logFd = ::open(logPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (logFd < 0)
            throw std::system_error(errno, std::generic_category(),
                "Cannot create KairoPlayer log " + logPath.string());

        posix_spawn_file_actions_t actions{};
        int status = posix_spawn_file_actions_init(&actions);
        if (status == 0)
        {
            status = posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
            if (status == 0)
                status = posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);
            if (status == 0)
                status = posix_spawn_file_actions_addclose(&actions, logFd);
            if (status == 0)
                status = posix_spawn(&child, executableText.c_str(), &actions,
                    nullptr, childArguments.data(), environ);
            posix_spawn_file_actions_destroy(&actions);
        }
        ::close(logFd);
        if (status != 0)
            throw std::system_error(status, std::generic_category(),
                "Cannot launch KairoPlayer " + executableText);
        std::cout << "KairoPlayer launched (PID " << child
                  << "); diagnostics: " << logPath << '\n';
        // The editor frame loop polls this PID. Do not detach a waitpid thread
        // that silently throws away a startup failure and leaves no game window.
        return static_cast<std::intptr_t>(child);
#endif
    }
}

int main(int argc, char** argv)
{
    try
    {
        AppOptions options = ParseOptions(argc, argv);
        if (options.Project.empty())
        {
            if (argc != 1)
                throw std::invalid_argument("--project is required when automation options are supplied.");
            const auto selected = kairo::editor::ChooseProjectFile();
            if (!selected.has_value()) return 0;
            options.Project = *selected;
        }
        const auto recentProjectsPath = kairo::editor::DefaultRecentProjectsPath();
        if (!recentProjectsPath.empty())
        {
            try
            {
                auto recent = kairo::editor::RecentProjects::Load(recentProjectsPath);
                recent.Touch(options.Project);
                recent.PruneMissing();
                recent.Save(recentProjectsPath);
            }
            catch (const std::exception& error)
            {
                std::cerr << "KairoEditor recent-projects warning: " << error.what() << '\n';
            }
        }
        std::optional<std::filesystem::path> projectTransition;
        // All project-bound objects, including ImGui descriptors and native
        // GPU resources, must destruct before exec replaces the process.
        {
        kairo::editor::ProjectSession project;
        project.OpenProject(options.Project);
        std::optional<kairo::editor::RecoverySnapshot> recovered;
        if (options.RecoverySnapshot.has_value())
        {
            recovered = kairo::editor::LoadRecoverySnapshot(*options.RecoverySnapshot);
            const auto requested = kairo::editor::CanonicalExistingFile(
                options.Project, "requested project descriptor");
            const auto recorded = std::filesystem::canonical(
                recovered->OriginalProjectRoot / recovered->ProjectFile);
            if (requested != recorded)
                throw std::invalid_argument(
                    "Recovery snapshot belongs to a different project descriptor.");
            project.RestoreRecoveryPoint(recovered->Directory,
                kairo::editor::UnsavedChangesPolicy::Discard);
        }
        if (options.Document.has_value()) (void)project.OpenDocument(*options.Document);
        const kairo::renderer::GraphicsBackend backend =
            options.BackendOverride.value_or(
                kairo::renderer::ParseGraphicsBackend(
                    project.Descriptor().GraphicsBackend));
        kairo::renderer::RendererRuntime renderer({
            project.Descriptor().Name, 1600u, 1000u, true, backend });
        // Establish the exact initial render extent instead of inferring it from dock geometry.
        renderer.ResizeViewport(1600u,1000u);
        kairo::editor::detail::ResizePolicy viewportResize({1600u,1000u});
        kairo::editor::EditorState state(project.Scene());
        if (const auto entities = project.Scene().Entities(); !entities.empty()) state.Select(entities.front());
        kairo::editor::RenderAssetBindings renderAssets(project.Assets());
        using kairo::editor::detail::MeshBounds;
        std::unordered_map<kairo::renderer::MeshHandle,MeshBounds> selectionBounds;
        std::unordered_map<kairo::renderer::MeshHandle,kairo::editor::detail::ContourTopology> selectionContours;
        std::size_t cachedContourFaces=0;
        // Cache geometry bounds at upload, for both native meshes and glTF
        // primitives. Never read vertices back from the GPU per frame.
        const auto uploadMesh = [&](const kairo::renderer::Mesh& mesh)
        {
            const auto handle=renderer.CreateMesh(mesh);
            kairo::editor::detail::BoundsBuilder builder;
            for (const auto& vertex:mesh.Vertices())
                (void)builder.Add({vertex.Position.x,vertex.Position.y,vertex.Position.z});
            if (builder.Result()) selectionBounds.emplace(handle,*builder.Result());
            using namespace kairo::editor::detail;
            if(!mesh.IsSkinned())
                if(auto contour=BuildContourTopology(mesh.Vertices().size(),mesh.Indices(),
                    [&](std::size_t i){
                        const auto& p=mesh.Vertices()[i].Position;
                        return ContourPoint{p.x,p.y,p.z};
                    },MaximumCachedContourFaces-cachedContourFaces))
                {
                    cachedContourFaces+=mesh.Indices().size()/3;
                    selectionContours.emplace(handle,std::move(*contour));
                }
            return handle;
        };
        // Preview data is derived only from already imported, validated CPU
        // assets. Cap memory and work; no additional imports or GPU allocations.
        using kairo::editor::detail::PreviewPixels;
        struct CachedPreview {kairo::assets::AssetID ID;std::uint64_t Revision;PreviewPixels Pixels;};
        std::vector<CachedPreview> previews;
        const auto cacheMeshPreview=[&](const kairo::assets::AssetMetadata& asset,const kairo::renderer::Mesh& mesh)
        {
            if(previews.size()>=128)return;
            const auto pixels=kairo::editor::detail::MeshPreview(mesh.Vertices().size(),mesh.Indices(),
                [&](std::size_t i)
                {
                    const auto& p=mesh.Vertices()[i].Position;
                    return kairo::editor::detail::PreviewPoint{p.x,p.y,p.z};
                });
            if(pixels)previews.push_back({asset.ID,asset.Revision,*pixels});
        };
        const auto cacheTexturePreview=[&](kairo::assets::AssetID id,const kairo::assets::TextureArtifactData& texture)
        {
            if(previews.size()>=128||texture.Mips.empty())return;
            using kairo::assets::TexturePixelFormat;
            const unsigned channels=texture.Format==TexturePixelFormat::R8?1u:
                texture.Format==TexturePixelFormat::RG8?2u:texture.Format==TexturePixelFormat::RGBA8?4u:0u;
            if(!channels)return; // HDR is deliberately not misrepresented as LDR.
            const auto& mip=texture.Mips.front();
            const auto pixels=kairo::editor::detail::TexturePreview(mip.Width,mip.Height,
                [&](unsigned x,unsigned y)
                {
                    const std::size_t offset=(std::size_t(y)*mip.Width+x)*channels;
                    const auto byte=[&](unsigned c){return unsigned(std::to_integer<std::uint8_t>(mip.Pixels.at(offset+c)));};
                    const unsigned red=byte(0);
                    if(channels==1)return std::array<unsigned,4>{red,red,red,255};
                    if(channels==2)return std::array<unsigned,4>{red,byte(1),0,255};
                    return std::array<unsigned,4>{red,byte(1),byte(2),byte(3)};
                });
            if(pixels)previews.push_back({id,project.Assets().At(id).Revision,*pixels});
        };
        kairo::assets::ImportDatabase meshImports;
        const kairo::assets::DerivedDataCache derivedCache(
            project.ProjectRoot() / ".kairo" / "derived-data");
        std::unordered_map<kairo::assets::AssetID, kairo::assets::MaterialArtifactData,
            kairo::assets::AssetIDHash> materialArtifacts;
        std::unordered_map<kairo::assets::AssetID, kairo::assets::TextureImportSettings,
            kairo::assets::AssetIDHash> textureSettings;
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::Mesh,
            kairo::assets::AssetIDHash> offlineMeshes;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::PBRMaterial,
            kairo::assets::AssetIDHash> offlineMaterials;
        std::unordered_map<kairo::assets::AssetID, kairo::renderer::GltfRenderAsset,
            kairo::assets::AssetIDHash> offlineScenes;
        std::unordered_map<kairo::assets::AssetID, kairo::foundation::raytracer::Texture2D,
            kairo::assets::AssetIDHash> offlineTextures;
        std::unordered_map<kairo::renderer::TextureHandle,
            kairo::foundation::raytracer::Texture2D> offlineTextureHandles;
#endif
        const auto requireTexture = [&](const std::optional<kairo::assets::TextureAssetHandle>& texture,
            kairo::assets::TextureColorSpace colorSpace, bool normalMap)
        {
            if (!texture.has_value()) return;
            kairo::assets::TextureImportSettings settings;
            settings.ColorSpace = colorSpace;
            settings.NormalMap = normalMap;
            const auto [found, inserted] = textureSettings.emplace(texture->ID, settings);
            if (!inserted && (found->second.ColorSpace != settings.ColorSpace ||
                found->second.NormalMap != settings.NormalMap))
                throw std::invalid_argument("One texture asset is referenced with incompatible color/data semantics: " +
                    texture->ID.ToString());
        };
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Material ||
                asset.Origin == kairo::assets::AssetOrigin::Builtin) continue;
            auto material = kairo::editor::LoadRenderMaterial(
                project.ProjectRoot(), { asset.ID }, project.Assets());
            requireTexture(material.Textures.BaseColor, kairo::assets::TextureColorSpace::SRGB, false);
            requireTexture(material.Textures.Normal, kairo::assets::TextureColorSpace::Linear, true);
            requireTexture(material.Textures.MetallicRoughness, kairo::assets::TextureColorSpace::Linear, false);
            requireTexture(material.Textures.Emissive, kairo::assets::TextureColorSpace::SRGB, false);
            requireTexture(material.Textures.Occlusion, kairo::assets::TextureColorSpace::Linear, false);
            materialArtifacts.emplace(asset.ID, std::move(material));
        }
        if (const auto environmentEntity = project.Scene().ActiveEnvironment();
            environmentEntity.has_value())
            requireTexture(project.Scene().Environment(*environmentEntity).EnvironmentTexture,
                kairo::assets::TextureColorSpace::Linear, false);
        // Standalone imported images are visible even before a material references them.
        // Cap additional GPU uploads to the preview budget; referenced images are not capped.
        std::size_t standaloneTextureCount=0;
        for(const auto& asset:project.Assets().Snapshot())
            if(asset.Type==kairo::assets::AssetType::Texture2D&&asset.Origin==kairo::assets::AssetOrigin::SourceFile&&
                !textureSettings.contains(asset.ID)&&standaloneTextureCount<128)
            {
                textureSettings.emplace(asset.ID,kairo::assets::TextureImportSettings{});
                ++standaloneTextureCount;
            }
        for (const auto& [id, settings] : textureSettings)
        {
            const auto texture = kairo::editor::ImportRenderTexture(project.ProjectRoot(), { id },
                settings, project.Assets(), meshImports, derivedCache);
            const auto handle = renderer.CreateTexture(texture);
            renderAssets.BindTexture({ id }, handle);
            cacheTexturePreview(id,texture);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            auto offline = MakeOfflineTexture(texture, id.ToString());
            offlineTextureHandles.emplace(handle, offline);
            offlineTextures.emplace(id, std::move(offline));
#endif
        }
        for (const auto& [id, artifact] : materialArtifacts)
        {
            const auto material = kairo::renderer::MakePBRMaterial(artifact,
                [&renderAssets](kairo::assets::TextureAssetHandle texture)
                {
                    return renderAssets.ResolveTexture(texture);
                });
            renderAssets.BindMaterial({ id }, material);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            offlineMaterials.emplace(id, material);
#endif
        }
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Mesh) continue;
            if (asset.Origin == kairo::assets::AssetOrigin::SourceFile)
            {
                auto imported = kairo::editor::ImportRenderMesh(
                    project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports, derivedCache);
                renderAssets.BindMesh({ asset.ID }, uploadMesh(imported.Geometry));
                cacheMeshPreview(asset,imported.Geometry);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                offlineMeshes.emplace(asset.ID, std::move(imported.Geometry));
#endif
            }
            else if (const auto builtin = kairo::editor::MakeBuiltinRenderMesh(asset); builtin.has_value())
            {
                renderAssets.BindMesh({ asset.ID }, uploadMesh(*builtin));
                cacheMeshPreview(asset,*builtin);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                offlineMeshes.emplace(asset.ID, *builtin);
#endif
            }
        }
        std::unordered_map<std::string,kairo::renderer::TextureHandle> gltfTextures;
        for (const auto& asset : project.Assets().Snapshot())
        {
            if (asset.Type != kairo::assets::AssetType::Scene ||
                asset.Origin != kairo::assets::AssetOrigin::SourceFile) continue;
            const auto resolveGltfTexture = [&](std::string_view uri,
                kairo::assets::TextureSemantic semantic)
            {
                const auto path = (asset.Path.parent_path() /
                    kairo::assets::GltfLocalUriPath(uri)).lexically_normal();
                const auto metadata = project.Assets().FindByPath(path);
                if (!metadata.has_value() ||
                    metadata->Type != kairo::assets::AssetType::Texture2D)
                    throw std::invalid_argument(
                        "glTF image URI is not registered as a project texture asset: " +
                        path.generic_string());
                const auto settings=kairo::editor::GltfImageSettings(semantic);
                const auto key=metadata->ID.ToString()+":"+kairo::assets::CanonicalTextureImportSettings(settings);
                if(const auto found=gltfTextures.find(key);found!=gltfTextures.end())return found->second;
                const auto texture = kairo::editor::ImportRenderTexture(
                    project.ProjectRoot(), { metadata->ID }, settings,
                    project.Assets(), meshImports, derivedCache);
                const auto handle = renderer.CreateTexture(texture);
                gltfTextures.emplace(key,handle);
                // Keep the first standalone asset binding available for
                // editor asset inspection. glTF draws use their own variants.
                const bool first=textureSettings.emplace(metadata->ID,settings).second;
                if(first)
                {
                    renderAssets.BindTexture({metadata->ID},handle);
                    cacheTexturePreview(metadata->ID,texture);
                }
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                auto offline = MakeOfflineTexture(texture,key);
                offlineTextureHandles.emplace(handle,offline);
                if(first)offlineTextures.emplace(metadata->ID,std::move(offline));
#endif
                return handle;
            };
            const auto resolveEmbedded=[&](const kairo::assets::GltfTextureBinding& binding,
                kairo::assets::TextureSemantic semantic)
            {
                const auto fingerprint=binding.EmbeddedBytes.empty()?
                    kairo::assets::FingerprintBytes(std::as_bytes(std::span(binding.Uri.data(),binding.Uri.size()))):
                    kairo::assets::FingerprintBytes(binding.EmbeddedBytes);
                const auto key="embedded:"+fingerprint.ToHex()+":"+
                    kairo::assets::CanonicalTextureImportSettings(kairo::editor::GltfImageSettings(semantic));
                if(const auto found=gltfTextures.find(key);found!=gltfTextures.end())return found->second;
                const auto texture=kairo::editor::DecodeEmbeddedGltfTexture(binding,semantic);
                const auto handle=renderer.CreateTexture(texture);
                gltfTextures.emplace(key,handle);
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
                offlineTextureHandles.emplace(handle,MakeOfflineTexture(texture,key));
#endif
                return handle;
            };
            auto imported = kairo::editor::ImportRenderGltfSceneWithSource(
                project.ProjectRoot(), { asset.ID }, project.Assets(), meshImports,
                derivedCache, resolveGltfTexture,resolveEmbedded);
            std::vector<kairo::renderer::MeshHandle> meshHandles;
            meshHandles.reserve(imported.RenderAsset.Primitives.size());
            if(!imported.RenderAsset.Primitives.empty())cacheMeshPreview(asset,imported.RenderAsset.Primitives.front().Geometry);
            for (const auto& primitive : imported.RenderAsset.Primitives)
                meshHandles.push_back(uploadMesh(primitive.Geometry));
            renderAssets.BindGltfScene({ asset.ID }, std::move(imported.Source),
                imported.RenderAsset, meshHandles, imported.CacheKey,std::move(imported.AuthoredRevision),
                std::move(imported.Controller),std::move(imported.ControllerRevision));
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
            offlineScenes.emplace(asset.ID, std::move(imported.RenderAsset));
#endif
        }
        const std::filesystem::path layoutFile = options.PersistLayout
            ? project.ProjectRoot() / ".kairo" / "editor-layout.ini" : std::filesystem::path{};
        const kairo::editor::EditorLayoutPlan layoutPlan = kairo::editor::PrepareEditorLayout(layoutFile);
        kairo::editor::ImGuiRuntime imgui(renderer, layoutFile);
        kairo::editor::ApplyKairoEditorTheme();
        const std::filesystem::path keymapSettings = kairo::editor::DefaultKeymapSettingsPath();
        kairo::editor::EditorKeymapSettings keymap;
        try { keymap = kairo::editor::LoadEditorKeymapSettings(keymapSettings); }
        catch (const std::exception& error)
        {
            std::cerr << "KairoEditor keymap settings warning: " << error.what()
                << " Using the Kairo profile.\n";
        }
        const std::filesystem::path navigationSettingsPath =
            kairo::editor::DefaultNavigationSettingsPath();
        kairo::editor::NavigationSettings navigationSettings;
        try { navigationSettings = kairo::editor::LoadNavigationSettings(navigationSettingsPath); }
        catch (const std::exception& error)
        {
            std::cerr << "KairoEditor navigation settings warning: " << error.what()
                << " Using defaults.\n";
        }
        AIHost ai = CreateAIHost();
#if defined(KAIRO_EDITOR_HAS_OFFLINE_RENDER)
        kairo::runtime::renderbridge::OfflineSceneAssetResolver offlineAssets;
        const auto offlineAlbedo = [&offlineTextureHandles](
            const kairo::renderer::PBRMaterial& material)
            -> std::optional<kairo::foundation::raytracer::Texture2D>
        {
            if (material.BaseColorTexture == kairo::renderer::InvalidTextureHandle)
                return std::nullopt;
            if (const auto found = offlineTextureHandles.find(material.BaseColorTexture);
                found != offlineTextureHandles.end()) return found->second;
            throw std::out_of_range("Offline renderer cannot resolve base-color texture handle " +
                std::to_string(material.BaseColorTexture));
        };
        offlineAssets.ResolveMeshRenderer = [&offlineMeshes, &offlineMaterials, offlineAlbedo](
            const kairo::engine::MeshRendererComponent& component,
            const kairo::foundation::math::Transformf& world)
        {
            const auto mesh = offlineMeshes.find(component.MeshAsset.ID);
            if (mesh == offlineMeshes.end())
                throw std::out_of_range("Offline renderer cannot resolve mesh asset " +
                    component.MeshAsset.ID.ToString());
            kairo::renderer::PBRMaterial material;
            const auto materialAsset = component.MaterialForSlot(0u);
            if (const auto found = offlineMaterials.find(materialAsset.ID);
                found != offlineMaterials.end()) material = found->second;
            kairo::runtime::renderbridge::OfflineResolvedGeometry result;
            result.Submeshes.push_back({ MakeOfflineMaterial(material),
                MakeOfflineMesh(mesh->second, kairo::foundation::math::ToMatrix4(world)),
                offlineAlbedo(material) });
            return result;
        };
        offlineAssets.ResolveSceneInstance = [&offlineScenes, offlineAlbedo](
            const kairo::engine::SceneInstanceComponent& component,
            const kairo::foundation::math::Transformf& world)
        {
            const auto scene = offlineScenes.find(component.SceneAsset.ID);
            if (scene == offlineScenes.end())
                throw std::out_of_range("Offline renderer cannot resolve scene asset " +
                    component.SceneAsset.ID.ToString());
            kairo::runtime::renderbridge::OfflineResolvedGeometry result;
            const auto instanceWorld = kairo::foundation::math::ToMatrix4(world);
            result.Submeshes.reserve(scene->second.Primitives.size());
            for (const auto& primitive : scene->second.Primitives)
                result.Submeshes.push_back({ MakeOfflineMaterial(primitive.Material),
                    MakeOfflineMesh(primitive.Geometry, instanceWorld * primitive.LocalToAsset),
                    offlineAlbedo(primitive.Material) });
            return result;
        };
        offlineAssets.ResolveTexture = [&offlineTextures](kairo::assets::TextureAssetHandle texture)
        {
            if (const auto found = offlineTextures.find(texture.ID);
                found != offlineTextures.end()) return found->second;
            throw std::out_of_range("Offline renderer cannot resolve texture asset " +
                texture.ID.ToString());
        };
        auto offlineRender = std::make_shared<kairo::runtime::renderbridge::EditorOfflineRenderService>(
            [&project]() -> const kairo::engine::Scene& { return project.Scene(); },
            std::move(offlineAssets));
#else
        std::shared_ptr<kairo::editor::OfflineRenderService> offlineRender;
#endif
        auto& nativeGameplayRegistry = kairo::editor::EditorNativeGameplayRegistry();
        kairo::editor::EditorShell shell(state, project, layoutPlan.ShouldRebuild(),
            std::move(keymap), keymapSettings, navigationSettings, navigationSettingsPath,
            std::move(ai.Provider), std::move(ai.Model), std::move(offlineRender),
            &nativeGameplayRegistry, &meshImports);
        for(const auto& preview:previews)shell.SetAssetPreview(preview.ID,preview.Revision,preview.Pixels);
        previews.clear();previews.shrink_to_fit();
        if (recovered.has_value()) shell.RestoreRecoveryDrafts(*recovered);
        if (options.ViewportShading.has_value()) shell.SetViewportShading(*options.ViewportShading);
        if (options.AuthoringSurface.has_value()) state.SetAuthoringSurface(*options.AuthoringSurface);
        kairo::editor::AnimationPreviewController animationPreview;
        LogicTracePanel logicTrace;

        std::uint64_t renderedFrames = 0u;
        std::optional<kairo::renderer::ViewportCapture> screenshot;
#if !defined(_WIN32)
        std::optional<pid_t> launchedRuntime;
        std::chrono::steady_clock::time_point runtimeLaunchedAt{};
#endif
        bool viewportCursorCaptured = false;
        while (!renderer.NativeWindow().ShouldClose() && (!options.FrameLimit.has_value() || renderedFrames < *options.FrameLimit))
        {
            renderer.NativeWindow().PollEvents();
            if (auto capture = renderer.TakeViewportCapture(); capture.has_value())
                screenshot = std::move(capture);
            if (const auto picked = renderer.TakeViewportPickResult(); picked.has_value())
                shell.ApplyViewportPick(*picked);
            imgui.BeginFrame(shell.ViewportKeyboardActive());
            const auto displayed=viewportResize.Current();
            shell.SetViewportTexture(imgui.ViewportTexture(),displayed.W,displayed.H);
            shell.SetRendererProfile(renderer.LastFrameProfile());
            shell.Draw();
#if !defined(_WIN32)
            if (launchedRuntime.has_value())
            {
                int childStatus = 0;
                const pid_t completed = waitpid(*launchedRuntime, &childStatus, WNOHANG);
                if (completed == *launchedRuntime || (completed < 0 && errno != EINTR))
                {
                    const bool quickExit = std::chrono::steady_clock::now() -
                        runtimeLaunchedAt < std::chrono::seconds(2);
                    const bool success = completed > 0 &&
                        WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0;
                    launchedRuntime.reset();
                    if (!success || quickExit)
                    {
                        const auto logPath = project.ProjectRoot() / ".kairo" /
                            "logs" / "player-last-run.log";
                        std::ifstream log(logPath);
                        std::string excerpt;
                        std::string line;
                        while (std::getline(log, line))
                        {
                            excerpt += line + '\n';
                            if (excerpt.size() > 2400u)
                                excerpt.erase(0u, excerpt.size() - 2400u);
                        }
                        std::string detail = success
                            ? "KairoPlayer closed immediately without staying in Play mode."
                            : "KairoPlayer failed to start or exited with an error.";
                        if (completed > 0 && WIFEXITED(childStatus))
                            detail += " Exit status: " +
                                std::to_string(WEXITSTATUS(childStatus)) + ".";
                        detail += "\nLog: " + logPath.string();
                        if (!excerpt.empty()) detail += "\n\n" + excerpt;
                        shell.ReportHostError(std::move(detail));
                    }
                }
            }
#endif
            logicTrace.Draw(project.ProjectRoot(),!project.Descriptor().PlayExecutable);
            if (shell.TakeRuntimeLaunchRequest())
            {
                try
                {
                    // Saving scene and logic documents is not compilation.
                    // KairoPlayer validates source-bound bytecode at startup;
                    // skipping this gate can cause an immediate invisible exit.
                    const auto built = kairo::editor::BuildProjectLogic(project.ProjectFile());
                    std::cout << "Play: compiled " << built.size()
                              << " attached logic artifact(s).\n";
#if !defined(_WIN32)
                    if (launchedRuntime.has_value())
                        throw std::runtime_error("The game is already running. Close it before launching Play again.");
                    const auto traceSession=logicTrace.NextSession(project.ProjectRoot());
                    launchedRuntime = static_cast<pid_t>(LaunchProjectRuntime(
                        project, std::filesystem::absolute(argv[0]), options.BackendOverride,traceSession));
                    if(traceSession)logicTrace.Start(*traceSession);
                    runtimeLaunchedAt = std::chrono::steady_clock::now();
#else
                    const auto traceSession=logicTrace.NextSession(project.ProjectRoot());
                    (void)LaunchProjectRuntime(project, std::filesystem::absolute(argv[0]),
                        options.BackendOverride,traceSession);
                    if(traceSession)logicTrace.Start(*traceSession);
#endif
                }
                catch (const std::exception& error)
                {
                    shell.ReportHostError(error.what());
                }
            }
            animationPreview.Draw(state.SelectedEntity(), shell.RenderScene(), renderAssets,
                project.ProjectRoot(),state.Mode()==kairo::editor::EditorMode::Edit);
            const bool wantsCursorCapture = shell.ViewportCursorCaptured();
            if (wantsCursorCapture != viewportCursorCaptured)
            {
                // GLFW cursor disabling is a state transition, not a per-frame
                // command. Repeating it can reset relative mouse sampling.
                renderer.NativeWindow().SetCursorCaptured(wantsCursorCapture);
                viewportCursorCaptured = wantsCursorCapture;
            }
            imgui.EndFrame();
            if(auto sources=shell.TakeAssetImportRequests();!sources.empty())
            {
                try
                {
                    const auto imported=kairo::editor::ImportAssetBatch(project,sources);
                    std::cout<<"Imported "<<imported.size()<<" primary asset(s).\n";
                    projectTransition=project.ProjectFile();
                    break;
                }
                catch(const std::exception& error){shell.ReportHostError(error.what());}
            }
            bool assetReloadRequested = false;
            for (const auto& request : shell.TakeAssetBrowserRequests())
            {
                if (request.Kind != kairo::editor::AssetBrowserRequestKind::Reimport) continue;
                const auto metadata = project.Assets().At(request.Asset);
                if (metadata.Origin != kairo::assets::AssetOrigin::SourceFile)
                    throw std::logic_error("Only source-file assets can be reimported.");
                projectTransition = project.ProjectFile();
                assetReloadRequested = true;
                break;
            }
            if (assetReloadRequested) break;
            if (auto transition = shell.TakeProjectTransitionRequest(); transition.has_value())
            {
                projectTransition = std::move(transition);
                break;
            }
            const auto camera = shell.ViewportCamera();
            renderer.SetCameraPose(camera);
            auto renderScene=kairo::editor::BuildRenderScene(
                shell.RenderScene(), renderAssets, animationPreview.Overrides(),
                shell.ViewportRenderLayers());
            auto debug=shell.PhysicsDebugDraw();
            if (const auto selected=state.SelectedEntity(); selected.has_value())
            {
                using namespace kairo::foundation::math;
                using namespace kairo::editor::detail;
                const ContourPoint eye{camera.Position.x,camera.Position.y,camera.Position.z};
                std::optional<ContourPoint> towardEye;
                if(camera.Projection==kairo::renderer::CameraProjectionMode::Orthographic)
                {
                    const auto direction=camera.Position-camera.Target;
                    towardEye=ContourPoint{direction.x,direction.y,direction.z};
                }
                std::size_t remainingFaces=MaximumCachedContourFaces;
                std::size_t remainingLines=MaximumContourLines;
                // Real draw IDs and model matrices are the selection authority.
                // Trace only the selected static geometry; oversized, skinned,
                // or budget-exhausted draws use the established bounds fallback.
                for (const auto& draw:renderScene.Draws())
                {
                    if (draw.ObjectID!=selected->Value) continue;
                    const auto contour=selectionContours.find(draw.Mesh);
                    if(contour!=selectionContours.end()&&
                        contour->second.Faces.size()<=remainingFaces&&remainingLines>0)
                    {
                        remainingFaces-=contour->second.Faces.size();
                        const auto& m=draw.Model;
                        const double determinant=double(m(0,0))*(double(m(1,1))*m(2,2)-double(m(1,2))*m(2,1))
                            -double(m(0,1))*(double(m(1,0))*m(2,2)-double(m(1,2))*m(2,0))
                            +double(m(0,2))*(double(m(1,0))*m(2,1)-double(m(1,1))*m(2,0));
                        const auto lines=TraceContour(contour->second,[&](ContourPoint p){
                            const auto q=TransformPoint(draw.Model,Vec3f{p[0],p[1],p[2]});
                            return ContourPoint{q.x,q.y,q.z};
                        },eye,towardEye,remainingLines,determinant<0.0);
                        if(lines&&!lines->empty())
                        {
                            remainingLines-=lines->size();
                            for(const auto& line:*lines)
                                debug.AddLine({line[0][0],line[0][1],line[0][2]},
                                    {line[1][0],line[1][1],line[1][2]},{0.16f,0.48f,1.0f,1.0f});
                            continue;
                        }
                    }
                    if(remainingLines<BoundsEdges.size())continue;
                    remainingLines-=BoundsEdges.size();
                    const auto found=selectionBounds.find(draw.Mesh);
                    if (found==selectionBounds.end()) continue;
                    const auto local=kairo::editor::detail::BoundsCorners(found->second);
                    std::array<Vec3f,8> world{};
                    for (unsigned i=0;i<world.size();++i)
                        world[i]=TransformPoint(draw.Model,Vec3f{local[i][0],local[i][1],local[i][2]});
                    for (const auto& edge:kairo::editor::detail::BoundsEdges)
                        debug.AddLine(world[edge[0]],world[edge[1]],{0.16f,0.48f,1.0f,1.0f});
                }
            }
            renderer.SubmitRenderScene(renderScene);
            renderer.SubmitDebugDraw(debug);
            renderer.SetViewportShadingMode(shell.ViewportShading());
            if (options.Screenshot.has_value() && renderedFrames == 1u)
                renderer.RequestViewportCapture();
            if (const auto pick = shell.TakeViewportPickRequest(); pick.has_value())
                renderer.RequestViewportPick(pick->first, pick->second);
            renderer.DrawFrame();
            if (auto capture = renderer.TakeViewportCapture(); capture.has_value())
                screenshot = std::move(capture);
            const auto [viewportWidth, viewportHeight] = shell.RequestedViewportExtent();
            if(const auto next=viewportResize.Observe({viewportWidth,viewportHeight}))
                renderer.ResizeViewport(next->W,next->H);
            ++renderedFrames;
        }
        if (options.Screenshot.has_value())
        {
            if (!screenshot.has_value())
                throw std::runtime_error("Viewport screenshot requires at least three rendered frames.");
            WriteCapture(*options.Screenshot, *screenshot);
        }
        if(viewportCursorCaptured)renderer.NativeWindow().SetCursorCaptured(false);
        } // project-bound resources released before restart
        if (projectTransition.has_value())
        {
            const std::string executable = std::filesystem::absolute(argv[0]).string();
            const std::string projectPath = projectTransition->string();
#if defined(_WIN32)
            const auto encoded = kairo::editor::detail::EncodeWindowsArguments(
                {executable, "--project", projectPath});
            const char* args[]{encoded[0].c_str(), encoded[1].c_str(), encoded[2].c_str(), nullptr};
            const intptr_t result = _spawnv(_P_OVERLAY, executable.c_str(), args);
            if (result == -1)
                throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#else
            execl(executable.c_str(), executable.c_str(), "--project", projectPath.c_str(),
                static_cast<char*>(nullptr));
            throw std::runtime_error("Cannot restart KairoEditorApp for the selected project.");
#endif
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
