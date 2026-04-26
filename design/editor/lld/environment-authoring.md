# Environment Authoring LLD

Status: `reviewed LLD`

## 1. Purpose

Define scene-level environment and renderer authoring in the editor.

## 2. Current Baseline

The live engine can create default atmosphere and post-process state. The
editor has light components and engine renderer settings, but no coherent
scene-owned environment UI/model.

## 3. Target Authoring Model

Scene environment state should include:

- atmosphere enabled/disabled
- sun light binding
- sky/atmosphere parameters
- height fog and local fog defaults
- post-process volume defaults
- exposure mode
- exposure compensation
- tone mapping mode
- background/clear behavior
- preview renderer feature toggles that are scene-owned

## 4. Data Placement

| Data | Placement |
| --- | --- |
| Scene environment | Scene authoring data |
| Active scene camera | Scene authoring data plus document viewport selection |
| Project renderer defaults | Project settings, per [settings-architecture.md](./settings-architecture.md) |
| Local machine renderer/debug settings | Editor settings JSON, per [settings-architecture.md](./settings-architecture.md) |
| Runtime-only diagnostics | Engine diagnostics UI/logs |

## 5. V0.1 Defaults

New scenes should start with:

- atmosphere enabled
- one directional light marked as sun/environment contribution, if the user
  selects a lit starter scene
- auto exposure enabled
- ACES tone mapping
- camera FOV stored in degrees and cooked using engine schema expectations
- visible default procedural material for V0.1 procedural meshes

Defaults must be explicit in authoring data or deterministic scene template
generation. They must not depend on hidden runtime fallback behavior.

Environment settings are scene-owned unless this LLD explicitly classifies a
field as project or editor scope. Scope changes must follow the settings
architecture LLD.

## 6. Inspector Surface

Environment editor groups:

- Sky and atmosphere
- Sun binding
- Fog
- Exposure
- Tone mapping
- Preview renderer options

The scene environment editor is scene-level. It is not tied to selected nodes,
though it may reference a directional light component as the sun.

## 7. Live Sync

Environment live sync follows:

```text
environment authoring state
  -> validation
  -> SceneEngineSync environment adapter
  -> interop environment API
  -> renderer/post-process update
```

If engine APIs are missing, the correct response is to add stable engine
capabilities, not to duplicate renderer behavior in the editor.

## 8. Cooking

Cooked scene output must include the environment settings needed for standalone
runtime parity. The standalone scene must not rely on editor-only defaults.

## 9. Exit Gate

ED-M06 closes when atmosphere, sun binding, exposure, and tone mapping can be
edited, saved, live-synced, cooked, reopened, and loaded in standalone runtime.
