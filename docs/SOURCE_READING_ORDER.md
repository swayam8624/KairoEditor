# KairoEditor — linked teaching source order

Read [README](../README.md) first for platform setup and supported capabilities. This is a **conceptual study sequence**; compile/launch order is defined by CMake (Editor) or Cargo/Tauri/Vite (Hub).

1. [ProjectDescriptor.cppm](../ProjectDescriptor.cppm) — trace its inputs, ownership and next consumer.
2. [ProjectSession.cppm](../ProjectSession.cppm) — trace its inputs, ownership and next consumer.
3. [ProjectLifecycle.cppm](../ProjectLifecycle.cppm) — trace its inputs, ownership and next consumer.
4. [EditorState.cppm](../EditorState.cppm) — trace its inputs, ownership and next consumer.
5. [AuthoringDocument.cppm](../AuthoringDocument.cppm) — trace its inputs, ownership and next consumer.
6. [DocumentSchema.cppm](../DocumentSchema.cppm) — trace its inputs, ownership and next consumer.
7. [DocumentValidation.cppm](../DocumentValidation.cppm) — trace its inputs, ownership and next consumer.
8. [DocumentCommands.cppm](../DocumentCommands.cppm) — trace its inputs, ownership and next consumer.
9. [CommandHistory.cppm](../CommandHistory.cppm) — trace its inputs, ownership and next consumer.
10. [DocumentCompiler.cppm](../DocumentCompiler.cppm) — trace its inputs, ownership and next consumer.
11. [AssetBrowserModel.cppm](../AssetBrowserModel.cppm) — trace its inputs, ownership and next consumer.
12. [AssetWorkspace.cppm](../AssetWorkspace.cppm) — trace its inputs, ownership and next consumer.
13. [SceneCommands.cppm](../SceneCommands.cppm) — trace its inputs, ownership and next consumer.
14. [SceneRenderBridge.cppm](../SceneRenderBridge.cppm) — trace its inputs, ownership and next consumer.
15. [ViewportController.cppm](../ViewportController.cppm) — trace its inputs, ownership and next consumer.
16. [EditorShell.cppm](../EditorShell.cppm) — trace its inputs, ownership and next consumer.
17. [ImGuiReflectionInspector.cppm](../ImGuiReflectionInspector.cppm) — trace its inputs, ownership and next consumer.
18. [KairoEditor.cppm](../KairoEditor.cppm) — trace its inputs, ownership and next consumer.
19. [examples/editor_main.cpp](../examples/editor_main.cpp) — trace its inputs, ownership and next consumer.
20. [tests/EditorTests.cpp](../tests/EditorTests.cpp) — trace its inputs, ownership and next consumer.

## Integration flow

KairoHub/project descriptor → ProjectSession → AuthoringDocument and commands/undo → validation/compilation → KairoAssets/EngineCore scene → SceneRenderBridge → KairoRenderer viewport. EditorState holds UI/session choices; ImGui shell draws panels; KairoReflection describes inspectable properties. Editing is not the same as running KairoPlayer.

## API concepts and questions

For each file, identify state ownership, valid input formats, resource lifetimes, validation and failure paths, and which system consumes the result. Follow import statements and `invoke` command names to their actual definitions, rather than guessing at behavior. Do not confuse editor documents, source-game inspection, runtime scene objects and persistent asset references.

## Practice

1. Follow a project path from user interaction to validated descriptor and either authoring or runtime.
2. Trace one user action to an undoable document mutation, or, for Hub, an invocation into Rust.
3. Find an invalid path/security boundary and explain how it rejects escape or malformed data.
4. Differentiate durable editor/project state from process-local renderer handles and transient UI state.
5. Run applicable unit/integration tests and record which functionality is actually implemented.

Original project code/docs are proposed to be MIT-licensed under [LICENSE](../LICENSE); independently licensed dependencies, icons, sample content and third-party tools retain their own terms.
