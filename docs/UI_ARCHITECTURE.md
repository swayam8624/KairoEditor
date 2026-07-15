# KairoEditor UI Architecture

## Decision

KairoEditor uses the Dear ImGui docking branch as its current native desktop
shell and interaction backend. `Kairo.Editor.UI` provides engine-owned visual
tokens and semantic widgets on top of it, while Kairo-owned draw-list widgets
provide the visual and behavioral identity for the graph canvas, viewport
chrome, asset browser, timeline, inspectors, and other domain-specific tools.

ImGui is not the editor data model. It may translate input and draw state, but
it must not own project identity, scene data, document topology, validation,
serialization, compilation, undo/redo, or asset metadata.

## Why this path

- KairoRenderer already owns the Vulkan window, device, render pass, command
  recording, and presentation lifecycle. ImGui records tooling UI into that
  validated frame instead of forcing a second graphics owner.
- The C++23 engine can expose editor behavior directly without an FFI bridge,
  state mirroring, or another language runtime.
- Docking, keyboard focus, text input, tables, and multi-window tooling are
  available now, while custom draw lists leave the high-value surfaces under
  Kairo's visual control.
- The existing backend-neutral editor modules are independently testable and
  remain reusable by a future native KairoUI implementation.

## Alternatives considered

### Flutter

Flutter is capable of a polished application shell, but a production Kairo
integration needs Dart FFI, strict authoritative-state ownership, and zero-copy
composition of a renderer-owned Metal/Vulkan image. It remains a possible host
experiment, not the primary editor path.

### Qt 6

Qt Widgets provide mature desktop controls, accessibility, and OS integration.
They also introduce another application/window/render-surface owner around the
existing Vulkan runtime. That cost is not justified while the editor's most
important widgets are custom engine tools rather than standard forms.

### Tauri or other web shells

HTML and CSS offer a high visual ceiling, but native low-latency Vulkan viewport
composition, input routing, packaging, and dual frontend/backend state create a
larger integration surface than the editor requires.

### egui

egui is a strong immediate-mode choice for Rust projects. Kairo's runtime and
authoring stack are C++, so adopting it would add a language boundary without
solving a problem Dear ImGui leaves unsolved.

## Dependency rule

Only native-shell modules may import Dear ImGui:

```text
KairoEditorImGui
    Kairo.Editor.UI
    EditorTheme
    ImGuiRuntime
    EditorShell
```

The reusable target remains UI-independent:

```text
KairoEditor
    ProjectSession
    EditorState
    CommandHistory
    AuthoringDocument
    ProjectDocuments
    GraphCanvas
    DocumentProjection
    AuthoringWorkspaceState
```

No type in `KairoEditor` may expose `ImVec*`, `ImGuiID`, widget flags, or an
ImGui lifetime. Screen coordinates enter reusable graph code through KairoMath
vectors and explicit viewport transforms.

## Visual implementation policy

- Use docking for panel organization and native ImGui input/focus handling.
- Use the Kairo theme, curated fonts, consistent spacing, restrained color, and
  custom draw-list surfaces rather than default demo widgets.
- Keep the viewport and authoring canvas dominant; operational panels should be
  dense, predictable, and optimized for repeated work.
- Use stable dimensions for nodes, pins, toolbars, and controls so labels and
  interaction states do not shift the layout.
- Use icons for familiar tools once the licensed icon asset pipeline lands;
  text buttons remain only for explicit commands in the current dependency-free
  shell.
- Do not claim unavailable timeline, compiler, material, or runtime behavior.
  A visible control must execute a real command or clearly represent current
  read-only state.

## Semantic KairoUI boundary

`Kairo.Editor.UI` is not a second window toolkit. It is a small semantic
surface over the current implementation: design tokens, action buttons,
toolbar controls, search fields, section headings, muted metadata, and status
labels. A panel may use low-level ImGui only for primitives that do not yet
have a KairoUI counterpart; new ordinary controls should be added to this
semantic layer first.

The boundary prevents raw backend styling from becoming editor policy:

```text
Editor feature
        |
        v
Kairo.Editor.UI semantic control
        |
        v
Dear ImGui implementation today
```

## Future toolkit migration

The long-term native toolkit may replace docking, layout, text rendering,
widgets, animation, and input routing. It should consume the same editor APIs:

```text
KairoEditor domain state
        |
        +-- Kairo.Editor.UI -> KairoEditorImGui (current)
        |
        +-- alternative Kairo.Editor.UI implementation (future)
```

Migration is permitted when KairoUI can match the current shell's docking,
keyboard/text input, accessibility target, multi-monitor behavior, clipping,
large-list performance, and renderer integration. The migration must not fork
project formats or create a second scene/document model.
