cmake_minimum_required(VERSION 3.28)
project(KairoEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KAIRO_EDITOR_BUILD_TESTS "Build KairoEditor tests" ${PROJECT_IS_TOP_LEVEL})
option(KAIRO_EDITOR_BUILD_APP "Build the Vulkan Dear ImGui editor application" ${PROJECT_IS_TOP_LEVEL})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

if(NOT TARGET KairoEngineCore)
    set(KAIRO_ENGINE_CORE_SOURCE_DIR "" CACHE PATH "Path to a local KairoEngineCore checkout")
    if(KAIRO_ENGINE_CORE_SOURCE_DIR AND EXISTS "${KAIRO_ENGINE_CORE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${KAIRO_ENGINE_CORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoEngineCore")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoEngineCore/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoEngineCore" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoEngineCore")
    else()
        include(FetchContent)
        FetchContent_Declare(KairoEngineCore GIT_REPOSITORY https://github.com/swayam8624/KairoEngineCore.git GIT_TAG main)
        FetchContent_MakeAvailable(KairoEngineCore)
    endif()
endif()

add_library(KairoEditor)
target_sources(KairoEditor PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    EditorTypes.cppm TextValidation.cppm DocumentTypes.cppm DocumentSchema.cppm
    ProjectDescriptor.cppm ProjectSession.cppm CommandHistory.cppm SceneCommands.cppm
    EditorState.cppm PanelState.cppm KairoEditor.cppm)
target_link_libraries(KairoEditor PUBLIC KairoEngineCore)

if(KAIRO_EDITOR_BUILD_APP)
    if(NOT TARGET KairoRenderer)
        set(KAIRO_RENDERER_SOURCE_DIR "" CACHE PATH "Path to a local KairoRenderer checkout")
        if(KAIRO_RENDERER_SOURCE_DIR AND EXISTS "${KAIRO_RENDERER_SOURCE_DIR}/CMakeLists.txt")
            add_subdirectory("${KAIRO_RENDERER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoRenderer")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoRenderer/CMakeLists.txt")
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoRenderer" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoRenderer")
        else()
            include(FetchContent)
            FetchContent_Declare(KairoRenderer GIT_REPOSITORY https://github.com/swayam8624/KairoRenderer.git GIT_TAG main)
            FetchContent_MakeAvailable(KairoRenderer)
        endif()
    endif()

    include(FetchContent)
    FetchContent_Declare(DearImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.92.8-docking
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(DearImGui)

    find_path(KAIRO_EDITOR_VULKAN_INCLUDE_DIR vulkan/vulkan.h
        HINTS /opt/homebrew/include /usr/local/include)
    find_library(KAIRO_EDITOR_VULKAN_LOADER_LIBRARY NAMES vulkan
        HINTS /opt/homebrew/lib /usr/local/lib)
    find_library(KAIRO_EDITOR_GLFW_LIBRARY NAMES glfw glfw3
        HINTS /opt/homebrew/lib /usr/local/lib)
    if(NOT KAIRO_EDITOR_VULKAN_INCLUDE_DIR OR NOT KAIRO_EDITOR_VULKAN_LOADER_LIBRARY OR NOT KAIRO_EDITOR_GLFW_LIBRARY)
        message(FATAL_ERROR "KairoEditorApp requires GLFW, Vulkan headers, and the Vulkan loader.")
    endif()

    add_library(KairoDearImGui STATIC
        ${dearimgui_SOURCE_DIR}/imgui.cpp
        ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
        ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
        ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_include_directories(KairoDearImGui PUBLIC
        ${dearimgui_SOURCE_DIR}
        ${dearimgui_SOURCE_DIR}/backends
        ${KAIRO_EDITOR_VULKAN_INCLUDE_DIR})
    target_link_libraries(KairoDearImGui PUBLIC ${KAIRO_EDITOR_GLFW_LIBRARY} ${KAIRO_EDITOR_VULKAN_LOADER_LIBRARY})

    add_library(KairoEditorImGui)
    target_sources(KairoEditorImGui PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        EditorTheme.cppm ImGuiRuntime.cppm EditorShell.cppm)
    target_link_libraries(KairoEditorImGui PUBLIC KairoEditor KairoRenderer KairoDearImGui)

    add_library(KairoEditorRendererBridge)
    target_sources(KairoEditorRendererBridge PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        SceneRenderBridge.cppm)
    target_link_libraries(KairoEditorRendererBridge PUBLIC KairoEditor KairoRenderer)
    if(APPLE)
        target_compile_definitions(KairoEditorImGui PRIVATE
            KAIRO_EDITOR_UI_FONT_PATH="/System/Library/Fonts/SFNS.ttf"
            KAIRO_EDITOR_MONO_FONT_PATH="/System/Library/Fonts/SFNSMono.ttf")
    endif()

    add_executable(KairoEditorApp examples/editor_main.cpp)
    target_link_libraries(KairoEditorApp PRIVATE KairoEditorImGui KairoEditorRendererBridge)
endif()

if(KAIRO_EDITOR_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(Catch2 GIT_REPOSITORY https://github.com/catchorg/Catch2.git GIT_TAG v3.5.4)
    FetchContent_MakeAvailable(Catch2)
    add_executable(KairoEditorTests tests/EditorTests.cpp)
    target_link_libraries(KairoEditorTests PRIVATE KairoEditor Catch2::Catch2WithMain)
    enable_testing()
    add_test(NAME KairoEditorTests COMMAND KairoEditorTests)
    if(TARGET KairoEditorApp)
        add_executable(KairoEditorRenderBridgeTests tests/SceneRenderBridgeTests.cpp)
        target_link_libraries(KairoEditorRenderBridgeTests PRIVATE KairoEditorRendererBridge Catch2::Catch2WithMain)
        add_test(NAME KairoEditorRenderBridgeTests COMMAND KairoEditorRenderBridgeTests)
        add_test(NAME KairoEditorNativeSmoke COMMAND KairoEditorApp
            --project ${CMAKE_CURRENT_SOURCE_DIR}/examples/StarterProject/Project.kproject
            --frames 3 --no-layout-persistence)
        set_tests_properties(KairoEditorNativeSmoke PROPERTIES TIMEOUT 20)
        add_test(NAME KairoEditorRequiresProject COMMAND KairoEditorApp --frames 1)
        set_tests_properties(KairoEditorRequiresProject PROPERTIES WILL_FAIL TRUE TIMEOUT 20)
    endif()
endif()
