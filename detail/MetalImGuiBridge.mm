#import <Metal/Metal.h>

#include <imgui.h>
#include <imgui_impl_metal.h>

#include "MetalImGuiBridge.hpp"

namespace
{
    id<MTLTexture> g_PipelineFormatTexture = nil;
}

extern "C" bool kairo_imgui_metal_init(void* device)
{
    id<MTLDevice> metalDevice = (__bridge id<MTLDevice>)device;
    MTLTextureDescriptor* texture = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB
        width:1 height:1 mipmapped:NO];
    texture.usage = MTLTextureUsageRenderTarget;
    g_PipelineFormatTexture = [metalDevice newTextureWithDescriptor:texture];
    return g_PipelineFormatTexture != nil && ImGui_ImplMetal_Init(metalDevice);
}

extern "C" void kairo_imgui_metal_new_frame()
{
    MTLRenderPassDescriptor* descriptor =
        [MTLRenderPassDescriptor renderPassDescriptor];
    descriptor.colorAttachments[0].texture = g_PipelineFormatTexture;
    descriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    descriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    ImGui_ImplMetal_NewFrame(descriptor);
}

extern "C" void kairo_imgui_metal_render(
    void* commandBuffer, void* renderEncoder)
{
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(),
        (__bridge id<MTLCommandBuffer>)commandBuffer,
        (__bridge id<MTLRenderCommandEncoder>)renderEncoder);
}

extern "C" void kairo_imgui_metal_shutdown()
{
    ImGui_ImplMetal_Shutdown();
    g_PipelineFormatTexture = nil;
}
