# ED-WP06.1 - Settings Architecture And Editors

Status: `planned`

## 1. Goal

Implement the V0.1 settings architecture so editor, project, workspace,
runtime, and scene settings are owned, validated, persisted, and edited through
the correct contracts.

## 2. Inputs

- existing editor configuration services
- project metadata and project layout services
- workspace layout/recent document state
- scene environment authoring model
- runtime engine configuration and readiness state

## 3. Required Scope Decisions

| Setting Area | Scope |
| --- | --- |
| Theme, UI preferences, local debug defaults | Editor |
| Content roots, cook policy, default renderer preset | Project |
| Docking layout, open documents, recent projects | Workspace |
| Atmosphere, exposure, tone mapping, active camera | Scene |
| Engine readiness, mounted cooked roots, surface leases | Runtime session |
| Temporary log/runtime overrides | Diagnostic override |

## 4. Required UI

- project settings editor for V0.1 project/cook/runtime defaults
- scene environment settings editor
- runtime diagnostics panel for effective engine settings and overrides
- validation presentation for invalid settings

Editor-wide preferences UI may be minimal in V0.1, but any setting exposed
there must still follow the durable setting contract.

## 5. Mutation Contract

- Scene settings mutate through scene commands and support undo/redo.
- Project settings mutate through project settings services and project save
  state.
- Workspace settings mutate through shell/workspace services.
- Runtime session state is not persisted as settings.
- Diagnostic overrides are visible and temporary.

## 6. Acceptance Criteria

- V0.1 settings have documented owner, scope, storage, default, validation, and
  mutation path.
- Scene environment settings save, reload, live-sync, cook, and validate.
- Project settings drive content/cook/runtime defaults without duplicating
  runtime policy.
- Runtime settings apply to the embedded engine or produce actionable
  diagnostics.
- No new durable setting is introduced as a command-line or environment-only
  path.

## 7. Risks

- Existing settings paths may mix editor, project, and runtime concerns.
- Some renderer settings may need engine API support before they can be edited
  cleanly.
- Migration of existing user settings must not break startup or project
  loading.
