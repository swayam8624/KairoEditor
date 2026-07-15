# KairoEditor Product Direction

This document translates the current visual references into editor behavior
and architecture. The reference media are design input only and are not copied
into the repository.

## Product Character

KairoEditor is a viewport-first production environment. It should feel calm at
city scale and precise at node, curve, timeline, and property scale. Dense does
not mean cluttered: the active task determines which tools occupy the screen.

The shell uses dark neutral surfaces, quiet borders, compact controls, clear
selection, and one restrained accent. The large center canvas must remain the
dominant visual signal. Panels are dockable, but curated workspace presets
provide a useful first layout instead of presenting an empty docking system.

## Workspaces

| Workspace | Primary canvas | Supporting tools |
| --- | --- | --- |
| Scene | 3D viewport | hierarchy, inspector, assets, console |
| World | streamed world viewport | world tools, hierarchy, inspector, assets |
| Logic | graph/code split | hierarchy, inspector, viewport, diagnostics |
| Materials | material preview + graph/code | inspector, assets |
| Animation | viewport + timeline | curves, sequencer, hierarchy, inspector |
| Simulation | live physics viewport | physics debug, statistics, inspector |
| Audio | signal/event graph | assets, timeline, inspector |
| Profiling | frame diagnostics | renderer, physics, console, statistics |

Workspace switching changes layout, never the scene, current selection, undo
history, or authored assets. Users can customize and persist each preset.

## Code and Graph Authoring

Code, Graph, and Code + Graph are projections of one active document, not
separate copies that can silently disagree. Kairo makes a deliberate distinction
between documents where bidirectional editing is honest and native code where it
is not:

1. Structured Kairo documents use a typed intermediate representation. Logic,
   materials, audio routing, animation state, and simulation documents can be
   edited through nodes or a constrained textual projection with one command
   stream, validation model, and undo history.
2. Native C++ remains code-first. Kairo can provide dependency, call, data-flow,
   reflection, and profiling graphs, but it will not advertise arbitrary C++ as
   a lossless editable node round trip. Macros, templates, overload resolution,
   control flow, and lifetime semantics make that promise technically false.

The shared intermediate representation must provide:

- stable node, pin, symbol, and source-location identities
- typed ports and compile-time connection validation
- deterministic serialization and human-readable diffs
- graph-to-code and code-to-graph navigation
- incremental diagnostics shown on nodes and source lines
- searchable node creation, keyboard operation, and command palette actions
- rich nodes with focused inline controls, previews, and diagnostics
- explicit unsupported-code regions rather than lossy conversion

Split mode links selection and navigation between both surfaces. Selecting a
node focuses its generated/source declaration; selecting code highlights the
owning graph region. Users may author gameplay, materials, audio events, and
simulation behavior through the same interaction principles while each domain
retains a typed schema.

### Node canvas contract

Nodes have stable serialized IDs, typed pins, explicit execution/data semantics,
and deterministic connection ordering. The graph surface provides cursor-centered
zoom, pan, box selection, alignment, reroute points, comments/groups, minimap,
connection search, copy/paste, duplicate, frame-selection, keyboard deletion,
and undo/redo.

Important values can be edited inline, as in the supplied workflow reference.
Expanded node bodies are focused inspectors for their operation, not decorative
cards nested inside cards. Collapsed nodes preserve title, type/state color,
essential pins, warnings, breakpoints, and live-value state. Validation attaches
to the exact node and pin and also enters the shared diagnostics stream.

Large graphs require spatial indexing, clipped rendering, cached text/layout,
stable zoom limits, and deterministic hit testing. Evaluation order, cycles,
implicit conversions, and compile state must be inspectable rather than hidden.

Compact nodes reveal focused inline editors only for the selected operation;
contextual property surfaces appear near the work instead of permanently
shrinking the canvas. The canvas remains visually dominant during graph work.
Node, constrained text, and split views are projections over one persisted
typed document. Switching views cannot fork state or silently discard data
owned by a temporarily unavailable schema provider.

## Large World Experience

The city-scale reference implies more than a camera looking at many meshes.
World workspace eventually depends on spatial streaming, hierarchical LOD,
instancing, occlusion, origin management, async asset loading, and diagnostics
for cell residency and budgets. The editor UI will expose those systems only
after KairoRenderer and the asset layer own the underlying runtime behavior.

## Usability Contract

- Every destructive action participates in undo/redo transactions.
- Every icon has a tooltip and every major action has a searchable command.
- Drag/drop previews validity before committing a change.
- Invalid graph connections and properties explain the correction in place.
- Keyboard focus is visible; graph, hierarchy, and timeline are navigable.
- Empty, loading, compiling, disconnected, and failed states are designed.
- Play mode clearly separates runtime mutations from authored scene data.
- Layout, selection, and navigation are restored without hiding stale errors.
- Empty panels expose only a real next action; decorative dummy controls do not
  count as implemented functionality.
- Numerical editing supports predictable drag sensitivity, typed entry, units,
  reset-to-default, bounds, and mixed multi-selection values.

## Visual System

- Dark neutral surfaces use several contrast tiers instead of one black field.
- Blue is the primary interaction accent. Type, warning, error, success,
  simulation, and profiler colors retain separate semantic roles.
- The viewport or active authoring canvas remains the largest surface.
- Compact tool headings, stable control dimensions, and quiet separators keep
  dense panels scannable at laptop and desktop resolutions.
- Icons are used for familiar global actions and always have tooltips. Named
  workspaces and ambiguous commands retain text labels.
- Focus, hover, active, disabled, warning, and error states remain distinguishable
  without depending on color alone.

## Current Implementation Boundary

Implemented and verified:

- Vulkan renderer-owned UI frame hooks and a native Dear ImGui docking host;
- real scene hierarchy, selection, transform inspection, play-state controls,
  and renderer scene extraction;
- persistent typed asset IDs resolved through `KairoAssets` rather than paths;
- deterministic EngineCore scene persistence with stable entity IDs;
- validated project descriptors, dirty/save lifecycle, and registry-backed
  Content Browser metadata;
- bounded command history with reversible hierarchy and Inspector edits;
- project-scoped dock layout persistence and a live scene/status summary;
- shared UTF-8/value boundaries and a deterministic typed node-schema registry;
- bounded deterministic graph topology with stable IDs and located validation;
- canonical, bounded, self-describing authoring-document persistence with
  atomic replacement and missing-schema preservation;
- reversible document topology and value commands in the editor-wide bounded
  command journal, including merged continuous edits and stable-ID restoration;
- a validation-gated domain compiler interface with checked diagnostics and
  bounded identity-bearing artifacts; no placeholder execution semantics;
- a project-bound multi-document workspace with stable asset identity,
  portable path uniqueness, explicit dirty/overwrite policy, atomic file
  replacement, and safe command-reference lifetime boundaries;
- `ProjectSession` transactions that keep `.kdoc` identity/path synchronized
  with first-class `KairoAssets::Document` metadata and include document state
  in project save and destructive-lifecycle guards.

Not yet represented as complete product surfaces:

- native project create/open/save-as dialogs and recent documents;
- asset importing, thumbnails, dependency inspection, and source reimport state;
- shared diagnostics and reflected property transactions beyond current scene commands;
- the complete typed document kernel and production graph/code editors;
- timelines, curves, sequencer, audio tools, profilers, and large-world controls.

Until those systems land, their dockable panels are shell boundaries, not
finished features.

## Delivery Order

1. Finish project tooling: native create/open/save-as dialogs, recent projects,
   shared diagnostics, and reflected property commands.
2. Scene viewport selection, transform gizmos, runtime scene cloning, and physics
   debug overlays.
3. Typed document kernel: stable document/node/pin IDs, values, commands,
   validation, serialization, compiler boundary, and backend-neutral tests.
4. Production graph canvas backed by the document kernel.
5. Structured text projection and synchronized Split mode.
6. Native C++ editor backed by `clangd`, build diagnostics, and analytical graphs.
7. Material, animation-state, audio-routing, and simulation document schemas.
8. Timeline, curves, sequencer, profiling, and large-world tooling.

This ordering prevents beautiful panels from presenting controls for systems
that do not yet exist, while preserving the full product direction in code and
documentation.

## Acceptance Rules

- The editor opens a project, restores its layouts, and resolves assets by stable
  identity after path changes.
- Scene edits save and reload without changing entity or asset identities.
- Structured documents execute identically after supported graph or text edits.
- Undo/redo crosses hierarchy, Inspector, graph, code, and timeline commands in
  causal order.
- Invalid documents cannot build or enter play without located diagnostics.
- Native C++ and structured documents expose their different round-trip
  guarantees through available actions, not only explanatory text.
- Every released workspace has at least one complete
  create-edit-validate-save-run workflow.
