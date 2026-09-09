# Oxygen.Editor.Schemas

Shared property identities, descriptors, edit snapshots, bindings, and schema
overlay utilities for editor authoring features.

## Schema ownership

Engine JSON schemas define the field set, types, and validation constraints.
Sibling editor overlays add presentation metadata in `x-editor-*` annotations.
The editor consumes those engine schemas rather than maintaining a second
copy of their constraints.

| File | Owner | Purpose |
| --- | --- | --- |
| `*.schema.json` | Engine | Authored fields, types, constraints. |
| `*.editor.schema.json` | Editor | Labels, groups, widget choices, and other annotations. |

[EditorSchemaCatalog](src/EditorSchemaCatalog.cs) loads and evaluates schemas.
[EditorSchemaOverlay](src/EditorSchemaOverlay.cs) extracts annotations, lints
the annotation namespace, and checks coverage of engine authoring paths.
The [project file](src/Oxygen.Editor.Schemas.csproj) copies schemas from the
engine source tree into the editor output instead of duplicating them in
source control. Cooked binary serialization remains owned by the engine cooker.

[EditorSchemaOverlayTests](tests/EditorSchemaOverlayTests.cs) checks repository
overlays for permitted keywords, sibling schema references, authoring coverage,
and exclusion from the cooker's embedded schema blocks. Material validation
also checks agreement between engine and merged schemas. These checks protect
the engine's validation authority while allowing editor-specific presentation.

## Shared editing mechanisms

| Type | Responsibility |
| --- | --- |
| `PropertyId<T>` | Typed property identity. |
| `PropertyDescriptor<T>` | Model reader/writer, validator, annotation, and engine command key. |
| `PropertyEdit` | Descriptor-addressed value map. |
| `PropertySnapshot` / `PropertyOp` | Per-target before/after values and the target identity set. |
| `PropertyApply` | Shared apply path for forward edits and history restoration. |
| `PropertyBinding<T>` | Multi-selection values, including mixed-value state. |
| `CommitGroupController` | Session grouping for continuous edits. |

[PropertyApply](src/PropertyApply.cs) receives an `IPropertyTarget` resolver
instead of depending on world models or the C++/CLI facade. It applies the
selected snapshot to the model and calls the supplied sync function. Reusing
the same apply logic for forward and inverse operations reduces the chance of
undo taking a different mutation path.

[PropertyOp](src/PropertyOp.cs) captures per-target values, and
[CommitGroupController](src/CommitGroupController.cs) retains the session's
original target set and before snapshot. Selection changes during an edit
must not redirect that operation to a different set of nodes.

## Feature integration and limits

The feature command layer owns validation orchestration, history registration,
dirty state, operation results, and concrete runtime adapters. A descriptor or
a call to `PropertyApply` alone does not supply those integrations.

The current
[scene command pipeline](../Oxygen.Editor.WorldEditor/src/Documents/Commands/SceneDocumentCommandService.PropertyPipeline.cs)
uses the shared operation/session mechanisms for transforms. Other component
property edits adapt into existing scene commands so their undo/redo, sun
exclusivity, and specialized asset/environment behavior remain centralized.
Material editing uses descriptors and validation; its missing history
integration is tracked in [issue #9](https://github.com/abdes/DroidNet/issues/9).

[SceneDocumentCommandServiceTests](../Oxygen.Editor.WorldEditor/tests/SceneExplorer/SceneDocumentCommandServiceTests.cs)
covers session grouping, preservation of original targets, no-op edits,
validation failures, and undo/redo behavior.
[PropertyPipelineContractTests](tests/PropertyPipelineContractTests.cs) covers
the shared mechanisms. This documents test coverage in source, not a current
execution result.

## Design and dependencies

- [Property pipeline LLD](../../design/editor/lld/property-pipeline-redesign.md).
- [Documents and commands LLD](../../design/editor/lld/documents-and-commands.md).
- [Implementation status](../../design/editor/IMPLEMENTATION_STATUS.md).
- `JsonSchema.Net` provides schema evaluation; package versions are centrally
  managed.
