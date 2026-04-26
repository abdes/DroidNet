# Viewport And Tools LLD

Status: `reviewed LLD`

## 1. Purpose

Define viewport behavior, editor camera, tools, overlays, and multi-view
requirements.

## 2. Current Baseline

The editor can create live engine views, attach WinUI surfaces, render Vortex
output, resize surfaces, and frame the initial scene. Camera navigation and
multi-pane layouts exist in early form.

## 3. Target Viewport Features

- perspective and orthographic views
- one/two/four pane layouts
- editor camera navigation
- frame selected/all
- object selection
- transform gizmos
- grid and origin overlays
- view modes and shading modes
- engine diagnostics overlay
- per-view clear color and render settings
- optional view-through-scene-camera mode

## 4. View Ownership

| State | Owner |
| --- | --- |
| Editor camera pose | Document viewport state |
| Scene camera component | Authoring scene |
| Surface lease | `Oxygen.Editor.Runtime` |
| Engine view ID | Runtime/interop |
| Viewport layout | Scene document/workspace layout |
| UI overlays | World editor UI |
| GPU diagnostics | Engine diagnostics |

## 5. Viewport Rules

1. The editor viewport uses an editor-owned camera unless explicitly viewing
   through a scene camera.
2. Editor camera state is document/view state, not scene runtime camera data.
3. Rendering and presentation use the engine renderer and composition system.
4. UI overlays are editor UI; GPU debug/engine overlays are engine diagnostics.
5. Multi-view must support independent surfaces without single-target asserts.
6. Resizing must be debounced at the UI layer and idempotent in runtime sync.

## 6. Tool Phases

| Phase | Features |
| --- | --- |
| V0.1 | camera navigation, frame all, view presets, multi-view stability. |
| Selection | picking, hierarchy selection sync, selection outline/overlay. |
| Transform | translate/rotate/scale gizmos with command-based mutation. |
| Diagnostics | view modes, render stats, engine debug console integration. |

## 7. Surface/Composition Validation

The viewport validation matrix must cover:

- one pane open scene
- two pane split
- four pane split
- resize while engine is running
- close/reopen document
- engine restart/resync
- detach/destroy surface

Each case must verify that every visible viewport presents to its own surface
and that hidden viewports do not keep stale composition submissions alive.

## 8. Exit Gate

ED-M07 closes when users can navigate, frame, select, and edit object
transforms in the live viewport with correct undo/redo and no surface
composition aborts.
