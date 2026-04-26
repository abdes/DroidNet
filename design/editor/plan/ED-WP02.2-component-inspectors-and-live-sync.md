# ED-WP02.2 - Component Inspectors And Live Sync

Status: `planned`

## 1. Goal

Complete V0.1 component editors and live sync coverage for scene authoring.

## 2. V0.1 Components

- Transform
- Geometry
- PerspectiveCamera
- OrthographicCamera
- DirectionalLight
- scene Environment
- basic material/rendering override

PointLight and SpotLight are domain-capable but are not V0.1-complete until
engine sync, cooking, and validation behavior are confirmed.

## 3. Required Editor Fields

| Component | Fields |
| --- | --- |
| PerspectiveCamera | FOV degrees, near, far, aspect policy, active scene camera. |
| OrthographicCamera | size, near, far, active scene camera. |
| DirectionalLight | color, intensity lux, angular size, casts shadows, environment contribution, sun flag. |
| Geometry | asset reference, generated/source/cooked identity, material override entry. |
| Environment | atmosphere enabled, sun binding, auto exposure, exposure compensation, ACES tone mapping. |

## 4. Live Sync Requirements

Each editor mutation must:

1. mutate authoring state through a command
2. mark the document dirty
3. validate the changed component
4. sync to the embedded engine when the engine is available
5. preserve authoring state when sync fails
6. surface sync failure as a validation/diagnostic result

## 5. Likely Touch Points

- `SceneNodeEditorViewModel`
- `SceneNodeDetailsViewModel`
- `TransformViewModel`
- `GeometryViewModel`
- new camera/light/environment view models and views
- `SceneEngineSync`
- `Oxygen.Editor.World` component models

## 6. Acceptance Criteria

- Selecting a camera node shows a camera editor.
- Selecting a directional light node shows a light editor.
- Scene environment can be edited without selecting a node.
- Changing camera/light/environment values updates live preview.
- Save/reopen preserves values.
- Cooked scene contains values needed by standalone runtime.
- Invalid values are rejected or reported consistently.

## 7. Risks

- Engine APIs for environment settings may be incomplete.
- Some renderer settings may be project/editor settings, not scene settings.
- Material overrides need a minimal asset reference model before becoming robust.
