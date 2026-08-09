#pragma once

extern "C"
{
    bool kairo_imgui_metal_init(void* device);
    void kairo_imgui_metal_new_frame();
    void kairo_imgui_metal_render(void* commandBuffer, void* renderEncoder);
    void kairo_imgui_metal_shutdown();
}
