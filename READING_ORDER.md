# KairoEditor — source reading order from document to viewport

A native editor is **not** just a renderer with controls. KairoEditor separates the durable authored scene/documents, typed commands and undo, transient project/session state, camera/navigation, and an ImGui shell that presents the application. The C++23 module facade is the starting point; follow the chain below before changing a widget.

```text
.kproject + KairoEngineCore scene / KairoAssets
                   |
          ProjectSession / Documents
                   |
       DocumentSchema + Validation
                   |
        DocumentCommands / History
                   |
       Compiler -> runtime source artifacts
                   |
         EditorState / scene snapshot
                /       \
    EditorShell UI      PhysicsPreview (isolated play mode)
        |                  |
    KairoUI / ImGui      KairoPhysicsEngine
        |
    ViewportController -> KairoRenderer backend
```

## Source path

1. [`KairoEditor.cppm`](KairoEditor.cppm) — public umbrella module. `export import` re-exports distinct contracts but does not make the editor shell a universal source of application state.
2. [`EditorTypes.cppm`](EditorTypes.cppm), [`EditorState.cppm`](EditorState.cppm) and [`PanelState.cppm`](PanelState.cppm) — editor model: selection, play/edit state, panel visibility and workspace state. Native UI is a view of that model.
3. [`ProjectPaths.cppm`](ProjectPaths.cppm), [`ProjectLifecycle.cppm`](ProjectLifecycle.cppm), [`ProjectSession.cppm`](ProjectSession.cppm) and [`ProjectRecovery.cppm`](ProjectRecovery.cppm) — project initialization, persistence, safe autosave/recovery and explicit restoration.
4. [`DocumentTypes.cppm`](DocumentTypes.cppm), [`DocumentSchema.cppm`](DocumentSchema.cppm), [`CoreDocumentSchemas.cppm`](CoreDocumentSchemas.cppm) and [`DocumentValidation.cppm`](DocumentValidation.cppm) — typed authoring document model and invariant checks.
5. [`DocumentSerialization.cppm`](DocumentSerialization.cppm), [`DocumentCommands.cppm`](DocumentCommands.cppm), [`CommandHistory.cppm`](CommandHistory.cppm), [`CommandTransaction.cppm`](CommandTransaction.cppm) — serialization, reversible actions, transactions and undo/redo. Do not mutate documents behind the command/validation layer.
6. [`DocumentCompiler.cppm`](DocumentCompiler.cppm), [`LogicDocumentCompiler.cppm`](LogicDocumentCompiler.cppm), [`ProjectLogicBuild.cppm`](ProjectLogicBuild.cppm) and [`examples/project_compiler_main.cpp`](examples/project_compiler_main.cpp) — convert authored and validated logic into runtime artifacts. Saving incomplete work and compiling a runnable project are intentionally different operations.
7. [`SceneCommands.cppm`](SceneCommands.cppm), [`SceneSelection.cppm`](SceneSelection.cppm), [`SceneFragment.cppm`](SceneFragment.cppm) and [`TransformGizmo.cppm`](TransformGizmo.cppm) — reversible scene edits, selection identity and transform manipulators.
8. [`ViewportController.cppm`](ViewportController.cppm), [`EditorInputRouter.cppm`](EditorInputRouter.cppm), [`NavigationSettings.cppm`](NavigationSettings.cppm), [`KeymapSettings.cppm`](KeymapSettings.cppm) — separate orbit/fly camera control from input focus and authored entity transforms.
9. [`PhysicsPreview.cppm`](PhysicsPreview.cppm) and [`AnimationPreview.cppm`](AnimationPreview.cppm) — isolated preview state. Live simulation must not silently destroy the edit-time authoritative document.
10. [`KairoUI.cppm`](KairoUI.cppm), [`EditorTheme.cppm`](EditorTheme.cppm), [`ImGuiRuntime.cppm`](ImGuiRuntime.cppm), [`ImGuiReflectionInspector.cppm`](ImGuiReflectionInspector.cppm), [`ImGuiGraphCanvas.cppm`](ImGuiGraphCanvas.cppm) — semantic UI components, ImGui backend and views over model/reflection data.
11. [`EditorShell.cppm`](EditorShell.cppm) and [`examples/editor_main.cpp`](examples/editor_main.cpp) — full docked native editor composition. Read this large file *after* understanding data, commands and renderer ownership.
12. [`tests/EditorTests.cpp`](tests/EditorTests.cpp), [`tests/DocumentCompilerTests.cpp`](tests/DocumentCompilerTests.cpp), [`tests/PhysicsPreviewTests.cpp`](tests/PhysicsPreviewTests.cpp), [`tests/Phase4WorkflowTests.cpp`](tests/Phase4WorkflowTests.cpp) — validate commands, lifecycle, compilation and preview behavior.
13. [`CMakeLists.txt`](CMakeLists.txt) and [`STATUS.md`](STATUS.md) — build and host acceptance. The 95% completion target is explicitly unverified.

## Components and interfaces

**`KairoEngineCore`** owns the portable versioned `.kproject`/scene contracts; **`KairoAssets`** supplies asset identity; **`KairoEditor`** owns documents, commands and selection; **`KairoRenderer`** owns graphics device/GPU resources; **`KairoPhysicsEngine`** owns rigid-body simulation. **`KairoUI`** is the semantic UI vocabulary built on Dear ImGui and is not a second scene database. **`ViewportController`** changes the *view camera*; **`TransformGizmo`** changes an *authored entity* through commands. **`ProjectRecovery`** must validate and explicitly restore snapshots rather than blindly replacing files.

An exercise: create a cube, select it, drag with the gizmo, undo/redo, save the project, close/reopen, enter Play mode, pause and return to Edit mode. Trace the exact module that owns each state transition and which data remains persistent.

The original editor code is offered under [MIT](LICENSE); Dear ImGui, platform SDKs, imported assets, and other dependencies retain their own licensing. This guide teaches the implementation, not an unsupported claim of full DCC feature parity.
