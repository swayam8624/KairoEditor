# Kairo Editor delivery checklist

Checked items have both an implementation and a verification path.

## Phase 0 — Navigable canvases

- [x] Option + left drag orbits the 3D viewport.
- [x] Shift + Option + left drag pans the 3D viewport.
- [x] Control + Option + left drag dollies the 3D viewport.
- [x] Middle drag orbit and Shift + middle drag pan remain available for mice.
- [x] Right drag provides fly mouse-look with W/A/S/D/Q/E translation.
- [x] An active navigation drag continues after the pointer leaves the viewport.
- [x] Escape cancels viewport navigation and restores the normal cursor.
- [x] Two-finger scroll is configurable as pan or dolly.
- [x] Space + left drag pans every implemented 2D graph canvas without moving nodes.
- [x] Pointer-centered wheel/pinch events zoom 2D graph canvases.
- [x] Navigation gestures do not issue viewport picks or compete with transform gizmos.
- [x] Sensitivities, fly speed, inversion, and scroll behavior persist outside projects.
- [x] Help > Navigation Controls documents the live bindings.
- [x] Backend-neutral camera and settings regression coverage is present.

Exit criterion: a Mac trackpad can orbit, pan, dolly, fly, frame, manipulate, and pan all currently implemented 2D canvases.

## Phase 1 — Daily editor usability

- [x] Launching without arguments opens the native macOS project chooser.
- [x] File > New Project uses a native folder chooser and transactional creation.
- [x] File > Open Project uses the native project chooser.
- [x] File > Save Project As clones through a validated staging directory.
- [x] Recent projects persist outside projects and appear under File > Open Recent.
- [x] Project switches reload the host so renderer asset handles cannot cross projects.
- [x] Unsaved project switches offer Save and Continue, Discard, or Cancel.
- [x] Documents expose create, save, save-all, close, and unsaved-close handling.
- [x] Recovery points are automatic/manual, rotate safely, and restore from the File menu.
- [x] Operation failures feed the shared diagnostics store and Console.
- [x] Typed entity, asset, and graph diagnostics navigate when clicked.
- [x] Keyboard Shortcuts lists bindings and supports replace, unbind, profile, reset, validation, and persistence.
- [x] Cmd/Ctrl + Shift + P opens a searchable semantic command palette.
- [x] Navigation preferences persist outside projects.
- [x] Project, recovery, recent-project, keymap, diagnostics, and navigation contracts have backend-neutral tests.

Exit criterion: on macOS, a project can be created, edited, saved, closed, reopened, and recovered without Terminal.

## Phase 2 — Scene authoring contract

- [x] Cameras serialize projection, clipping, clear color, priority, and render-layer masks.
- [x] Directional, point, and spot lights serialize type-specific lighting and shadow settings.
- [x] Scene environments serialize color, texture, and intensity.
- [x] Mesh renderers serialize mesh, primary/additional material slots, visibility, shadows, and render layers.
- [x] Imported scene instances serialize their scene asset, visibility, shadow state, and render layers.
- [x] Viewport extraction filters meshes, imported scene primitives, and lights through the active camera layer mask.
- [x] Zero render-layer masks are rejected instead of silently producing an ambiguous scene.
- [x] Rendering components participate in create, edit, duplicate, undo/redo, save/reload, and play-clone workflows.
- [x] Backend-neutral scene-bridge regression coverage verifies layer filtering and environment propagation.

Exit criterion: authored rendering state survives save/reload and reaches Editor/Player render extraction without losing component data.

## Phase 3 — Real-time material and environment path

- [x] Texture upload supports mip chains, samplers, and explicit linear versus sRGB interpretation.
- [x] Real-time PBR consumes base color, normal, metallic/roughness, emissive, and occlusion channels.
- [x] Alpha modes and double-sided material state reach the renderer.
- [x] Imported glTF scene primitives preserve their individual material bindings.
- [x] Authored environment textures are registered, bound through Vulkan descriptors, and sampled as equirectangular image-based lighting.
- [x] Environment intensity reaches the shader and is validated as finite and non-negative.
- [x] Scene-authored lights drive the viewport without a hidden hardcoded light.
- [x] Renderer validation and native viewport, normals, and lighting captures cover the path.

Exit criterion: the real-time viewport renders authored PBR materials, scene lights, shadows, and environment illumination through project assets.

## Phase 4 — Editor scene tools

- [x] Camera, directional-light, point-light, spot-light, environment, mesh-renderer, and scene-instance components can be added and removed in the inspector.
- [x] The hierarchy identifies cameras, lights, environments, scene instances, and mesh renderers.
- [x] Camera, light, environment, mesh-renderer, and scene-instance properties are editable with undo/redo commands.
- [x] Mesh and primary/additional material slots are assignable from project assets, with add, replace, and remove operations.
- [x] Camera view-through acts as a live camera preview and applies that camera's render-layer mask.
- [x] Transform gizmos manipulate camera and light entities, with viewport debug visualization for scene helpers.
- [x] Environment color, texture, and intensity controls update the live viewport path.
- [x] Native editor smoke and screenshot tests verify the integrated scene-authoring surface.

Exit criterion: a user can create and edit the complete rendering scene setup from the Editor and immediately inspect the authored result in the viewport.

## Phase 5 — Shared content end-to-end

- [x] A repository-owned shared-content project contains a hash-locked GLB fixture and a textured external-image glTF fixture.
- [x] The project uses stable asset IDs for its scene instance, texture, camera, light, and environment references.
- [x] KairoAssets validates the project manifest and imports the shared content deterministically.
- [x] KairoEditor loads and renders the project through its normal project/session and scene-instance path.
- [x] KairoPlayer loads the same project and startup scene without editor-only paths.
- [x] CTest registers checksum, project-validation, Editor smoke, and Player smoke gates unconditionally when the superbuild is configured.

Exit criterion: Editor and Player consume the same committed project, scene, assets, transforms, camera, light, environment, and materials.

## Phase 6 — Offline render bridge

- [x] The Editor snapshots the authoritative EngineCore scene rather than requiring a second ray-tracer scene file.
- [x] Camera, lights, mesh renderers, imported scene instances, PBR scalar material data, base-color textures, and environment textures cross the shared render bridge.
- [x] Texture conversion preserves linear versus sRGB interpretation and supports the cooked 8-bit and half-float formats.
- [x] Unsupported or unresolved scene features produce located conversion diagnostics.
- [x] Offline render jobs run asynchronously with progress polling and cancellation.
- [x] The Render Results panel owns render dimensions, pass count, project-relative output, status, progress, diagnostics, and result/metadata paths.
- [x] Snapshot conversion, host resolution, service output, and Editor controller behavior have backend-neutral regression coverage.

Exit criterion: the native Editor can submit its current EngineCore scene to KairoRayTracer and track a project-owned output without a second scene format.

## Phase 7 — Code-driven gameplay API

- [x] Native gameplay types register stable reflected type/property metadata and validated factories.
- [x] Linked game modules can populate process-wide Editor and Player registries through explicit registration hooks.
- [x] The Native Gameplay panel attaches registered types to the selected stable entity and edits reflected bool, number, vector, entity, and string fields.
- [x] Attachments persist to the shared `Config/NativeGameplay.knative` manifest and reject missing linked types.
- [x] Player consumes that manifest and dispatches BeginPlay, FixedUpdate, Update, Event, and EndPlay through the runtime bridge.
- [x] Structural mutation remains queued through RuntimeWorld, with explicit restart/reload policy instead of unsafe binary hot replacement.
- [x] Registration, authoring, persistence, Player execution, and lifecycle behavior have regression coverage.

Exit criterion: a linked C++ gameplay type is discoverable and authorable in Editor, persists by stable entity ID, and executes through the same reflected contract in Player.

## Phase 8 — Editable mesh kernel

- [x] EditableMesh owns stable vertex, edge, loop, polygon, UV, material-slot, crease, normal, and selection identities separately from runtime MeshArtifact data.
- [x] Half-edge/loop topology validation rejects broken twins, rings, references, and non-manifold state.
- [x] Transactional topology operations retain bounded undo/redo and validate committed results.
- [x] Cooking produces deterministic runtime mesh artifacts without mutating the authoring model.
- [x] Repeated cooks produce byte-equivalent vertices and indices.
- [x] Valid authoring topology that cooks to deliberately degenerate triangles is rejected at the runtime artifact boundary.
- [x] Topology property, operation, persistence, and cook regression tests cover valid and invalid meshes.

Exit criterion: stable editable topology survives validated operations and deterministically cooks valid runtime geometry while rejecting deliberate degeneracy.

## Phase 9 — Polygon modeling UX

- [x] MeshDocumentWorkspace owns object/edit selection without creating a second topology model.
- [x] Transform, extrude, inset, split, knife, bevel, loop cut, merge, dissolve, bridge, fill, triangulate, normal, and duplicate operations use validated transactions.
- [x] Vertex, edge, and face selections retain stable identities across supported edits.
- [x] Full-document undo/redo restores topology, UVs, materials, and modifier state together.
- [x] The bounded modifier stack evaluates non-destructively before deterministic cooking.
- [x] Save/reopen and cook regression tests cover the combined modeling document.

Exit criterion: a modeled prop survives operations, undo/redo, persistence, evaluation, and deterministic cooking without topology corruption.

## Phase 10 — UV and material authoring

- [x] UVs are stored per face corner with explicit seam ownership.
- [x] Planar unwrap, seam-derived islands, deterministic packing, and texel-density measurement share the Assets authoring model.
- [x] Face material slots and complete PBR texture-channel settings persist with the editable mesh document.
- [x] Texture semantic, color-space, mipmap, and reimport controls are validated rather than inferred silently.
- [x] Material preview requests use the production renderer material conversion for sphere and plane previews.
- [x] Save/reopen tests retain UV, material, and reimport data.

Exit criterion: one multi-material prop can be unwrapped, assigned, previewed, persisted, reopened, and cooked from one document.

## Phase 11 — Sculpting

- [x] Inflate, Smooth, and Grab brushes support falloff, symmetry, and masks.
- [x] Sculpt strokes have bounded undo memory, stroke-count, and affected-vertex budgets.
- [x] Viewport updates identify only dirty vertices unless remeshing changes topology.
- [x] Deterministic uniform remeshing enforces hard face and subdivision limits.
- [x] Multiresolution levels are bounded and derived from the authoritative editable mesh.
- [x] Sculpt, undo/redo, remesh, and production-budget regression tests are present.

Exit criterion: sustained sculpt edits remain deterministic and within explicit memory/topology budgets through final cooking.

## Phase 12 — Advanced production systems

- [x] Animation, terrain, foliage, particles, cloth, fluid, and world-streaming descriptors share a versioned project manifest.
- [x] Runtime construction rejects configurations beyond explicit resource and per-frame workload budgets.
- [x] Editor authoring and Player execution consume the same production manifest.
- [x] Player streaming follows the authored primary camera.
- [x] Runtime profiling publishes deterministic operation and peak-resource counters.
- [x] Manifest, runtime, Editor preview, and Player integration tests cover every subsystem.

Exit criterion: every Phase 12 subsystem crosses persistence, Editor authoring, Player runtime, profiling, and automated validation.

## Phase 13 — Runtime audio contract

- [x] RuntimeAudioMixer owns stable voices, named buses, gain/mute state, looping, and completion.
- [x] Spatial voices apply deterministic listener distance attenuation with validated minimum/maximum distances.
- [x] Invalid buses, duplicate IDs, non-finite values, and unsafe gain ranges fail before mixing.
- [x] Editor authoring can audition through the exact runtime mixer contract.
- [x] Player advances the same mixer and exposes mixed bus levels to a platform device adapter.

Exit criterion: authored voices produce deterministic bus-level frames in Editor and Player; platform audio devices consume those frames without owning gameplay state.

## Phase 14 — Runtime UI, localization, and accessibility

- [x] Runtime UI uses a renderer-neutral parent-first widget hierarchy with normalized anchors.
- [x] Pixel layout composes nested anchors deterministically for arbitrary valid viewport sizes.
- [x] Locale lookup has explicit fallback behavior and rejects missing translations.
- [x] Focusable widgets require accessible labels and expose deterministic focus order and semantic actions.
- [x] Editor preview and Player consume the same validated UI scene.

Exit criterion: layout, translated text, focus, accessible labels, and actions agree between Editor preview and Player without backend-owned UI state.

## Phase 15 — Save, replay, and replication state

- [x] Save snapshots contain bounded typed bool, integer, finite-real, and text values ordered by stable keys.
- [x] Canonical `kairo-state 1` serialization is deterministic and atomically saved beneath validated project paths.
- [x] State deltas include the baseline hash and reject application to a different baseline.
- [x] Replay frames require strictly increasing ticks, bounded actions, and recorded state hashes.
- [x] Replay verification reports deterministic simulation divergence.
- [x] Editor diff/save authoring and Player save/load/replay paths share the same public contract.

Exit criterion: save files round-trip exactly, replication deltas cannot target the wrong baseline, and replay divergence is detected deterministically.
