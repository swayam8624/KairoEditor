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

The current foundation provides a tested, backend-neutral editor state model:

- validated entity selection and stale-selection recovery
- edit, play, pause, resume, and stop state transitions
- persistent visibility state for hierarchy, inspector, viewport, content,
  console, and statistics panels
- task-focused Scene, World, Logic, Materials, Animation, Simulation, Audio,
  and Profiling workspaces
- Code, Graph, and synchronized Code + Graph authoring surfaces
- bounded cross-surface command history with causal undo/redo branching
- reversible entity creation, deletion, rename, and complete transform edits

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
It validates typed EngineCore asset handles against a live `KairoAssets`
registry, then resolves registered mesh IDs to renderer-owned GPU handles and
extracts visible entities every frame. The hierarchy, transform inspector, and
viewport therefore operate on one scene instead of disconnected demo data.
The starter cube is loaded from a committed project descriptor, asset manifest,
and scene file using the explicit `builtin/cube` metadata record with a
persistent UUID; it is not hidden inside KairoRenderer and remains valid if its
logical path moves.

## Project sessions

The editor opens versioned `.kproject` descriptors instead of constructing a
hardcoded startup scene. A descriptor points to one validated KairoAssets
manifest and startup `.kscene` using project-root-relative portable paths:

```text
kairo-project 1
name "Kairo Starter Project"
assets "Assets.kassets"
startup-scene "Scenes/Main.kscene"
```

`ProjectSession` owns the address-stable scene and asset registry used by editor
state, persistence, the Content Browser, and renderer extraction. Project and
scene replacement reject unsaved work unless the caller explicitly chooses the
discard policy. New projects are built in a unique sibling staging directory
and published by directory rename; descriptors, manifests, and scenes each use
atomic same-directory replacement.

Dear ImGui docking state is stored per project at
`.kairo/editor-layout.ini`. This keeps customized task layouts with their
project instead of leaking one global layout across unrelated work. The
user-specific `.kairo/` directory is ignored by this repository. Pass
`--no-layout-persistence` for read-only or deterministic bounded runs.

The native File menu saves the active scene or all dirty project data, and
`Cmd+S`/`Ctrl+S` saves the scene. This milestone accepts project paths through
`--project`; native create/open dialogs will call the same session API rather
than introducing another persistence path.

## Commands and undo

`CommandHistory` is independent from Dear ImGui and owns a bounded linear
journal. A successful edit after undo removes the obsolete redo branch;
continuous name and transform changes merge into one user operation. Failed
command execution does not advance or truncate history. The native Edit menu
shows the next command name and supports `Cmd/Ctrl+Z` and
`Cmd/Ctrl+Shift+Z`.

Hierarchy and Inspector edits use concrete scene commands. Entity deletion
captures the stable entity ID, name, transform, mesh renderer, camera, rigid
body binding, and collider binding, then restores that complete authored state
on undo. Commands retain a `ProjectSession` reference, so a host must clear its
history before replacing or closing that session; the native host currently
opens one project for its process lifetime.

## Typed document foundation

The backend-neutral authoring kernel now defines strongly separated node and
pin identities, bounded validated UTF-8 text, finite typed values, and an
immutable deterministic node-schema registry. Structured document values cover
flow, booleans, signed integers, floats, vectors, strings, and persistent Kairo
asset IDs. Schema registration rejects duplicate or malformed type/pin keys,
invalid defaults, illegal required outputs, and ambiguous pin contracts before
they can enter an authored graph.

`AuthoringDocument` now supplies the deterministic mutable topology: bounded
node/pin/connection counts, stable ID restoration, finite canvas positions,
schema-instantiated defaults, typed property mutation, non-mutating connection
previews, cardinality enforcement, and incident-edge cleanup. Validation emits
stable node/pin diagnostics for unknown schemas, schema drift, required inputs,
and missing defaults. The same API is intended for graph and structured-text
commands.

Canonical `.kdoc` persistence is self-describing: node, pin, property, stable
identity, and connection contracts survive a load/save cycle even when the
schema provider or plugin is unavailable. Serialization is deterministic,
parsing is bounded and line/column located, and saves replace the destination
atomically. This makes missing schemas a validation diagnostic instead of a
data-loss event.

This is not a claim that the visual node canvas is complete. Reversible
document commands and compiler projection land before the UI can create real
nodes.

Document changes now use the same bounded `CommandHistory` as hierarchy and
Inspector edits. Node create/delete, edge connect/disconnect, movement,
properties, defaults, and rename operations retain stable IDs and complete
incident topology. Continuous drags and value entry coalesce into one causal
undo step.

The compiler boundary validates the complete document and schema snapshot
before invoking a domain backend. Logic, material, audio, animation, and
simulation semantics remain separate compiler implementations. Backend target,
diagnostic references, diagnostic text, and artifact size are validated before
an identity-bearing artifact can enter build or play; failures remain editor
diagnostics rather than partially published output.

`ProjectDocuments` owns multiple open `.kdoc` files against one canonical
project root. It enforces persistent-ID and portable case-insensitive path
uniqueness, bounded tab counts, explicit dirty state, validated reopen,
non-destructive create, atomic save/save-as, explicit replacement policy, and
strong failure behavior. Closing a document clears the shared document command
journal before referenced object lifetimes end.

`ProjectSession` binds that workspace to `KairoAssets`: document creation uses
one persistent ID for asset metadata, file content, compiler artifacts, and UI
selection; registered type and portable path must agree before opening or
saving. Project dirty guards and Save All include every open document, while an
unregistered `.kdoc` must enter through the future explicit import transaction
instead of silently bypassing metadata.

The graph canvas kernel is also backend-neutral. `GraphViewport` provides
bounded cursor-centered zoom, panning, framing, and reversible document/screen
transforms. `GraphSpatialIndex` uses a validated uniform grid for deterministic
node culling, z-ordered hits, and pin-radius queries without scanning an entire
large graph. Selection supports replace/add/subtract/toggle and marquee modes;
connection gestures normalize input- or output-initiated drags while retaining
`AuthoringDocument` as the only compatibility authority.

Code, Graph, and Split are views over the same authored-document model, not
independent sources of truth. The current shell exposes workspace and panel
contracts, while the production graph/text projections remain the next UI
surface rather than being represented by nonfunctional controls.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
./build/KairoEditorApp \
  --project examples/StarterProject/Project.kproject
```

Appending `--frames 3` runs a bounded native smoke session used by CTest to
verify project loading, asset resolution, initialization, frame recording,
presentation, and orderly shutdown.

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
