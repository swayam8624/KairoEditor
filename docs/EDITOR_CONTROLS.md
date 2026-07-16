# Editor Controls

KairoEditor uses one action vocabulary across macOS and other desktop systems:
`Cmd` means `Cmd` on macOS and `Ctrl` elsewhere. Shortcuts are intentionally
processed only when the relevant editor surface owns focus, so typing in a code
or graph text field does not unexpectedly move or delete scene content.

## Global

| Action | Shortcut |
| --- | --- |
| Save active document or scene | `Cmd+S` |
| Save all | `Cmd+Option+S` |
| New typed document | `Cmd+N` |
| Undo / redo | `Cmd+Z` / `Cmd+Shift+Z` |
| Run or stop the isolated runtime preview | `F5` |

## Viewport

| Action | Shortcut / gesture |
| --- | --- |
| Select / move / rotate / scale tool | `Q` / `W` / `E` / `R` |
| Blender-style move and scale aliases | `G` / `S` |
| Add cube, plane, UV sphere, or cylinder | `Shift+A` or viewport `+` |
| Delete selected entity | `Delete` or `X` |
| Duplicate selected entity | `Cmd+D` |
| Frame selected entity | `F` |
| Orbit | middle mouse drag |
| Pan | `Shift` + middle mouse drag |
| Zoom | mouse wheel |
| Fly navigation | hold right mouse, then `W`/`A`/`S`/`D`; `Q`/`E` descend/ascend |
| Transform selected entity | choose a transform tool, then left-drag inside the viewport |

`R` is the scale-tool binding used by the current Unreal-style toolbar. `E`
selects rotation. This resolves the Blender `R` versus Unreal `R` conflict
without ambiguous context-sensitive behavior; `G` and `S` remain Blender
aliases for move and scale.

## Graph Canvas

| Action | Shortcut / gesture |
| --- | --- |
| Add a node | `Space`, right click, or graph `+` |
| Pan / zoom | middle mouse drag / mouse wheel |
| Frame all graph nodes | `F` |
| Select nodes | click, modifier click, or marquee |
| Connect pins | drag from an input or output pin |

## Physics Preview

Select an entity and choose **Add Dynamic Box Physics** in Inspector. On Play,
Kairo clones the authored scene, builds a dynamic KairoPhysicsEngine body from
the entity's transform and scale, and draws it in the renderer. The authored
scene never receives the runtime body IDs or simulated transforms. Entities
with only a collider binding become static runtime boxes, which lets a plane
primitive act as a ground surface. The Simulation workspace exposes body,
collider, and contact counts; broadphase bounds can be toggled there.

The current preview deliberately uses boxes derived from local scale. Sphere,
capsule, mesh, joint, cloth, fluid, and particle authoring require persistent
physics component descriptors and are not represented as misleading controls.
