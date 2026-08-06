# Phase 4 — Production Editor Workflows

Phase 4 moves project and scene workflow state out of immediate-mode UI code and into deterministic services that can be tested headlessly.

## Asset browser

- Project-folder, type and case-insensitive search filtering.
- Current, changed, missing, unimported and generated status.
- Direct dependent counts, transitive dependency inspection and safe-delete plans.
- Stable path/ID ordering independent of registry hash iteration.

## Scene authoring

- Single, toggle, range, marquee and select-all behaviour with active/anchor state.
- Selection pruning after scene mutations.
- Transactional scene fragment capture and paste.
- Basic prefab templates with validated per-entity name, transform and enabled overrides.
- Complete hierarchy and authored runtime component copying.

## Project lifecycle

- Bounded recent-project persistence with atomic publication.
- Missing-project pruning.
- Atomic project cloning for Save Project As and project templates.
- Descriptor validation before and after cloning.

## Diagnostics

Diagnostics are grouped by producer, sorted by severity and linked to project files, assets, entities/components, graph nodes/pins or source locations. Recompilation replaces only the owning producer's results instead of clearing unrelated importer, scene or packaging errors.

## Acceptance gates

- Asset source changes appear without relying on UI-local caches.
- Referenced assets cannot be presented as safely deletable.
- Scene copy/paste and prefab instantiation preserve hierarchy and components.
- Failed validation leaves the destination scene unchanged.
- Recent projects and cloned templates survive process restart.
- All services compile and test without Vulkan, GLFW or Dear ImGui.
