# One editor action, from input to undoable scene change

Read alongside [the complete source-reading order](SOURCE_READING_ORDER.md).
The goal is to explain the *authoring pipeline* without confusing it with
the runtime engine, the physics preview or native GPU lifetimes.

## 1. The Editor borrows a scene; it does not define its schema

[`KairoEngineCore/Scene.cppm`](https://github.com/swayam8624/KairoEngineCore/blob/docs/teaching-walkthrough-20261121/Scene.cppm)
owns the canonical Entity, hierarchy, transform and render/physics/audio
component contracts. [`ProjectSession.cppm`](../ProjectSession.cppm)
owns a project's in-memory editable Scene, KairoAssets registry, documents,
dirty flags and disk lifecycle. Returning a mutable `EditScene()` reference
marks the scene dirty.

[`EditorState.cppm`](../EditorState.cppm) holds a borrowed Scene reference
plus selection, edit/play state, current workspace and UI mode.
`ValidateSelection` protects the Inspector/viewport from a deleted entity.

A native Dear ImGui widget is **not** authoritative scene state. Similarly,
a Renderer `MeshHandle` is not the authored KairoAssets `AssetID`.

## 2. Read input as intent, not component mutation

[`EditorInputRouter.cppm`](../EditorInputRouter.cppm) and
[`EditorActions.cppm`](../EditorActions.cppm) translate keyboard/mouse
gestures into editor commands. Focus matters: a shortcut for deleting an
entity must not fire while the code editor is handling text input.

[`EditorShell.cppm`](../EditorShell.cppm) displays the hierarchy,
inspector, viewport, graph, asset browser and dialogs. Its high-level
read path is `Draw` -> `DrawVisiblePanels` ->
`DrawHierarchy/DrawInspector/DrawViewport` -> input/action dispatch.
The large native shell organizes interaction; it should delegate
semantic project changes to model/command modules.

The UI's current appearance is managed through
[`KairoUI.cppm`](../KairoUI.cppm) and associated theme/panel contracts.
Moving widgets to another toolkit should not rewrite the scene schema.

## 3. Command history is a transaction journal

[`CommandHistory.cppm`](../CommandHistory.cppm) defines
`EditorCommand` with `Execute`, `Undo`, `Name`, and optional
`TryMerge`. Commands store enough before/after data to undo a change
without reconstructing it from a widget's current values.

`CommandHistory::Execute` calls the new command's `Execute` **before**
moving its journal cursor. A failing operation is expected to leave
authored data and existing undo/redo state unchanged. After successful
execution, a new edit discards the old redo suffix; compatible consecutive
edits may merge to avoid hundreds of tiny drag steps. The history has a
finite retained-entry capacity.

```text
ImGui user gesture
  -> Editor action (semantic intent)
  -> concrete SceneCommand(ProjectSession, entity ID, before/after)
  -> CommandHistory::Execute()
  -> ProjectSession::EditScene() -> EngineCore::Scene mutation
  -> render/preview refresh
  -> Save project/scene separately when requested
```

`Undo` applies the command's inverse, then moves the cursor only if it
succeeds. This is **authoring** undo/redo, not PhysicsWorld rollback and not
RuntimeWorld's queued gameplay command system.

## 4. Concrete scene edits and hierarchy snapshots

[`SceneCommands.cppm`](../SceneCommands.cppm) implements command classes
for create/rename/delete/reparent, physics-preview settings, render
components and reflection-based properties.

A deletion must preserve more than an Entity ID: descendants, local
transforms, parent links, component descriptors and references must
survive an undo. The source's `CaptureEntitySubtree` records individual
`EntitySnapshot` objects. `RestoreEntitySubtree` first creates their
stable IDs, then restores component data and hierarchy. This prevents the
restore order from deciding which ID a child receives.

**Current limitation found in source review:** `EntitySnapshot` currently
captures names, transforms, tags, camera, light, environment, logic,
rigid-body and collider descriptors, among other fields, but does **not**
include EngineCore's `AudioEmitterComponent` and
`AudioListenerComponent`. Deleting and undoing an audio-bearing entity
therefore needs additional capture/apply coverage and a dedicated
regression test before subtree restoration can be described as complete.
The teaching pass documents this gap without changing behavior.

**Exercise:** create an entity with two children and distinct component
values, delete it, undo, redo, then inspect stable IDs and hierarchy.
Add an audio-emitter/listener test to reproduce and eventually repair
the above limitation in a separate functional change.

## 5. Project documents and compiler boundaries

[`DocumentTypes.cppm`](../DocumentTypes.cppm),
[`DocumentSchema.cppm`](../DocumentSchema.cppm),
[`DocumentValidation.cppm`](../DocumentValidation.cppm),
[`DocumentCommands.cppm`](../DocumentCommands.cppm),
[`DocumentSerialization.cppm`](../DocumentSerialization.cppm) and
[`DocumentCompiler.cppm`](../DocumentCompiler.cppm) separate an
editable document, structural validation, undoable document changes,
disk persistence and executable/derived outputs.

[`ProjectSession.cppm`](../ProjectSession.cppm) owns the project
documents and their history, while
[`ProjectRecovery.cppm`](../ProjectRecovery.cppm) holds recovery state.
`SceneCommands` should not mutate graph text or compiled logic
silently; distinct authoring surfaces have their own transaction
boundaries.

**Exercise:** make a graph edit, undo it, then compile the document.
Verify that compiler outputs derive from the committed document state,
not from an unsaved ImGui text buffer.

## 6. Separate Play mode from authored Edit mode

[`PhysicsPreview.cppm`](../PhysicsPreview.cppm) hosts an isolated
PhysicsEngine simulation. EditorState::Play/Stop only changes session/UI
mode; it is not the physics integration loop by itself.

A preview can move bodies and generate contacts, but a project save
should not silently capture transient PhysicsWorld impulses or
overwrite authored transforms unless an explicit apply/commit action
exists. Compare editor-side preview and the Player's runtime physics
adapter before adding record-and-replay features.

[`SceneRenderBridge.cppm`](../SceneRenderBridge.cppm) is a
compatibility re-export of
[KairoGameEngine's shared realtime scene bridge](https://github.com/swayam8624/KairoGameEngine/blob/docs/teaching-walkthrough-20261121/Runtime/KairoRealtimeRenderBridge/RealtimeSceneBridge.cppm).
Editor and Player should use the same conversion for mesh, material,
camera and lighting. The Editor owns neither Metal device objects nor
their final release.

## 7. How to trace a viewport edit safely

Select a viewport object; record its EngineCore Entity ID and renderer
object-picking identity. Change its transform with the gizmo, then find
the concrete `SetEntity...` command that records before/after values.
Check its validation and `Undo`. Rebuild the renderer scene, but do not
destroy and recreate persistent AssetIDs unnecessarily.

If editing project paths or documents, verify canonical containment,
dirty-state behavior, save/recovery and failure atomicity. Use the
EngineCore `.kproject` schema rather than maintaining an Editor-only
duplicate parser.

## 8. Tests and evidence

Read [`tests/EditorTests.cpp`](../tests/EditorTests.cpp),
[`tests/DocumentCompilerTests.cpp`](../tests/DocumentCompilerTests.cpp),
[`tests/Phase4WorkflowTests.cpp`](../tests/Phase4WorkflowTests.cpp),
[`tests/PhysicsPreviewTests.cpp`](../tests/PhysicsPreviewTests.cpp) and
[`tests/SceneRenderBridgeTests.cpp`](../tests/SceneRenderBridgeTests.cpp).
Also inspect specialized command, recovery and audio tests in the
[complete inventory](SOURCE_READING_ORDER.md).

Do not infer successful native-editor testing from a model-layer test
that never created an ImGui context or graphics backend. Record
exact-head source, host OS, selected API, editor action, expected scene
state and observed undo/redo result.

*Evidence note: inspection-based teaching documentation; no new compiler,
native editor or UI automation result is claimed.*
