# KairoEditor — one user action, from inspector to a reversible scene transaction

This is a focused laboratory for [CommandHistory.cppm](../CommandHistory.cppm), [SceneCommands.cppm](../SceneCommands.cppm), [EditorState.cppm](../EditorState.cppm) and [ProjectSession.cppm](../ProjectSession.cppm). Read the [wider authoring-transaction walkthrough](AUTHORING_TRANSACTION_WALKTHROUGH.md) to see how EditorShell, input routing and native GPU presentation fit around this source.

## 1. Separate state from intent

An inspector field is a view onto an authored EngineCore Scene component. It is not the authoritative component, and it does not own a renderer mesh, texture or native GPU buffer. EditorState stores selection, mode and panel/workspace preferences; ProjectSession owns the editable Scene.

When the user drags a Transform widget, EditorShell should produce a semantic action and construct a SetEntityTransformCommand. The command captures both the original local TRS and the requested new TRS. CommandHistory executes the command and keeps the object for later Undo/Redo.

A scene's Entity ID is persistent authored identity. It is neither a KairoECS generational slot, a PhysicsWorld BodyID, nor a renderer picking/mesh handle. Mapping is performed by the host bridges after authored state changes.

## 2. Understand the cursor and history suffix

CommandHistory stores a vector of unique_ptr<EditorCommand> and cursor C. Entries before C are applied; entries from C to the end are available for Redo.

**Worked sequence:** Execute A, B, C gives applied [A,B,C] and cursor=3. Undo twice calls C.Undo and B.Undo, yielding applied [A], redo [B,C], cursor=1. Now Execute D: the redo suffix [B,C] is discarded and the new applied chain is [A,D]. The user must not be allowed to Redo B after the divergent scene edit.

The implementation calls command->Execute before moving the journal cursor. If it throws, the existing undo/redo journal is unchanged; the *command itself* is responsible for rolling back any mutation it performed before throwing. CommandHistory cannot generically restore an unknown partially edited Scene.

**Exercise:** implement a tiny test command that increments an integer and throws. Observe that the history cursor does not advance, and explain why the command should restore the integer before throwing to meet a strong exception guarantee.

## 3. Gesture merging: one drag should undo as one action

SetEntityTransformCommand::TryMerge checks the same project and entity identity, then updates the older command's after-TRS while retaining its original before-TRS. SetReflectedPropertyCommand also compares registry, project, entity, type key and property key before merging the latest value.

This prevents one slider drag from adding hundreds of tiny undo steps. It must not merge changes to different entities or properties, even when two widgets have the same visible label.

**Exercise:** create position values x=0→1→2→3 from one drag. After coalescing, a single Undo should restore x=0, not x=2. Now interleave a change to another entity and prove that it cannot be absorbed by the previous command.

## 4. Why deletion requires a subtree snapshot

Destroying an EngineCore parent removes all descendants. A reversible DeleteEntityCommand must therefore record far more than the root's name: stable Entity IDs, local transforms, enabled/layer/tags, parent references and optional authored component records.

CaptureEntitySubtree walks the complete hierarchy before mutation and saves EntitySnapshot values. RestoreEntitySubtree first allocates all original IDs, then applies saved components and finally restores parenting. This two-pass approach makes the result independent of the ordering of parent and child snapshots.

If restoration throws after creating some nodes, it destroys created entities in reverse order. Because parent deletion can recursively destroy its descendants, the rollback checks Scene::Contains before each cleanup step.

**Component coverage repair:** [SceneEntityData.cppm](../SceneEntityData.cppm) centralizes capture, validation, application and copy-role policy. Undo now retains audio emitters/listeners; duplicate and clipboard copies retain their data but clear the unique primary camera/listener flags. Scene fragments also retain imported scene instances. The new regression tests cover these cases; exact-head execution is not yet verified.

**Exercise:** build root→child with different camera/light/mesh/physics components, delete root, Undo, then compare all stable IDs and component values. Repeat with audio emitters and a primary listener to verify the repaired path.

## 5. Reparenting can preserve local or world transform—but not both automatically

Scene::SetParent retains a child's local transform. Consequently, its world position changes when it receives a new parent with a different transform.

SetEntityParentCommand implements a separate authoring policy: compute the child's old world TRS, compute its relative local transform under the target parent, change parent and set the newly derived local values. The operation records before/after parent and local TRS for Undo. A zero-scale parent is rejected because mapping world coordinates back into its local coordinates would require division by zero.

For a simplified translation-only example, child at world x=7 is reparented beneath a parent at world x=3, so the new child local x becomes 4. Its world position remains x=7. With rotated, nonuniformly scaled parents, pure TRS composition cannot represent all possible shear, so inspect the implementation's convention before claiming matrix-exact world preservation.

## 6. Reflection is a schema boundary

SetReflectedPropertyCommand stores the stable type/property keys and typed PropertyValue before/after data. It invokes ReflectionRegistry::Write and then EngineCore component validation. If a range check or invariant fails, Apply attempts to restore the captured before value.

A field being visible in an inspector does not make arbitrary memory a valid reflection target. ProjectSession, EngineCore and the reflection registry must agree about concrete object type and lifetime. A model-generated property request also does not bypass host authorization or the undo path.

**Exercise:** edit a numeric property within bounds, Undo and Redo, then enter a value that passes basic type checking but violates a component-level cross-field invariant. Confirm both data and journal cursor remain consistent.

## 7. What tests should prove

Read [tests/EditorTests.cpp](../tests/EditorTests.cpp), the scene-command tests indexed in [SOURCE_READING_ORDER.md](SOURCE_READING_ORDER.md), and EngineCore's Scene tests. Verify cursor movement, divergence invalidating redo, bounded-history eviction, merge scope, exception rollback, subtree restoration, stable ID behavior, audio snapshot coverage, noninvertible reparenting, and reflection invariant recovery.

Model-only tests prove command semantics, not native ImGui viewport, graphics-picking or save/recovery behavior. Keep runtime backend tests separate and record exact source SHA, compiler and host.

*The implementation and regression tests were updated on the cleanup branch. Native execution is not established by this document.*
