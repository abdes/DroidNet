# Settings Architecture LLD

Status: `draft LLD`

## 1. Purpose

Define durable editor settings as an owned architecture, not incidental feature
state. This LLD covers editor, project, workspace, runtime, and scene settings,
their precedence, persistence, validation, and editing model.

## 2. Current Baseline

The editor already has settings-related behavior:

- editor launch and engine configuration values
- native runtime discovery for engine DLL loading
- workspace layout restoration
- project roots and cooked root mounting
- scene environment and render defaults

These behaviors are not yet governed by one settings contract. Some values are
durable user intent, some are project policy, some are scene authoring data,
and some are runtime bootstrap details. ED-M06 must separate those products
before exposing more settings UI.

## 3. Settings Scopes

| Scope | Owner | Storage | Examples |
| --- | --- | --- | --- |
| Editor | `Oxygen.Editor` / config services | user-local JSON | theme, default engine config, debug console preference |
| Project | `Oxygen.Editor.Projects` | project metadata/settings file | content roots, cook policy, default renderer preset |
| Workspace | `Oxygen.Editor` / editor data | user-local workspace state | windows, docking layout, recent documents |
| Scene | `Oxygen.Editor.World` | scene authoring data or engine descriptors when suitable | environment, active camera, tone mapping intent |
| Runtime session | `Oxygen.Editor.Runtime` | in-memory only | engine readiness, mounted cooked roots, surface leases |
| Diagnostic override | app launch/debug infrastructure | command line or environment | temporary log level, one-off runtime path override |

Settings belong to the narrowest scope that matches the user intent. Do not
store scene-owned values in editor settings, project policy in runtime state,
or local machine preferences in project files.

## 4. Precedence

Effective settings are resolved in this order:

1. Explicit session diagnostic override
2. Scene setting, for scene-owned rendering/authoring intent
3. Project setting, for project policy and defaults
4. Editor setting, for user/workstation defaults
5. Built-in default

Workspace settings are restored by workspace identity rather than merged into
scene/project/runtime precedence. They must not override authored scene values
or project policy.

Diagnostic overrides are temporary and must be visible as overrides. They must
not silently mutate durable settings.

## 5. Durable Setting Contract

Every durable setting requires:

- stable key or schema field
- owning project/module
- storage file and scope
- default value
- validation rule
- migration behavior
- UI owner, if editable
- command/service path for mutation
- live runtime application behavior, if applicable

Adding a new durable setting without this contract is a design violation.

## 6. Editing Model

Settings editors do not write files directly. They call the owning settings
service or scene command:

```text
settings UI
  -> typed settings view model
  -> command or settings service mutation
  -> validation
  -> dirty/pending state
  -> persistence
  -> live runtime apply where applicable
```

Scene settings use scene commands and participate in scene dirty state and
undo/redo. Project settings use project dirty/save state. Editor settings use
user-local settings persistence and should not dirty the open scene.

## 7. Runtime Application

Runtime-facing settings are applied through `Oxygen.Editor.Runtime`:

- engine launch configuration
- target frame rate
- debug overlay/console enablement
- renderer backend/preset selection when supported
- mounted cooked roots derived from project/cook state

Runtime services apply effective settings; they do not decide where a setting
belongs or create hidden fallback policy.

Process-local native runtime DLL discovery is bootstrap infrastructure. It is
configured by development/build layout, not by product settings UI.

## 8. Scene And Descriptor Policy

Scene environment and render intent should use engine scene/descriptor schemas
when those schemas fit editor authoring. If the editor needs additional
authoring-only metadata, the owning LLD must decide whether to:

- augment the engine schema
- store editor metadata alongside the descriptor
- introduce a separate editor schema

Small mismatches should prefer engine schema augmentation. Structural
mismatches require design review before implementation.

## 9. Validation

Settings validation produces structured validation results:

| Failure | Scope | Required behavior |
| --- | --- | --- |
| Invalid project root | Project | Block dependent cook/mount actions and show fix action. |
| Missing engine runtime path | Runtime/editor | Show startup diagnostic with searched locations. |
| Unsupported renderer preset | Project/runtime | Keep saved value, select safe effective value, report mismatch. |
| Invalid scene exposure/tone mapping | Scene | Mark scene invalid for sync/cook until fixed. |
| Conflicting override | Session | Show active override and durable value separately. |

## 10. Exit Gate

ED-M06 settings architecture is complete when:

- all V0.1 settings have explicit scope, owner, storage, defaults, validation,
  and mutation path
- settings UI edits use commands/services rather than direct file writes
- scene environment settings participate in scene dirty state and undo/redo
- project settings participate in project save state
- runtime settings apply to the embedded engine and report failures
- validation results identify invalid settings before sync, cook, or launch
