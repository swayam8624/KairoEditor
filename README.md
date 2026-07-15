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
- reflection-driven scalar inspection for Name, Camera, mesh visibility, and
  physics bindings, with component invariants checked before a command commits

The visual direction is viewport-first and production-dense: low-chrome dark
panels, a strong central canvas, rich inspectable nodes, timeline/curve tools,
and focused workspace presets. See [docs/EDITOR_PRODUCT_SPEC.md](docs/EDITOR_PRODUCT_SPEC.md).
The current and future UI ownership boundaries are recorded in
[docs/UI_ARCHITECTURE.md](docs/UI_ARCHITECTURE.md).

## KairoUI design system

`Kairo.Editor.UI` is the semantic UI boundary inside the native shell. It owns
the charcoal/yellow design tokens and reusable `ActionButton`, `ToolbarButton`,
`SearchField`, `SectionHeader`, `MutedText`, and `StatusText` controls. Editor
features call those named controls rather than adding ad hoc colors and widget
styling directly to their panels. The initial migration covers the workspace
toolbar, play controls, hierarchy commands, asset filtering, and inspector
sections; more panels move over incrementally without changing project or
authoring state.

The module is intentionally implemented on Dear ImGui today, but its public
tokens and semantic intent are engine-owned. This preserves one visual language
for a later custom or Qt backend without introducing a second editor model.

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

The native File menu creates, saves, and closes typed authoring documents in
addition to saving the scene or all dirty project data. Registered `.kdoc`
assets open from the Content Browser with a double-click; open-document tabs
retain their view state and require an explicit Save, Discard, or Cancel choice
before closing dirty work. `Cmd+S`/`Ctrl+S` saves the focused document or the
scene according to editor focus, and undo/redo routes to the corresponding
command history. This milestone accepts project paths through
`--project`; native create/open dialogs will call the same session API rather
than introducing another persistence path.

## Commands and undo

`CommandHistory` is independent from Dear ImGui and owns a bounded linear
journal. A successful edit after undo removes the obsolete redo branch;
continuous name and transform changes merge into one user operation. Failed
command execution does not advance or truncate history. The native Edit menu
shows the next command name and supports `Cmd/Ctrl+Z` and
`Cmd/Ctrl+Shift+Z`.

Hierarchy and Inspector edits use concrete scene commands. Reflection-backed
Inspector fields are discovered from `KairoReflection` metadata rather than
duplicated widget definitions: the editor resolves the target component only
when a command executes, writes through the registered descriptor, validates
component invariants, and restores the pre-edit value if validation fails.
This currently covers all scalar EngineCore descriptors. Transform remains a
purpose-built composite editor until vector/quaternion property adapters have
the same stable metadata and serialization contract.

Entity deletion
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

The shipped core catalog contributes 16 validated contracts across logic,
material, audio, animation-state, and simulation documents. Registry search is
bounded, tokenized, ASCII case-insensitive, and deterministically relevance
ordered. These contracts define authored data shape; each domain compiler still
owns runtime meaning rather than letting the editor emulate execution.

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

The native Graph panel now renders that kernel through a custom ImGui draw-list
surface. It provides cursor-centered wheel zoom, middle-button panning, `F` to
frame authored nodes, spatially culled node drawing, typed pin colors, Bezier
connections, deterministic hit testing, modifier-aware selection, marquee
selection, compatibility-colored connection previews, and transactional
multi-node dragging. Canvas mutations enter the shared document command history
and dirty/conflict tracking rather than editing widget-local copies. The `+`
control, right-click, or `Space` opens a searchable category-grouped node
palette; the chosen schema is instantiated at the canvas center or pointer and
selected through the same reversible command path.

The constrained Code surface uses `DocumentTextProjection`, not a second data
model. Canonical UTF-8 text carries byte-accurate spans back to node, pin,
property, and connection identities. Edited text is fully parsed before one
reversible content swap; persistent ID and document kind cannot be changed from
the text view. Successive valid text edits merge while malformed edits leave
the graph and command history untouched.

The native Code panel edits that canonical projection in a dynamically resized
monospaced buffer. Apply parses the complete draft before requesting mutable
project access, so malformed input reports its exact format error without
changing or dirtying the document. Revert reloads the current graph. Dirty
drafts mark tabs, participate in close confirmation, and are validated/applied
before Save or Save All; a graph change made after editing began raises an
explicit stale-draft conflict that cannot be overwritten silently.

Code, Graph, and Split are views over the same authored-document model, not
independent sources of truth. Both native surfaces mutate production document
contracts and share persistence, conflict detection, validation, and undo.

`AuthoringWorkspaceState` now owns transient state separately for every open
document: graph pan/zoom, graph selection, canonical structured-text baseline,
and the live text draft. Clean drafts automatically follow graph and undo/redo
changes. Dirty drafts are retained, and an external graph change raises an
explicit conflict instead of silently discarding text or applying it over a
newer document. This state is UI-backend-neutral so styled ImGui and a future
KairoUI implementation can share identical conflict and tab-switch behavior.

## Build and run

```bash
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++
cmake --build build
ctest --test-dir build --output-on-failure
./build/KairoEditorApp \
  --project examples/StarterProject/Project.kproject \
  --document Logic/Player.kdoc \
  --authoring split
```

Appending `--frames 3` runs a bounded native smoke session used by CTest to
verify project loading, asset resolution, initialization, frame recording,
presentation, and orderly shutdown.

On a CI host that exposes X/GLFW but no Vulkan presentation extension, the
native smoke executable exits with CTest skip code `77`. This is limited to the
typed `PresentationUnavailableError`; renderer initialization or frame failures
continue to fail the test.

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
