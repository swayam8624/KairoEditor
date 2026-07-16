module;

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
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
            const std::filesystem::path& layoutFile = {}) : m_Renderer(renderer)
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

                if (!ImGui_ImplGlfw_InitForVulkan(renderer.NativeWindow().NativeHandle(), true))
                    throw std::runtime_error("Dear ImGui GLFW backend initialization failed.");
                m_GlfwInitialized = true;

                const auto context = renderer.BackendContext();
                if (!context.IsValid()) throw std::runtime_error("KairoRenderer provided an invalid Vulkan tooling context.");
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
                if (!ImGui_ImplVulkan_Init(&init)) throw std::runtime_error("Dear ImGui Vulkan backend initialization failed.");
                m_VulkanInitialized = true;
                m_RenderPass = context.RenderPass;
                m_ImageCount = context.SwapchainImageCount;
                renderer.SetOverlayRecorder([](VkCommandBuffer command) {
                    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
                });
                m_OverlayInstalled = true;
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
            RefreshSwapchainContract();
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
        }

        void EndFrame() { ImGui::Render(); }

        /// Output: ImGui texture ID for the renderer's current offscreen scene.
        /// Task: rebuild the backend descriptor exactly when the renderer's
        /// viewport generation changes after a resize.
        [[nodiscard]] ImTextureID ViewportTexture()
        {
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
        std::string m_IniFilename;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        std::uint32_t m_ImageCount = 0u;
        bool m_ContextCreated = false;
        bool m_GlfwInitialized = false;
        bool m_VulkanInitialized = false;
        bool m_OverlayInstalled = false;
        VkDescriptorSet m_ViewportDescriptor = VK_NULL_HANDLE;
        std::uint64_t m_ViewportGeneration = 0u;

        void Shutdown() noexcept
        {
            if (m_OverlayInstalled)
            {
                m_Renderer.SetOverlayRecorder({});
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
