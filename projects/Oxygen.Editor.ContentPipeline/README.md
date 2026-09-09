# Oxygen.Editor.ContentPipeline

Editor-side orchestration for producing runtime content from authoring data.

## Ownership

The editor chooses the authoring workflow and requested cook scope. Project
services supply project/root policy, and this module coordinates descriptors,
manifests, native tool execution, output inspection, and validation.

[Oxygen.Managed.Assets](../Oxygen.Managed.Assets/README.md) owns reusable
managed asset primitives. The native cooker owns cooked product generation.
Runtime services and their consumers own mounting and live projection. A
successful cook does not itself prove mounting, presentation, or standalone
runtime parity.

The ownership contracts are defined in the
[content pipeline LLD](../../design/editor/lld/content-pipeline.md) and
[editor architecture](../../design/editor/ARCHITECTURE.md).

## Explicit workflow stages

[ContentPipelineService](src/ContentPipelineService.cs) exposes current-scene,
asset, folder, and project cook operations. The stages keep failures attributable
to the operation that produced them.

| Stage | Implementation | Purpose |
| --- | --- | --- |
| Scope | Project context and cook-scope provider | Identify authored inputs and destination roots. |
| Descriptors | [SceneDescriptorGenerator](src/SceneDescriptorGenerator.cs), [ProceduralGeometryDescriptorService](src/ProceduralGeometryDescriptorService.cs) | Translate authoring data and referenced generated assets into native descriptors. |
| Manifest | [ContentImportManifestBuilder](src/ContentImportManifestBuilder.cs), [ContentImportManifestValidator](src/ContentImportManifestValidator.cs) | Build and validate the bounded import request. |
| Execution | [IEngineContentPipelineApi](src/IEngineContentPipelineApi.cs) | Isolate orchestration from the native execution adapter. |
| Inspection and validation | `InspectLooseCookedRootAsync`, `ValidateLooseCookedRootAsync` | Check generated output and return structured results. |

The manifest execution path stops on manifest, import, or inspection failure,
accumulates stage diagnostics, and includes output validation in the final
result. Preserve these stages when adding cook targets; successful process exit
alone is insufficient evidence of valid output.

## Tool process boundary

[ImportToolContentPipelineApi](src/ImportToolContentPipelineApi.cs) writes a
manifest and invokes the installed native ImportTool through
[IContentPipelineProcessRunner](src/IContentPipelineProcessRunner.cs).

[ContentPipelineProcessRunner](src/ContentPipelineProcessRunner.cs) uses
`ProcessStartInfo.ArgumentList`, disables shell execution, and reads standard
output and standard error concurrently. Structured arguments preserve paths
and values as arguments; concurrent draining avoids serial pipe-read stalls.

Cancellation currently does not guarantee native process termination; that
lifetime correction is tracked in
[issue #8](https://github.com/abdes/DroidNet/issues/8).
Consolidating procedural semantics between live preview and cooking is tracked
in [issue #11](https://github.com/abdes/DroidNet/issues/11).

## Verification boundaries

[ContentPipelineServiceTests](tests/ContentPipelineServiceTests.cs),
[SceneDescriptorGeneratorTests](tests/SceneDescriptorGeneratorTests.cs), and
[ImportToolContentPipelineApiTests](tests/ImportToolContentPipelineApiTests.cs)
provide focused orchestration, descriptor, and adapter coverage. The adapter
tests substitute the process runner, so they do not establish the lifetime
behavior of a real cancelled tool.

Keep source-level tests, real tool execution, cooked-root validation, and
standalone/visual validation distinct. Current workflow evidence belongs in
[IMPLEMENTATION_STATUS.md](../../design/editor/IMPLEMENTATION_STATUS.md);
test files alone do not close those gates.
