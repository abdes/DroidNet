# Property Inspector LLD

Status: `reviewed LLD`

## 1. Purpose

Define a scalable inspector model for scene nodes, components, scene settings,
project settings, and engine/runtime settings. Settings editors must follow
[settings-architecture.md](./settings-architecture.md).

## 2. Current Baseline

The inspector currently has:

- `IPropertyEditor<SceneNode>`
- `ComponentPropertyEditor`
- `MultiSelectionDetails`
- `TransformViewModel`
- `GeometryViewModel`
- component add/remove menus

The registry is static and incomplete. Transform and geometry have editors.
Camera and light components can be added, but do not have complete property
editors.

## 3. Target Model

The inspector uses descriptors:

```text
EditorDescriptor
  -> target type
  -> editor factory
  -> selection policy
  -> editable fields
  -> command bindings
  -> validation provider
  -> optional asset/reference picker
```

Descriptors are registered by feature area, not by manually editing one static
dictionary in a view model.

## 4. Selection Policies

| Policy | Meaning |
| --- | --- |
| `SingleOnly` | Editor appears for exactly one selected node/component. |
| `CommonComponent` | Editor appears when every selected node has the component. |
| `OptionalCommon` | Editor can show add/apply behavior for missing components. |
| `SceneOnly` | Editor appears for scene settings, not selected nodes. |

Mixed values are represented explicitly. Empty strings or zero values must not
be used as fake mixed-value sentinels.

## 5. V0.1 Editors

| Editor | Fields | Notes |
| --- | --- | --- |
| Transform | position, rotation, scale | Must use commands and support multi-select. |
| Geometry | asset reference, LOD/submesh override summary, material override entry points | Must stop treating cooked URI text as the only authoring reference. |
| Perspective camera | FOV degrees, near/far, aspect policy, active scene camera flag | FOV stored in authoring degrees; cooking converts if needed. |
| Orthographic camera | orthographic size, near/far, active scene camera flag | Required for scene camera completeness and view preset parity. |
| Directional light | color, intensity lux, angular size, casts shadows, sun/environment contribution | Defaults must create visible sun/sky. |
| Environment | atmosphere, sun binding, exposure mode, exposure compensation, tone mapping | Scene-level editor. |
| Rendering/material override | visible, cast/receive shadows, material reference | V0.1 can be scoped, but must define reference semantics. |

Project, editor, workspace, and runtime settings editors are registered through
the same inspector infrastructure only after their settings scope and owner are
defined in the settings architecture.

## 6. Command Binding

Property editors do not call interop. They issue authoring commands:

```text
UI edit
  -> field view model
  -> scene command
  -> document command service
  -> sync/validation side effects
```

Slider drags and text edits should batch:

- preview while dragging if affordable
- commit one undo record when interaction completes
- validate committed values

## 7. Validation Presentation

Editors display:

- field-level errors
- component-level warnings
- missing asset reference status
- unsupported runtime/cook status
- quick actions where available

## 8. Failure Modes

| Failure | Required behavior |
| --- | --- |
| Missing asset | Show unresolved reference and validation result. |
| Engine sync failure | Keep authoring value, show sync diagnostic. |
| Unsupported cook contribution | Show component warning before cooking. |
| Invalid numeric input | Reject or clamp by policy and explain. |
| Multi-selection mismatch | Show mixed state and allow overwrite where safe. |

## 9. Exit Gate

ED-M03 closes when V0.1 editors:

- edit through commands
- support defined selection behavior
- preserve dirty state and undo/redo
- surface validation
- update live preview where applicable
