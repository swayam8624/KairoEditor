module;

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#if defined(KAIRO_EDITOR_HAS_OPENGL_UI)
#include <imgui_impl_opengl3.h>
#endif
#if defined(KAIRO_EDITOR_HAS_METAL_UI)
#include "detail/MetalImGuiBridge.hpp"
#endif
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
#include <d3d12.h>
#include <imgui_impl_dx12.h>
#endif
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

export module Kairo.Editor.ImGuiRuntime;

import Kairo.Renderer;

export namespace kairo::editor
{
    /// Owns Dear ImGui's context and GLFW/Vulkan backends while borrowing one
    /// live KairoRenderer runtime. It records UI through the renderer overlay
    /// callback and never begins a render pass or submits GPU work itself.
    class ImGuiRuntime final
    {
    public:
        /// Input: a live renderer and optional project-scoped layout file.
        /// Output: one initialized Dear ImGui context using the renderer's
        /// existing Vulkan contract. An empty layout path disables persistence,
        /// which is useful for deterministic smoke tests and read-only hosts.
        explicit ImGuiRuntime(kairo::renderer::RendererRuntime& renderer,
            const std::filesystem::path& layoutFile = {})
            : m_Renderer(renderer), m_Backend(renderer.Backend())
        {
            try
            {
                if (!layoutFile.empty())
                {
                    std::error_code error;
                    std::filesystem::create_directories(layoutFile.parent_path(), error);
                    if (error) throw std::runtime_error("Cannot create editor layout directory: " + error.message());
                    m_IniFilename = layoutFile.string();
                }
                IMGUI_CHECKVERSION();
                ImGui::CreateContext();
                m_ContextCreated = true;
                ImGuiIO& io = ImGui::GetIO();
                io.IniFilename = m_IniFilename.empty() ? nullptr : m_IniFilename.c_str();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
                io.ConfigWindowsMoveFromTitleBarOnly = true;
#if defined(__APPLE__)
                io.ConfigMacOSXBehaviors = true;
#endif
#if defined(KAIRO_EDITOR_UI_FONT_PATH)
                if (io.Fonts->AddFontFromFileTTF(KAIRO_EDITOR_UI_FONT_PATH, 15.0f) == nullptr)
                    io.Fonts->AddFontDefault();
#else
                io.Fonts->AddFontDefault();
#endif
#if defined(KAIRO_EDITOR_MONO_FONT_PATH)
                (void)io.Fonts->AddFontFromFileTTF(KAIRO_EDITOR_MONO_FONT_PATH, 14.0f);
#endif

                InitializeBackend();
            }
            catch (...)
            {
                Shutdown();
                throw;
            }
        }

        ~ImGuiRuntime() { Shutdown(); }

        ImGuiRuntime(const ImGuiRuntime&) = delete;
        ImGuiRuntime& operator=(const ImGuiRuntime&) = delete;

        void BeginFrame()
        {
            if (m_Backend == kairo::renderer::GraphicsBackend::Vulkan)
            {
                RefreshSwapchainContract();
                ImGui_ImplVulkan_NewFrame();
            }
#if defined(KAIRO_EDITOR_HAS_OPENGL_UI)
            else if (m_Backend == kairo::renderer::GraphicsBackend::OpenGL)
                ImGui_ImplOpenGL3_NewFrame();
#endif
#if defined(KAIRO_EDITOR_HAS_METAL_UI)
            else if (m_Backend == kairo::renderer::GraphicsBackend::Metal)
                kairo_imgui_metal_new_frame();
#endif
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
            else if (m_Backend == kairo::renderer::GraphicsBackend::Direct3D12)
                ImGui_ImplDX12_NewFrame();
#endif
            else throw std::logic_error("Dear ImGui backend was not initialized.");
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void EndFrame() { ImGui::Render(); }

        /// Output: ImGui texture ID for the renderer's current offscreen scene.
        /// Task: rebuild the backend descriptor exactly when the renderer's
        /// viewport generation changes after a resize.
        [[nodiscard]] ImTextureID ViewportTexture()
        {
            if (m_Backend == kairo::renderer::GraphicsBackend::OpenGL)
            {
#if defined(KAIRO_EDITOR_HAS_OPENGL_UI)
                const auto texture = m_Renderer.OpenGLViewportTextureInfo();
                if (!texture.IsValid())
                    throw std::runtime_error(
                        "KairoRenderer provided an invalid OpenGL viewport texture.");
                return static_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(texture.Name));
#else
                throw std::runtime_error(
                    "This Editor build has no OpenGL Dear ImGui adapter.");
#endif
            }
            if (m_Backend == kairo::renderer::GraphicsBackend::Metal)
            {
#if defined(KAIRO_EDITOR_HAS_METAL_UI)
                const auto texture = m_Renderer.MetalViewportTextureInfo();
                if (!texture.IsValid())
                    throw std::runtime_error(
                        "KairoRenderer provided an invalid Metal viewport texture.");
                return static_cast<ImTextureID>(
                    reinterpret_cast<std::uintptr_t>(texture.Texture));
#else
                throw std::runtime_error(
                    "This Editor build has no Metal Dear ImGui adapter.");
#endif
            }
            if (m_Backend == kairo::renderer::GraphicsBackend::Direct3D12)
            {
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
                const auto texture =
                    m_Renderer.Direct3D12ViewportTextureInfo();
                if (!texture.IsValid())
                    throw std::runtime_error(
                        "KairoRenderer provided an invalid Direct3D 12 viewport texture.");
                return static_cast<ImTextureID>(
                    static_cast<std::uintptr_t>(texture.Descriptor.GPU));
#else
                throw std::runtime_error(
                    "This Editor build has no Direct3D 12 Dear ImGui adapter.");
#endif
            }
            const auto texture = m_Renderer.ViewportTexture();
            if (!texture.IsValid()) throw std::runtime_error("KairoRenderer provided an invalid viewport texture.");
            if (texture.Generation == m_ViewportGeneration && m_ViewportDescriptor != VK_NULL_HANDLE)
                return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_ViewportDescriptor));
            if (m_ViewportDescriptor != VK_NULL_HANDLE)
                ImGui_ImplVulkan_RemoveTexture(m_ViewportDescriptor);
            m_ViewportDescriptor = ImGui_ImplVulkan_AddTexture(texture.View, texture.Layout);
            if (m_ViewportDescriptor == VK_NULL_HANDLE)
                throw std::runtime_error("Dear ImGui could not register the Kairo viewport texture.");
            m_ViewportGeneration = texture.Generation;
            return static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(m_ViewportDescriptor));
        }

    private:
        kairo::renderer::RendererRuntime& m_Renderer;
        kairo::renderer::GraphicsBackend m_Backend;
        std::string m_IniFilename;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::uint32_t m_ImageCount = 0u;
        bool m_ContextCreated = false;
        bool m_GlfwInitialized = false;
        bool m_VulkanInitialized = false;
        bool m_OpenGLInitialized = false;
        bool m_MetalInitialized = false;
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
        bool m_Direct3D12Initialized = false;
#endif
        bool m_OverlayInstalled = false;
        VkDescriptorSet m_ViewportDescriptor = VK_NULL_HANDLE;
        std::uint64_t m_ViewportGeneration = 0u;
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
        kairo::renderer::Direct3D12BackendContext m_Direct3D12Context;
#endif

        void InitializeBackend()
        {
            if (m_Backend == kairo::renderer::GraphicsBackend::Vulkan)
            {
                if (!ImGui_ImplGlfw_InitForVulkan(
                    m_Renderer.NativeWindow().NativeHandle(), true))
                    throw std::runtime_error(
                        "Dear ImGui GLFW/Vulkan backend initialization failed.");
                m_GlfwInitialized = true;
                const auto context = m_Renderer.BackendContext();
                if (!context.IsValid())
                    throw std::runtime_error(
                        "KairoRenderer provided an invalid Vulkan tooling context.");
                ImGui_ImplVulkan_InitInfo init{};
                init.ApiVersion = VK_API_VERSION_1_0;
                init.Instance = context.Instance;
                init.PhysicalDevice = context.PhysicalDevice;
                init.Device = context.Device;
                init.QueueFamily = context.GraphicsQueueFamily;
                init.Queue = context.GraphicsQueue;
                init.DescriptorPoolSize = 64u;
                init.MinImageCount = std::max(2u, context.SwapchainImageCount);
                init.ImageCount = context.SwapchainImageCount;
                init.PipelineInfoMain.RenderPass = context.RenderPass;
                init.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                if (!ImGui_ImplVulkan_Init(&init))
                    throw std::runtime_error(
                        "Dear ImGui Vulkan backend initialization failed.");
                m_VulkanInitialized = true;
                m_RenderPass = context.RenderPass;
                m_ImageCount = context.SwapchainImageCount;
                m_Renderer.SetOverlayRecorder([](VkCommandBuffer command)
                {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
                });
                m_OverlayInstalled = true;
                return;
            }
#if defined(KAIRO_EDITOR_HAS_OPENGL_UI)
            if (m_Backend == kairo::renderer::GraphicsBackend::OpenGL)
            {
                if (!ImGui_ImplGlfw_InitForOpenGL(
                    m_Renderer.NativeWindow().NativeHandle(), true))
                    throw std::runtime_error(
                        "Dear ImGui GLFW/OpenGL backend initialization failed.");
                m_GlfwInitialized = true;
                if (!ImGui_ImplOpenGL3_Init("#version 410 core"))
                    throw std::runtime_error(
                        "Dear ImGui OpenGL backend initialization failed.");
                m_OpenGLInitialized = true;
                m_Renderer.SetOpenGLOverlayRecorder([]
                {
                    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
                });
                m_OverlayInstalled = true;
                return;
            }
#endif
#if defined(KAIRO_EDITOR_HAS_METAL_UI)
            if (m_Backend == kairo::renderer::GraphicsBackend::Metal)
            {
                if (!ImGui_ImplGlfw_InitForOther(
                    m_Renderer.NativeWindow().NativeHandle(), true))
                    throw std::runtime_error(
                        "Dear ImGui GLFW/Metal backend initialization failed.");
                m_GlfwInitialized = true;
                const auto context = m_Renderer.MetalContext();
                if (!context.IsValid() ||
                    !kairo_imgui_metal_init(context.Device))
                    throw std::runtime_error(
                        "Dear ImGui Metal backend initialization failed.");
                m_MetalInitialized = true;
                m_Renderer.SetMetalOverlayRecorder([](void* commandBuffer,
                    void* renderEncoder)
                {
                    kairo_imgui_metal_render(commandBuffer, renderEncoder);
                });
                m_OverlayInstalled = true;
                return;
            }
#endif
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
            if (m_Backend == kairo::renderer::GraphicsBackend::Direct3D12)
            {
                if (!ImGui_ImplGlfw_InitForOther(
                    m_Renderer.NativeWindow().NativeHandle(), true))
                    throw std::runtime_error(
                        "Dear ImGui GLFW/Direct3D 12 backend initialization failed.");
                m_GlfwInitialized = true;
                m_Direct3D12Context = m_Renderer.Direct3D12Context();
                if (!m_Direct3D12Context.IsValid())
                    throw std::runtime_error(
                        "KairoRenderer provided an invalid Direct3D 12 tooling context.");
                ImGui_ImplDX12_InitInfo init{};
                init.Device = static_cast<ID3D12Device*>(
                    m_Direct3D12Context.Device);
                init.CommandQueue = static_cast<ID3D12CommandQueue*>(
                    m_Direct3D12Context.CommandQueue);
                init.NumFramesInFlight = static_cast<int>(
                    m_Direct3D12Context.FramesInFlight);
                init.RTVFormat = static_cast<DXGI_FORMAT>(
                    m_Direct3D12Context.RenderTargetFormat);
                init.DSVFormat = static_cast<DXGI_FORMAT>(
                    m_Direct3D12Context.DepthStencilFormat);
                init.SrvDescriptorHeap = static_cast<ID3D12DescriptorHeap*>(
                    m_Direct3D12Context.ShaderResourceHeap);
                init.UserData = &m_Direct3D12Context;
                init.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo* info,
                    D3D12_CPU_DESCRIPTOR_HANDLE* cpu,
                    D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
                {
                    auto& context = *static_cast<
                        kairo::renderer::Direct3D12BackendContext*>(info->UserData);
                    const auto descriptor = context.AllocateDescriptor();
                    cpu->ptr = descriptor.CPU;
                    gpu->ptr = descriptor.GPU;
                };
                init.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo* info,
                    D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                    D3D12_GPU_DESCRIPTOR_HANDLE gpu)
                {
                    auto& context = *static_cast<
                        kairo::renderer::Direct3D12BackendContext*>(info->UserData);
                    context.FreeDescriptor({ cpu.ptr, gpu.ptr });
                };
                if (!ImGui_ImplDX12_Init(&init))
                    throw std::runtime_error(
                        "Dear ImGui Direct3D 12 backend initialization failed.");
                m_Direct3D12Initialized = true;
                m_Renderer.SetDirect3D12OverlayRecorder([](void* commandList)
                {
                    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),
                        static_cast<ID3D12GraphicsCommandList*>(commandList));
                });
                m_OverlayInstalled = true;
                return;
            }
#endif
            throw std::runtime_error(
                "Selected renderer has no Dear ImGui backend in this Editor build.");
        }

        void Shutdown() noexcept
        {
            if (m_OverlayInstalled)
            {
                if (m_Backend == kairo::renderer::GraphicsBackend::Vulkan)
                    m_Renderer.SetOverlayRecorder({});
                else if (m_Backend == kairo::renderer::GraphicsBackend::OpenGL)
                    m_Renderer.SetOpenGLOverlayRecorder({});
                else if (m_Backend == kairo::renderer::GraphicsBackend::Metal)
                    m_Renderer.SetMetalOverlayRecorder({});
                else if (m_Backend ==
                    kairo::renderer::GraphicsBackend::Direct3D12)
                    m_Renderer.SetDirect3D12OverlayRecorder({});
                m_OverlayInstalled = false;
            }
            if (m_VulkanInitialized)
            {
                const auto context = m_Renderer.BackendContext();
                if (context.Device != VK_NULL_HANDLE) vkDeviceWaitIdle(context.Device);
                if (m_ViewportDescriptor != VK_NULL_HANDLE)
                {
                    ImGui_ImplVulkan_RemoveTexture(m_ViewportDescriptor);
                    m_ViewportDescriptor = VK_NULL_HANDLE;
                    m_ViewportGeneration = 0u;
                }
                ImGui_ImplVulkan_Shutdown();
                m_VulkanInitialized = false;
            }
#if defined(KAIRO_EDITOR_HAS_OPENGL_UI)
            if (m_OpenGLInitialized)
            {
                ImGui_ImplOpenGL3_Shutdown();
                m_OpenGLInitialized = false;
            }
#endif
#if defined(KAIRO_EDITOR_HAS_METAL_UI)
            if (m_MetalInitialized)
            {
                kairo_imgui_metal_shutdown();
                m_MetalInitialized = false;
            }
#endif
#if defined(KAIRO_EDITOR_HAS_D3D12_UI)
            if (m_Direct3D12Initialized)
            {
                ImGui_ImplDX12_Shutdown();
                m_Direct3D12Initialized = false;
            }
#endif
            if (m_GlfwInitialized)
            {
                ImGui_ImplGlfw_Shutdown();
                m_GlfwInitialized = false;
            }
            if (m_ContextCreated)
            {
                if (!m_IniFilename.empty()) ImGui::SaveIniSettingsToDisk(m_IniFilename.c_str());
                ImGui::DestroyContext();
                m_ContextCreated = false;
            }
        }

        void RefreshSwapchainContract()
        {
            if (m_Backend != kairo::renderer::GraphicsBackend::Vulkan) return;
            const auto context = m_Renderer.BackendContext();
            if (context.SwapchainImageCount != m_ImageCount)
            {
                ImGui_ImplVulkan_SetMinImageCount(std::max(2u, context.SwapchainImageCount));
                m_ImageCount = context.SwapchainImageCount;
            }
            if (context.RenderPass != m_RenderPass)
            {
                ImGui_ImplVulkan_PipelineInfo pipeline{};
                pipeline.RenderPass = context.RenderPass;
                pipeline.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
                ImGui_ImplVulkan_CreateMainPipeline(&pipeline);
                m_RenderPass = context.RenderPass;
            }
        }
    };
}
