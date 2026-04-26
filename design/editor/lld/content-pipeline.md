# Content Pipeline LLD

Status: `reviewed LLD`

## 1. Purpose

Define how the editor manages project assets, asset references, import
descriptors, cooking, cooked indexes, and cooked-root mounting.

## 2. Current Baseline

The content browser can show project folders and cooked assets. The editor can
cook simple scene content and mount loose cooked roots. The workflow is still
not a full asset pipeline because descriptor/manifest state, validation, and
asset reference picking are incomplete.

## 3. Target Asset Products

The editor distinguishes:

- source files
- sidecar import descriptor JSON files
- generated procedural descriptors
- scene source/import descriptors
- cooker manifests
- cooked asset descriptors
- cooked payload files
- loose cooked indexes
- missing or stale outputs

Engine descriptor schemas are the preferred persisted representation when they
fit editor authoring. If content authoring needs fields that the engine schema
does not support, the content-pipeline work must decide whether to augment the
engine schema or introduce an editor-side schema before implementation.

## 4. Asset Reference Model

Scene components should use an asset reference value that can represent:

| Kind | Example | Meaning |
| --- | --- | --- |
| `SourceAsset` | `project://Content/Meshes/Cube.gltf` | User-authored source file. |
| `ImportDescriptor` | `project://Content/Meshes/Cube.ogeo.import.json` | Stable cooker input. |
| `GeneratedDescriptor` | `generated://Scene/NewScene1/GeoCube` | Editor-generated procedural input. |
| `CookedVirtualPath` | `asset:///.cooked/Geometry/GeoCube.ogeo` | Runtime-loadable cooked asset. |
| `Missing` | stored unresolved URI | Reference cannot currently resolve. |

The picker may show cooked assets, but the authoring model should preserve the
best available intent.

## 5. Cooking Flow

```text
Authoring scene/content
  -> validate references and descriptors
  -> generate/update descriptors
  -> generate cooker manifest
  -> invoke supported cooker API or import tool
  -> validate loose cooked index and descriptors
  -> refresh asset catalog
  -> refresh engine cooked roots
  -> publish diagnostics
```

## 6. Content Browser Views

Required views:

- Project source tree
- Asset catalog by type
- Descriptor/manifest view
- Cooked output tree
- Import/cook diagnostics
- Asset reference picker

The content browser must make source/cooked/descriptor mode visible. Users
should not have to infer which tree they are editing from path strings.

## 7. Cook Scopes

| Scope | Behavior |
| --- | --- |
| Scene | Generate descriptors/manifests for the open scene and cook dependencies. |
| Asset | Cook selected source/descriptor and dependents. |
| Folder | Cook assets under a project root subtree. |
| Project | Cook all configured project content roots and scenes. |

## 8. Mount Rules

- Mount only roots with a readable valid `container.index.bin`.
- Prefer the project `.cooked` root when the index is root-level.
- Support per-mount indexes under `.cooked/<MountPoint>` when present.
- Missing or invalid indexes produce validation results.
- Mount refresh must not hide existing engine failures.

## 9. Failure Modes

| Failure | Required behavior |
| --- | --- |
| Import tool missing | Actionable diagnostic with searched paths. |
| Descriptor invalid | Validation issue linked to descriptor. |
| Cook process failed | Diagnostics include exit code, command, stdout/stderr summary. |
| Index invalid | Mount blocked and validation issue shown. |
| Asset stale | Warning with recook action. |
| Reference unresolved | Component validation issue and picker affordance. |

## 10. Exit Gate

ED-M05 closes when the editor can generate descriptors/manifests for the V0.1
scene, cook it, validate the loose cooked output, refresh the asset catalog,
mount the cooked root, and load the cooked scene in standalone runtime.
