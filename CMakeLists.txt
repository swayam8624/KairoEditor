cmake_minimum_required(VERSION 3.28)
project(KairoEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(KAIRO_EDITOR_BUILD_TESTS "Build KairoEditor tests" ${PROJECT_IS_TOP_LEVEL})
option(KAIRO_EDITOR_BUILD_APP "Build the Vulkan Dear ImGui editor application" ${PROJECT_IS_TOP_LEVEL})
set(KAIRO_EDITOR_UI_FONT_PATH "" CACHE FILEPATH "Optional UI TTF font for the native editor")
set(KAIRO_EDITOR_MONO_FONT_PATH "" CACHE FILEPATH "Optional monospace TTF font for the native editor")

# Immutable standalone fallback revisions. Local source directories and sibling
# checkouts still take precedence for active workspace development.
set(KAIRO_EDITOR_CORE_REVISION d098d4d87fd6105eefde858d57eb1b376edb230e)
set(KAIRO_EDITOR_RENDERER_REVISION dc6a2577d3804492dfa524b40c075f2459e6f188)

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
        FetchContent_Declare(KairoEngineCore
            GIT_REPOSITORY https://github.com/swayam8624/KairoEngineCore.git
            GIT_TAG ${KAIRO_EDITOR_CORE_REVISION}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(KairoEngineCore)
    endif()
endif()

add_library(KairoEditor)
target_sources(KairoEditor PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
    EditorTypes.cppm TextValidation.cppm TextFormat.cppm ProjectPaths.cppm DocumentTypes.cppm DocumentSchema.cppm
    CoreDocumentSchemas.cppm
    AuthoringDocument.cppm DocumentValidation.cppm DocumentSerialization.cppm DocumentCommands.cppm
    DocumentCompiler.cppm ProjectDocuments.cppm GraphCanvas.cppm DocumentProjection.cppm
    AuthoringWorkspaceState.cppm
    ProjectDescriptor.cppm ProjectSession.cppm CommandHistory.cppm EditorActions.cppm EditorInputRouter.cppm EditorLayoutRecovery.cppm ViewportController.cppm PrimitiveTypes.cppm SceneCommands.cppm
    EditorState.cppm PanelState.cppm KairoEditor.cppm)
target_link_libraries(KairoEditor PUBLIC KairoEngineCore)

if(KAIRO_EDITOR_BUILD_APP)
    # Physics preview consumes KairoRenderer's optional debug bridge. Make the
    # standalone editor build request it explicitly; the umbrella build already
    # coordinates the same cache value at the workspace root.
    set(KAIRO_RENDERER_BUILD_PHYSICS_BRIDGE ON CACHE BOOL "" FORCE)
    if(NOT TARGET KairoRenderer)
        set(KAIRO_RENDERER_SOURCE_DIR "" CACHE PATH "Path to a local KairoRenderer checkout")
        if(KAIRO_RENDERER_SOURCE_DIR AND EXISTS "${KAIRO_RENDERER_SOURCE_DIR}/CMakeLists.txt")
            add_subdirectory("${KAIRO_RENDERER_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoRenderer")
        elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../KairoRenderer/CMakeLists.txt")
            add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../KairoRenderer" "${CMAKE_CURRENT_BINARY_DIR}/_deps/KairoRenderer")
        else()
            include(FetchContent)
            FetchContent_Declare(KairoRenderer
                GIT_REPOSITORY https://github.com/swayam8624/KairoRenderer.git
                GIT_TAG ${KAIRO_EDITOR_RENDERER_REVISION}
                GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(KairoRenderer)
        endif()
    endif()

    include(FetchContent)
    FetchContent_Declare(DearImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG 572f249ce1975f98ad9f8aabce512ffa12a52d6c)
    FetchContent_MakeAvailable(DearImGui)

    find_package(glfw3 CONFIG REQUIRED)
    find_package(Vulkan REQUIRED)

    add_library(KairoDearImGui STATIC
        ${dearimgui_SOURCE_DIR}/imgui.cpp
        ${dearimgui_SOURCE_DIR}/imgui_draw.cpp
        ${dearimgui_SOURCE_DIR}/imgui_tables.cpp
        ${dearimgui_SOURCE_DIR}/imgui_widgets.cpp
        ${dearimgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${dearimgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp)
    target_include_directories(KairoDearImGui PUBLIC
        ${dearimgui_SOURCE_DIR}
        ${dearimgui_SOURCE_DIR}/misc/cpp
        ${dearimgui_SOURCE_DIR}/backends)
    target_link_libraries(KairoDearImGui PUBLIC glfw Vulkan::Vulkan)

    FetchContent_Declare(ImGuizmo
        GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
        GIT_TAG fd138a7ce089cfcbfee16da8e8692f37a0e9481e
        SOURCE_SUBDIR KairoNoCMakeProject)
    FetchContent_MakeAvailable(ImGuizmo)
    add_library(KairoImGuizmo STATIC ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp)
    add_library(Kairo::ImGuizmo ALIAS KairoImGuizmo)
    target_include_directories(KairoImGuizmo PUBLIC ${imguizmo_SOURCE_DIR}/src)
    target_link_libraries(KairoImGuizmo PUBLIC KairoDearImGui)

    add_library(KairoEditorImGui)
    target_sources(KairoEditorImGui PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        KairoUI.cppm EditorTheme.cppm ImGuiRuntime.cppm ImGuiGraphCanvas.cppm TransformGizmo.cppm
        ImGuiReflectionInspector.cppm EditorShell.cppm)
    target_link_libraries(KairoEditorImGui PUBLIC KairoEditor KairoRenderer KairoDearImGui Kairo::ImGuizmo)

    add_library(KairoEditorRendererBridge)
    target_sources(KairoEditorRendererBridge PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        SceneRenderBridge.cppm)
    target_link_libraries(KairoEditorRendererBridge PUBLIC KairoEditor KairoRenderer)

    add_library(KairoEditorPhysics)
    target_sources(KairoEditorPhysics PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES
        PhysicsPreview.cppm)
    if(NOT TARGET KairoRendererPhysicsDebug)
        message(FATAL_ERROR "KairoEditor physics preview requires KairoRendererPhysicsDebug. Enable KAIRO_RENDERER_BUILD_PHYSICS_BRIDGE.")
    endif()
    target_link_libraries(KairoEditorPhysics PUBLIC KairoEditor KairoRendererPhysicsDebug)
    target_link_libraries(KairoEditorImGui PUBLIC KairoEditorPhysics)
    if(KAIRO_EDITOR_UI_FONT_PATH)
        target_compile_definitions(KairoEditorImGui PRIVATE
            KAIRO_EDITOR_UI_FONT_PATH="${KAIRO_EDITOR_UI_FONT_PATH}")
    endif()
    if(KAIRO_EDITOR_MONO_FONT_PATH)
        target_compile_definitions(KairoEditorImGui PRIVATE
            KAIRO_EDITOR_MONO_FONT_PATH="${KAIRO_EDITOR_MONO_FONT_PATH}")
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
    # MSVC 19.44 ICEs in symbols.c while destroying the imported module types
    # used by this boundary harness. Keep the production modules and the main
    # editor suite covered on Windows, and automatically restore this focused
    # test when GitHub's runner advances to a newer compiler.
    if(MSVC AND MSVC_VERSION EQUAL 1944)
        message(WARNING
            "Skipping KairoEditorDocumentCompilerTests for MSVC 19.44 due to compiler ICE")
    else()
        add_executable(KairoEditorDocumentCompilerTests tests/DocumentCompilerTests.cpp)
        target_link_libraries(KairoEditorDocumentCompilerTests PRIVATE KairoEditor)
        add_test(NAME KairoEditorDocumentCompilerTests COMMAND KairoEditorDocumentCompilerTests)
    endif()
    if(TARGET KairoEditorApp)
        add_executable(KairoEditorRenderBridgeTests tests/SceneRenderBridgeTests.cpp)
        target_link_libraries(KairoEditorRenderBridgeTests PRIVATE KairoEditorRendererBridge Catch2::Catch2WithMain)
        add_test(NAME KairoEditorRenderBridgeTests COMMAND KairoEditorRenderBridgeTests)
        add_executable(KairoEditorUITests tests/KairoUITests.cpp)
        target_link_libraries(KairoEditorUITests PRIVATE KairoEditorImGui Catch2::Catch2WithMain)
        add_test(NAME KairoEditorUITests COMMAND KairoEditorUITests)
        add_executable(KairoEditorPhysicsPreviewTests tests/PhysicsPreviewTests.cpp)
        target_link_libraries(KairoEditorPhysicsPreviewTests PRIVATE KairoEditorPhysics Catch2::Catch2WithMain)
        add_test(NAME KairoEditorPhysicsPreviewTests COMMAND KairoEditorPhysicsPreviewTests)
        add_test(NAME KairoEditorNativeSmoke COMMAND KairoEditorApp
            --project ${CMAKE_CURRENT_SOURCE_DIR}/examples/StarterProject/Project.kproject
            --document Logic/Player.kdoc
            --frames 3 --no-layout-persistence)
        set_tests_properties(KairoEditorNativeSmoke PROPERTIES TIMEOUT 20)
        set_tests_properties(KairoEditorNativeSmoke PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME KairoEditorViewportScreenshot COMMAND KairoEditorApp
            --project ${CMAKE_CURRENT_SOURCE_DIR}/examples/StarterProject/Project.kproject
            --frames 3 --no-layout-persistence
            --screenshot ${CMAKE_CURRENT_BINARY_DIR}/artifacts/editor-native.ppm)
        set_tests_properties(KairoEditorViewportScreenshot PROPERTIES TIMEOUT 20 SKIP_RETURN_CODE 77)
        add_test(NAME KairoEditorCodeSurfaceSmoke COMMAND KairoEditorApp
            --project ${CMAKE_CURRENT_SOURCE_DIR}/examples/StarterProject/Project.kproject
            --document Logic/Player.kdoc --authoring code
            --frames 3 --no-layout-persistence)
        set_tests_properties(KairoEditorCodeSurfaceSmoke PROPERTIES TIMEOUT 20)
        set_tests_properties(KairoEditorCodeSurfaceSmoke PROPERTIES SKIP_RETURN_CODE 77)
        add_test(NAME KairoEditorRequiresProject COMMAND KairoEditorApp --frames 1)
        set_tests_properties(KairoEditorRequiresProject PROPERTIES WILL_FAIL TRUE TIMEOUT 20)
        if(WIN32)
            # GLFW and the Vulkan loader are DLLs in the Windows vcpkg lane.
            # Make the native smoke tests hermetic instead of relying on the
            # runner's global PATH to discover their runtime dependencies.
            set(_kairo_editor_runtime_path
                "$<TARGET_FILE_DIR:glfw>;$<TARGET_FILE_DIR:Vulkan::Vulkan>;$ENV{PATH}")
            set_tests_properties(KairoEditorNativeSmoke KairoEditorViewportScreenshot KairoEditorCodeSurfaceSmoke
                KairoEditorRequiresProject PROPERTIES
                ENVIRONMENT "PATH=${_kairo_editor_runtime_path}")
        endif()
    endif()
endif()
