# KairoEditor

`KairoEditor` is the authoring layer for Kairo scenes. It is intentionally a
separate repository from `KairoEngineCore`: EngineCore runs scenes, while the
editor owns selection, workspace state, inspection, and play/edit transitions.

The versioned `.kproject` descriptor and bounded text persistence primitives
live in `KairoEngineCore` so KairoHub, KairoEditor, build tools, and KairoPlayer
share one validated bootstrap contract. Editor-named modules remain as source
compatibility facades; they do not duplicate parser or atomic-save logic.

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
- action-based keyboard routing that protects code and graph text input
- renderer-backed orbit/fly navigation, transform tools, framing, and primitive creation
- isolated Play-mode KairoPhysicsEngine preview with box colliders and debug draw

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
toolbar, play controls, hierarchy commands, asset filtering, inspector
sections, document lifecycle modals, and code-authoring save/apply/revert
actions. Canvas interaction and popup lifetime remain direct ImGui integration
points because they are backend mechanics, not visual policy; more feature
panels move over incrementally without changing project or authoring state.

The module is intentionally implemented on Dear ImGui today, but its public
tokens and semantic intent are engine-owned. This preserves one visual language
for a later custom or Qt backend without introducing a second editor model.

`KairoEditorApp` is the first native shell milestone. It uses the official
Dear ImGui docking release, KairoRenderer's existing Vulkan device/render pass,
the Kairo neutral theme, curated docking, workspace controls, live hierarchy
selection, transform inspection, play controls, and runtime UI statistics.
The editor never creates a second Vulkan device or render pass; ImGui records
through the renderer's validated tooling-overlay contract.

The viewport is real KairoRenderer content behind transparent tooling chrome.
`ViewportController` owns backend-neutral orbit/fly state; the native shell
translates input and passes a camera pose to KairoRenderer before every frame.
The starter project opens with a persisted cube and ground plane. `Shift+A`
creates persistent asset-backed cube, plane, UV sphere, and cylinder entities.
These are honest blockout primitives, not a claim of Blender-style arbitrary
topology editing. See [docs/EDITOR_CONTROLS.md](docs/EDITOR_CONTROLS.md).

The native host also contains a narrow `KairoEditorRendererBridge` boundary.
It validates typed EngineCore asset handles against a live `KairoAssets`
registry, then resolves registered mesh IDs to renderer-owned GPU handles and
extracts visible entities every frame. The hierarchy, transform inspector, and
viewport therefore operate on one scene instead of disconnected demo data.
Source OBJ meshes execute the shared KairoAssets transaction on project open:
the importer fingerprints source bytes, reuses or publishes an immutable entry
under `.kairo/derived-data`, parses the portable `kairo.mesh.v1` artifact, and
adapts it once into KairoRenderer upload geometry. The starter cube is therefore
a committed `Meshes/ShowcaseCube.obj` referenced by the project manifest and a
persistent UUID, not geometry hidden in the editor or renderer. Unknown
importers, missing files, corrupt cache entries, invalid topology, and
line/column-located OBJ errors abort loading rather than producing partial GPU
state. The import bridge test exercises both cache-miss and cache-hit paths.

## Project sessions

The editor opens versioned `.kproject` descriptors instead of constructing a
hardcoded startup scene. A descriptor points to one validated KairoAssets
manifest and startup `.kscene` using project-root-relative portable paths:

```text
kairo-project 2
name "Kairo Starter Project"
engine-version "0.1.0"
assets "Assets.kassets"
startup-scene "Scenes/Main.kscene"
input-map "Config/Input.kinput"
rendering-profile "desktop"
build-profile "Development" development "Build/Development"
build-profile "Release" release "Build/Release"
```

The referenced `.kinput` file is the shared EngineCore gameplay-action format,
not an editor shortcut profile. Editor keymaps remain user-specific tool
settings; project input actions are portable runtime data consumed identically
by KairoPlayer, visual logic, and C++ gameplay.

Format 2 additionally accepts repeatable `plugin` statements. Format 1 remains
readable and receives deterministic engine, input, rendering, and build-profile
defaults in memory; the migration reaches disk only through an explicit project
save.

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

### Autosave and recovery

The editor creates a recovery point every 30 seconds while authoritative
project state or an editor text buffer is dirty. `File > Create Recovery Point`
creates one immediately. Recovery is a journal, not an implicit save:

- authored project files and editor dirty flags are left unchanged;
- snapshots are staged and atomically published under
  `.kairo/recovery/snapshot-*`;
- project descriptor, asset registry, active scene, open documents, active tab,
  and raw text drafts are captured;
- every payload has a bounded byte count and checksum;
- the newest 10 valid snapshots are retained; malformed or foreign directories
  are preserved for inspection rather than deleted;
- raw text drafts may be syntactically invalid and are never written over a
  canonical `.kdoc` during project restoration;
- explicit restoration validates the complete snapshot, backs up every target
  under `.kairo/recovery-backups`, and rolls back touched files if replacement
  fails.

`ProjectSession::CreateRecoveryPoint` and `RestoreRecoveryPoint` provide the
backend-neutral contract used by the editor and KairoHub. This makes recovery
testable without Dear ImGui or a native window.

KairoHub launches an explicitly selected snapshot with:

```bash
./build/KairoEditorApp \
  --project /absolute/path/Project.kproject \
  --recovery-snapshot /absolute/path/.kairo/recovery/snapshot-...
```

The editor rejects snapshots owned by another project and disables persisted
docking for the recovered session. Recovery is never selected implicitly by
this command line interface.

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

## AI command boundary

`AIEditorTools` exposes three strict JSON tools: read the entity list, create an
empty entity, and delete an entity subtree. `AIEditorSession` owns the
provider-neutral Ask/Plan/Agent conversation above that command boundary. It
constructs bounded project context, runs one cancellable request on an owned
worker, copies streamed text through a locked mailbox, and returns tool previews
to the editor thread.

The native `Kairo AI` dock exposes Ask, Plan, and Agent modes, streamed text,
cancellation, and explicit mutation approval/rejection. The core editor remains
credential-free by default. Enable the optional host adapter and provide secrets
only through its process environment:

```bash
cmake -S . -B build-ai-cloud -G Ninja \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DKAIRO_EDITOR_BUILD_AI_CLOUD_PROVIDER=ON
cmake --build build-ai-cloud --target KairoEditorApp

KAIRO_AI_API_KEY='...' \
KAIRO_AI_MODEL='your-openai-compatible-model' \
./build-ai-cloud/KairoEditorApp \
  --project examples/StarterProject/Project.kproject
```

`KAIRO_AI_ENDPOINT` may override the default HTTPS Chat Completions endpoint.
Remote plaintext HTTP is rejected; loopback HTTP remains available for a local
development provider. Keys are passed only to the transport constructor and are
never stored in `.kproject`, scene, recovery, layout, or editor log files.

Every call is parsed and fully previewed before execution. Unknown fields are
rejected, persistent entity IDs are range-checked, and mutations pass through
KairoAI's exact-call approval policy. Approved changes execute through the same
`CreateEntityCommand`, `DeleteEntityCommand`, and `CommandHistory` used by the
manual editor, so they remain dirty-state aware and undoable. The bridge records
both rejected and executed calls in a process-local audit trail. Model output
is never treated as approval, and credential access is not an editor tool.
Read-only calls may resolve immediately under policy. Scene mutations remain in
a pending queue until the host supplies an exact out-of-band approval; rejection
and duplicate-resolution paths are explicit and tested.

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

The shipped core catalog contributes 29 validated contracts across logic,
material, audio, animation-state, and simulation documents. Registry search is
bounded, tokenized, ASCII case-insensitive, and deterministically relevance
ordered. Scene Entity and Vector 3 value nodes feed typed transform and physics
actions without conflating scene-local entity IDs with persistent asset IDs.
These contracts define authored data shape; each domain compiler owns runtime
meaning rather than letting the editor emulate execution.

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

`LogicDocumentCompiler` is the first concrete backend. It deterministically
lowers Begin Play, Tick, Input Action, Branch, Add Float, Print, Set Position,
and Apply Impulse graphs to EngineCore's bounded `kairo.logic.bytecode-v1`
format. It rejects flow/data cycles, missing runtime inputs, dynamic strings,
and nodes without executable semantics with node/pin-local diagnostics. The
runtime consumes only the validated bytecode: it never links editor code or
parses mutable `.kdoc` authoring data.

Saving and building are intentionally separate. Authors can save incomplete or
temporarily invalid graphs. `KairoProjectCompiler` resolves only the unique
logic documents attached to startup-scene entities, compiles all of them before
publishing any result, and writes atomic source-bound artifacts under
`.kairo/compiled-logic/<document-id>.klogic`. Each artifact records the source
asset ID and SHA-256 of the exact saved `.kdoc`; KairoPlayer rejects missing,
foreign, or stale output instead of executing old gameplay.

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

Logic now includes Begin Play, Tick, Input Action, Branch, Spawn Entity, Set
Position, math, and diagnostic contracts. Simulation includes tick, forces,
impulses, collision events, trigger state, and raycast contracts. These nodes
are typed editor data; runtime behavior is claimed only once a matching domain
compiler/runtime is registered.

## Physics preview

The Inspector can add or remove an entity's **Dynamic Box Physics** binding as
an undoable scene command. On Play, `PhysicsPreview` clones the authored scene,
turns marked entities into real KairoPhysicsEngine dynamic bodies with box
colliders derived from local scale, and maps simulated poses to renderer scene
extraction. Collider outlines, contacts, and optional broadphase AABBs come
from `KairoRendererPhysicsDebug`. Stop drops the runtime copy, so simulation
never changes saved authoring transforms or persists stale physics IDs.

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
./build/KairoProjectCompiler examples/StarterProject/Project.kproject
./build/KairoEditorApp \
  --project examples/StarterProject/Project.kproject \
  --document Logic/Player.kdoc \
  --authoring split
```

From the game-engine superproject:

```bash
cd /Users/swayamsingal/Desktop/Programming/Kairo
cmake --preset dev-clang
cmake --build --preset dev-clang --parallel
./build/dev-clang/KairoEditor/KairoProjectCompiler \
  "$PWD/KairoEditor/examples/StarterProject/Project.kproject"
./build/dev-clang/KairoEditor/KairoEditorApp \
  --project "$PWD/KairoEditor/examples/StarterProject/Project.kproject" \
  --document Logic/Player.kdoc --authoring split
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
