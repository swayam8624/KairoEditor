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

Code, Graph, and Code + Graph are equivalent views of one authored model. They
must not become separate systems that can silently disagree. The future shared
intermediate representation must provide:

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

## Delivery Order

1. Renderer-owned Vulkan UI frame hooks and viewport texture ownership.
2. Styled Dear ImGui docking shell with workspace switching.
3. Hierarchy, inspector, content browser, console, and statistics panels.
4. Scene viewport selection, gizmos, play controls, and physics debug overlays.
5. Command, undo/redo, asset registry, and property reflection systems.
6. Shared typed graph/code document model, then domain graph editors.
7. Timeline, curves, sequencer, profiling, and large-world tooling.

This ordering prevents beautiful panels from presenting controls for systems
that do not yet exist, while preserving the full product direction in code and
documentation.
