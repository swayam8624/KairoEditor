# KairoEditor

`KairoEditor` is the authoring layer for Kairo scenes. It is intentionally a
separate repository from `KairoEngineCore`: EngineCore runs scenes, while the
editor owns selection, workspace state, inspection, and play/edit transitions.

![KairoEditor native docked shell](docs/images/editor-shell.png)

```text
KairoMath -> KairoEngineCore -> KairoEditor
                           -> KairoRenderer (viewport backend)
                           -> KairoPhysicsEngine (play-mode backend)
```

The initial milestone provides a tested, backend-neutral editor state model:

- validated entity selection and stale-selection recovery
- edit, play, pause, resume, and stop state transitions
- persistent visibility state for hierarchy, inspector, viewport, content,
  console, and statistics panels
- task-focused Scene, World, Logic, Materials, Animation, Simulation, Audio,
  and Profiling workspaces
- Code, Graph, and synchronized Code + Graph authoring surfaces

The visual direction is viewport-first and production-dense: low-chrome dark
panels, a strong central canvas, rich inspectable nodes, timeline/curve tools,
and focused workspace presets. See [docs/EDITOR_PRODUCT_SPEC.md](docs/EDITOR_PRODUCT_SPEC.md).

`KairoEditorApp` is the first native shell milestone. It uses the official
Dear ImGui docking release, KairoRenderer's existing Vulkan device/render pass,
the Kairo neutral theme, curated docking, workspace controls, live hierarchy
selection, transform inspection, play controls, and runtime UI statistics.
The editor never creates a second Vulkan device or render pass; ImGui records
through the renderer's validated tooling-overlay contract.

The native host also contains a narrow `KairoEditorRendererBridge` boundary.
It resolves serialized EngineCore mesh keys to renderer-owned GPU handles and
extracts visible entities every frame. The hierarchy, transform inspector, and
viewport therefore operate on one scene instead of disconnected demo data.
The startup cube is an editable untitled-scene object using the explicit
`builtin:cube` binding; it is not hidden inside KairoRenderer.

Code, Graph, and Split are views over one future authored-document model, not
independent sources of truth. The current shell exposes the workspace and panel
contracts without pretending that the typed graph compiler already exists.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
./build/KairoEditorApp
```

`KairoEditorApp --frames 3` runs a bounded native smoke session used by CTest
to verify initialization, frame recording, presentation, and orderly shutdown.

For CI or consumers that only need the backend-neutral editor state library:

```bash
cmake -S . -B build-headless -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DKAIRO_EDITOR_BUILD_APP=OFF
cmake --build build-headless
ctest --test-dir build-headless --output-on-failure
```

The application requires GLFW, Vulkan headers, and a Vulkan loader. On macOS,
the renderer runs through MoltenVK. CMake prefers sibling `KairoEngineCore` and
`KairoRenderer` checkouts and falls back to their GitHub repositories.
