# Teaching source map — KairoEditor

> A guided source-reading itinerary. It is not a test result or claim that every file is documented yet.

## How to use this guide

The editor owns the *authoring experience*, not the engine scene schema or renderer device. Study state and transaction history before the large native UI shell.

Read each phase in order. Every link goes to a source file or directory in this repository. For a directory, inspect its public interface and then its implementation files. After each phase, identify the caller, the state it owns, the inputs it validates, and its observable outputs.

## 1. State and commands

1. [`README.md`](README.md)
2. [`EditorTypes.cppm`](EditorTypes.cppm)
3. [`EditorState.cppm`](EditorState.cppm)
4. [`PanelState.cppm`](PanelState.cppm)
5. [`EditorActions.cppm`](EditorActions.cppm)
6. [`CommandHistory.cppm`](CommandHistory.cppm)
7. [`CommandTransaction.cppm`](CommandTransaction.cppm)
8. [`SceneCommands.cppm`](SceneCommands.cppm)

## 2. Documents and persistence

1. [`DocumentTypes.cppm`](DocumentTypes.cppm)
2. [`DocumentSchema.cppm`](DocumentSchema.cppm)
3. [`DocumentValidation.cppm`](DocumentValidation.cppm)
4. [`DocumentCommands.cppm`](DocumentCommands.cppm)
5. [`DocumentSerialization.cppm`](DocumentSerialization.cppm)
6. [`DocumentCompiler.cppm`](DocumentCompiler.cppm)
7. [`ProjectSession.cppm`](ProjectSession.cppm)
8. [`ProjectRecovery.cppm`](ProjectRecovery.cppm)

## 3. Authoring and runtime boundaries

1. [`AssetBrowserModel.cppm`](AssetBrowserModel.cppm)
2. [`SceneSelection.cppm`](SceneSelection.cppm)
3. [`ViewportController.cppm`](ViewportController.cppm)
4. [`TransformGizmo.cppm`](TransformGizmo.cppm)
5. [`PhysicsPreview.cppm`](PhysicsPreview.cppm)
6. [`SceneRenderBridge.cppm`](SceneRenderBridge.cppm)
7. [`ImGuiRuntime.cppm`](ImGuiRuntime.cppm)
8. [`KairoUI.cppm`](KairoUI.cppm)
9. [`EditorShell.cppm`](EditorShell.cppm)

## 4. Tests

1. [`tests/EditorTests.cpp`](tests/EditorTests.cpp)
2. [`tests/DocumentCompilerTests.cpp`](tests/DocumentCompilerTests.cpp)
3. [`tests/Phase4WorkflowTests.cpp`](tests/Phase4WorkflowTests.cpp)
4. [`tests/PhysicsPreviewTests.cpp`](tests/PhysicsPreviewTests.cpp)
5. [`tests/SceneRenderBridgeTests.cpp`](tests/SceneRenderBridgeTests.cpp)

## End-to-end interaction map

Inputs become editor actions and undoable commands. EditorState tracks selection/workspace; document validators constrain authoring changes; project session/recovery persists them; renderer and physics preview adapters display and simulate without giving the UI ownership of their GPU/physics state.

## APIs and code-reading questions

For each function, distinguish **contract** (preconditions, postconditions, failure modes), **implementation** (state/algorithms), **collaborators** (imports, calls and consumers), **lifetime/ownership**, and **verification** (test or evidence that checks it). For C++ modules, `export module` names the public compiled interface and `import` expresses module dependencies; an implementation unit's `module ...;` contributes definitions without exporting them.

## Boundaries and honesty

The source contains a large `EditorShell.cppm`. Break future teaching comments at concrete ownership and callback boundaries, not one comment per obvious line. Completion remains gated on exact-head native acceptance.

## Documentation rules

- Teaching comments describe *why*, invariants, edge cases, API ownership and cross-file effects; avoid narrating obvious assignments.
- Preserve behavior during documentation-only edits; keep correctness changes in separate reviewed commits.
- Link function names and tests where possible; mark claims as source-present, CI-observed, host-verified or research-evidenced.
- License grants apply only to material for which the repository owner holds rights; third-party source, models, media and datasets keep their own notices.
