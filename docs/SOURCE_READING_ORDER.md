# Teaching source map — KairoEditor

> A guided source-reading itinerary. It is not a test result or claim that every file is documented yet.

## How to use this guide

The editor owns the *authoring experience*, not the engine scene schema or renderer device. Study state and transaction history before the large native UI shell.

Read each phase in order. Every link goes to a source file or directory in this repository. For a directory, inspect its public interface and then its implementation files. After each phase, identify the caller, the state it owns, the inputs it validates, and its observable outputs.

**Source exercise:** [Undo/redo cursor, gesture merging, reflected edits and subtree restoration](UNDO_COMMANDS_AND_SCENE_SNAPSHOT_LAB.md). The current audio component snapshot gap remains explicitly documented.

## 1. State, project ownership and undoable operations

Use [the complete authoring-transaction walkthrough](AUTHORING_TRANSACTION_WALKTHROUGH.md)
to trace one real input gesture from UI to an undoable EngineCore edit.

1. [`CMakeLists.txt`](../CMakeLists.txt) and [`KairoEditor.cppm`](../KairoEditor.cppm): build and C++23 exports.
2. [`EditorTypes.cppm`](../EditorTypes.cppm), [`PanelState.cppm`](../PanelState.cppm) and [`EditorState.cppm`](../EditorState.cppm): selection, mode, workspace and borrowed Scene reference.
3. [`EditorInputRouter.cppm`](../EditorInputRouter.cppm), [`EditorActions.cppm`](../EditorActions.cppm) and [`KeymapSettings.cppm`](../KeymapSettings.cppm): focus-safe intent routing.
4. [`CommandHistory.cppm`](../CommandHistory.cppm) and [`CommandTransaction.cppm`](../CommandTransaction.cppm): undo/redo cursor, merging, authoring transactions.
5. [`SceneCommands.cppm`](../SceneCommands.cppm), [`SceneFragment.cppm`](../SceneFragment.cppm) and [`SceneSelection.cppm`](../SceneSelection.cppm): reversible component and subtree operations.
6. [`ProjectSession.cppm`](../ProjectSession.cppm), [`ProjectLifecycle.cppm`](../ProjectLifecycle.cppm) and [`ProjectRecovery.cppm`](../ProjectRecovery.cppm): project-owned scene, dirty state, saving and recovery.
7. [`tests/EditorTests.cpp`](../tests/EditorTests.cpp), [`tests/Phase4WorkflowTests.cpp`](../tests/Phase4WorkflowTests.cpp): state and command behavior.

**Exercise:** create an entity, rename it, duplicate a subtree, delete,
undo and redo. Inspect the snapshot coverage—currently audio emitter and
listener components are missing from SceneCommands' EntitySnapshot,
requiring separate functional repair/tests before claiming perfect undo.

## 2. Documents, validation and authoring surfaces

8. [`DocumentTypes.cppm`](../DocumentTypes.cppm), [`DocumentSchema.cppm`](../DocumentSchema.cppm) and [`CoreDocumentSchemas.cppm`](../CoreDocumentSchemas.cppm): portable typed documents.
9. [`AuthoringDocument.cppm`](../AuthoringDocument.cppm), [`DocumentCommands.cppm`](../DocumentCommands.cppm) and [`DocumentValidation.cppm`](../DocumentValidation.cppm): mutable document with reversible changes.
10. [`DocumentSerialization.cppm`](../DocumentSerialization.cppm), [`DocumentCompiler.cppm`](../DocumentCompiler.cppm) and [`LogicDocumentCompiler.cppm`](../LogicDocumentCompiler.cppm): persistence, compilation and derived logic.
11. [`ProjectDocuments.cppm`](../ProjectDocuments.cppm), [`DocumentProjection.cppm`](../DocumentProjection.cppm) and [`AuthoringWorkspaceState.cppm`](../AuthoringWorkspaceState.cppm): editor session/document views.
12. [`GraphCanvas.cppm`](../GraphCanvas.cppm), [`ImGuiGraphCanvas.cppm`](../ImGuiGraphCanvas.cppm), [`AIEditorSession.cppm`](../AIEditorSession.cppm) and [`AIEditorTools.cppm`](../AIEditorTools.cppm): interactive graph/edit assistance.
13. [`tests/DocumentCompilerTests.cpp`](../tests/DocumentCompilerTests.cpp) and the document-focused tests in the full inventory below.

**Exercise:** change a graph document, undo, compile and compare the
resulting artifact with the committed model rather than the current
ImGui text buffer.

## 3. Asset, scene rendering and simulation preview

14. [`AssetBrowserModel.cppm`](../AssetBrowserModel.cppm), [`AssetWorkspace.cppm`](../AssetWorkspace.cppm), [`AssetBrowserRuntime.cppm`](../AssetBrowserRuntime.cppm) and [`AssetScenePlacement.cppm`](../AssetScenePlacement.cppm): persistent IDs and placement.
15. [`SceneRenderBridge.cppm`](../SceneRenderBridge.cppm): compatibility aliases over the shared KairoGameEngine Editor/Player converter.
16. [`ViewportController.cppm`](../ViewportController.cppm) and [`TransformGizmo.cppm`](../TransformGizmo.cppm): camera navigation, selection and undoable transforms.
17. [`PhysicsPreview.cppm`](../PhysicsPreview.cppm), [`AnimationPreview.cppm`](../AnimationPreview.cppm) and [`OfflineRenderAuthoring.cppm`](../OfflineRenderAuthoring.cppm): isolated preview contexts.
18. [`tests/PhysicsPreviewTests.cpp`](../tests/PhysicsPreviewTests.cpp) and [`tests/SceneRenderBridgeTests.cpp`](../tests/SceneRenderBridgeTests.cpp): adapter behavior.

**Exercise:** follow a picked renderer object ID back to an authored
EngineCore Entity and then to a SceneCommand, ensuring a transient GPU
handle is never persisted as a project AssetID.

## 4. Native UI, shell and verification

19. [`KairoUI.cppm`](../KairoUI.cppm), [`EditorTheme.cppm`](../EditorTheme.cppm), [`ImGuiRuntime.cppm`](../ImGuiRuntime.cppm) and [`ImGuiReflectionInspector.cppm`](../ImGuiReflectionInspector.cppm): semantic widgets and toolkit-specific implementation.
20. [`EditorShell.cppm`](../EditorShell.cppm): full Draw -> hierarchy/inspector/viewport -> action -> command dispatch.
21. [`detail/MetalImGuiBridge.mm`](../detail/MetalImGuiBridge.mm), [`NativeDialogs.cppm`](../NativeDialogs.cppm) and [`EditorLayoutRecovery.cppm`](../EditorLayoutRecovery.cppm): native bridge/recovery.
22. Continue with the other authoring, shipping, diagnostics, tests and source files in the inventory below.

**Record:** authoritative Scene before/after, selected Entity ID,
command history cursor, current edit/play mode, dirty project state,
graphics backend and exact native tests. UI model tests are not proof
of complete native Editor acceptance.

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


## Complete source-file inventory

After the curated teaching path, follow this deterministic inventory of first-party source, scripts, shaders, and tests. Grouping is a suggested learning order, not an indication that every function is already annotated. Vendored dependencies, ROMs, binaries, data, media, and generated evidence are excluded.

### Entry points and build (1)

1. [`CMakeLists.txt`](../CMakeLists.txt)

### Interfaces and modules (63)

1. [`AdvancedProductionAuthoring.cppm`](../AdvancedProductionAuthoring.cppm)
2. [`AIEditorSession.cppm`](../AIEditorSession.cppm)
3. [`AIEditorTools.cppm`](../AIEditorTools.cppm)
4. [`AnimationPreview.cppm`](../AnimationPreview.cppm)
5. [`AssetBrowserModel.cppm`](../AssetBrowserModel.cppm)
6. [`AssetBrowserRuntime.cppm`](../AssetBrowserRuntime.cppm)
7. [`AssetScenePlacement.cppm`](../AssetScenePlacement.cppm)
8. [`AssetWorkspace.cppm`](../AssetWorkspace.cppm)
9. [`AuthoringDocument.cppm`](../AuthoringDocument.cppm)
10. [`AuthoringWorkspaceState.cppm`](../AuthoringWorkspaceState.cppm)
11. [`CommandHistory.cppm`](../CommandHistory.cppm)
12. [`CommandTransaction.cppm`](../CommandTransaction.cppm)
13. [`CoreDocumentSchemas.cppm`](../CoreDocumentSchemas.cppm)
14. [`detail/MetalImGuiBridge.hpp`](../detail/MetalImGuiBridge.hpp)
15. [`DocumentCommands.cppm`](../DocumentCommands.cppm)
16. [`DocumentCompiler.cppm`](../DocumentCompiler.cppm)
17. [`DocumentProjection.cppm`](../DocumentProjection.cppm)
18. [`DocumentSchema.cppm`](../DocumentSchema.cppm)
19. [`DocumentSerialization.cppm`](../DocumentSerialization.cppm)
20. [`DocumentTypes.cppm`](../DocumentTypes.cppm)
21. [`DocumentValidation.cppm`](../DocumentValidation.cppm)
22. [`EditorActions.cppm`](../EditorActions.cppm)
23. [`EditorDiagnostics.cppm`](../EditorDiagnostics.cppm)
24. [`EditorInputRouter.cppm`](../EditorInputRouter.cppm)
25. [`EditorLayoutRecovery.cppm`](../EditorLayoutRecovery.cppm)
26. [`EditorShell.cppm`](../EditorShell.cppm)
27. [`EditorState.cppm`](../EditorState.cppm)
28. [`EditorTheme.cppm`](../EditorTheme.cppm)
29. [`EditorTypes.cppm`](../EditorTypes.cppm)
30. [`GraphCanvas.cppm`](../GraphCanvas.cppm)
31. [`ImGuiGraphCanvas.cppm`](../ImGuiGraphCanvas.cppm)
32. [`ImGuiReflectionInspector.cppm`](../ImGuiReflectionInspector.cppm)
33. [`ImGuiRuntime.cppm`](../ImGuiRuntime.cppm)
34. [`KairoEditor.cppm`](../KairoEditor.cppm)
35. [`KairoUI.cppm`](../KairoUI.cppm)
36. [`KeymapSettings.cppm`](../KeymapSettings.cppm)
37. [`LogicDocumentCompiler.cppm`](../LogicDocumentCompiler.cppm)
38. [`MaterialPreviewAuthoring.cppm`](../MaterialPreviewAuthoring.cppm)
39. [`NativeDialogs.cppm`](../NativeDialogs.cppm)
40. [`NativeGameplayAuthoring.cppm`](../NativeGameplayAuthoring.cppm)
41. [`NavigationSettings.cppm`](../NavigationSettings.cppm)
42. [`OfflineRenderAuthoring.cppm`](../OfflineRenderAuthoring.cppm)
43. [`PanelState.cppm`](../PanelState.cppm)
44. [`PhysicsPreview.cppm`](../PhysicsPreview.cppm)
45. [`PrimitiveTypes.cppm`](../PrimitiveTypes.cppm)
46. [`ProductionAuthoring.cppm`](../ProductionAuthoring.cppm)
47. [`ProductionSystemsAuthoring.cppm`](../ProductionSystemsAuthoring.cppm)
48. [`ProjectDescriptor.cppm`](../ProjectDescriptor.cppm)
49. [`ProjectDocuments.cppm`](../ProjectDocuments.cppm)
50. [`ProjectLifecycle.cppm`](../ProjectLifecycle.cppm)
51. [`ProjectLogicBuild.cppm`](../ProjectLogicBuild.cppm)
52. [`ProjectPaths.cppm`](../ProjectPaths.cppm)
53. [`ProjectRecovery.cppm`](../ProjectRecovery.cppm)
54. [`ProjectSession.cppm`](../ProjectSession.cppm)
55. [`SceneCommands.cppm`](../SceneCommands.cppm)
56. [`SceneFragment.cppm`](../SceneFragment.cppm)
57. [`SceneRenderBridge.cppm`](../SceneRenderBridge.cppm)
58. [`SceneSelection.cppm`](../SceneSelection.cppm)
59. [`ShippingAuthoring.cppm`](../ShippingAuthoring.cppm)
60. [`TextFormat.cppm`](../TextFormat.cppm)
61. [`TextValidation.cppm`](../TextValidation.cppm)
62. [`TransformGizmo.cppm`](../TransformGizmo.cppm)
63. [`ViewportController.cppm`](../ViewportController.cppm)

### Implementation and other source (5)

1. [`detail/MetalImGuiBridge.mm`](../detail/MetalImGuiBridge.mm)
2. [`examples/editor_main.cpp`](../examples/editor_main.cpp)
3. [`examples/project_compiler_main.cpp`](../examples/project_compiler_main.cpp)
4. [`LogicDocumentCompilerBuild.cpp`](../LogicDocumentCompilerBuild.cpp)
5. [`ProjectLogicBuildImpl.cpp`](../ProjectLogicBuildImpl.cpp)

### Verification and tests (11)

1. [`tests/AdvancedProductionAuthoringTests.cpp`](../tests/AdvancedProductionAuthoringTests.cpp)
2. [`tests/AssetBrowserRuntimeTests.cpp`](../tests/AssetBrowserRuntimeTests.cpp)
3. [`tests/AssetWorkspaceTests.cpp`](../tests/AssetWorkspaceTests.cpp)
4. [`tests/DocumentCompilerTests.cpp`](../tests/DocumentCompilerTests.cpp)
5. [`tests/EditorTests.cpp`](../tests/EditorTests.cpp)
6. [`tests/KairoUITests.cpp`](../tests/KairoUITests.cpp)
7. [`tests/Phase4WorkflowTests.cpp`](../tests/Phase4WorkflowTests.cpp)
8. [`tests/PhysicsPreviewTests.cpp`](../tests/PhysicsPreviewTests.cpp)
9. [`tests/ProductionAuthoringTests.cpp`](../tests/ProductionAuthoringTests.cpp)
10. [`tests/ProductionSystemsAuthoringTests.cpp`](../tests/ProductionSystemsAuthoringTests.cpp)
11. [`tests/SceneRenderBridgeTests.cpp`](../tests/SceneRenderBridgeTests.cpp)

Snapshot totals: 80 first-party source/build/test files, 102 tracked files overall. Re-audit when files are renamed or added.
