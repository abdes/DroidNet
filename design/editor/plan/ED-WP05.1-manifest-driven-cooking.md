# ED-WP05.1 - Manifest-Driven Cooking

Status: `planned`

## 1. Goal

Make cooking an editor workflow around descriptors and manifests, not manual
cooked binary generation.

## 2. Inputs

- authoring scene JSON
- source assets
- generated procedural descriptors for engine-supported procedural meshes
- import descriptor JSON
- project cook settings
- selected cook scope

## 3. Outputs

- cooker manifest
- cooked asset descriptors and payloads
- `container.index.bin`
- cook diagnostics
- asset catalog refresh
- cooked-root mount refresh

## 4. Cook Scopes

- current scene
- selected asset
- selected folder
- project

## 5. Required UI

- cook command entry points
- cook progress
- cook output summary
- diagnostics list
- recook stale assets action
- open descriptor/manifest action

## 6. Acceptance Criteria

- The V0.1 scene cooks through descriptors/manifests.
- Procedural geometry is represented by schema-valid descriptors.
- Cooked index validates before mount.
- Cook failure is visible and actionable.
- Cooked scene loads in standalone runtime with renderables, camera, sun, and atmosphere.

## 7. Risks

- Import tool discovery may be fragile.
- Cooker schemas may evolve faster than editor generation logic.
- Partial cook failures must not leave the engine mounted to invalid output.
