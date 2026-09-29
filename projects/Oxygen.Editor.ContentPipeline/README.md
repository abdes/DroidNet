# Oxygen.Editor.ContentPipeline

Editor-side orchestration for producing runtime content from authoring data.

Read [workflow stages](#explicit-workflow-stages),
[worker ownership](#tool-process-boundary) and
[freshness and validation](#freshness-and-validation).

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

| Stage                     | Implementation                                                                                                                                 | Purpose                                                                                 |
| ------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Scope                     | Project context and cook-scope provider                                                                                                        | Identify authored inputs and destination roots.                                         |
| Descriptors               | [SceneDescriptorGenerator](src/SceneDescriptorGenerator.cs), [ProceduralGeometryDescriptorService](src/ProceduralGeometryDescriptorService.cs) | Translate authoring data and referenced generated assets into native descriptors.       |
| Manifest                  | [ContentImportManifestBuilder](src/ContentImportManifestBuilder.cs), [ContentImportManifestValidator](src/ContentImportManifestValidator.cs)   | Build and validate the bounded import request.                                          |
| Execution                 | [IEngineContentPipelineApi](src/IEngineContentPipelineApi.cs)                                                                                  | Isolate orchestration from the native execution adapter.                                |
| Inspection and validation | Native Inspector inventory under `CookOutputReadLease`                                                                                         | Verify files once per protected opening and derive structured results from that report. |

The manifest execution path stops on manifest, import, or inspection failure,
accumulates stage diagnostics, and includes output validation in the final
result. Preserve these stages when adding cook targets; successful process exit
alone is insufficient evidence of valid output.

## Tool process boundary

[ImportToolContentPipelineApi](src/ImportToolContentPipelineApi.cs) writes a
manifest and invokes the installed native ImportTool through
[IContentPipelineProcessRunner](src/IContentPipelineProcessRunner.cs).

[ContentPipelineProcessRunner](src/ContentPipelineProcessRunner.cs) passes
structured arguments to the Windows worker, which starts `CreateProcessW` under
an owned Job Object without shell execution. It drains standard output and error
concurrently, preserving argument boundaries and avoiding serial pipe-read stalls.

Cancellation stops the owned worker tree and drains both output readers before
releasing its input and tool leases. Failed termination retains ownership until
the worker drains. Progress and structured warnings/errors flow into the shared
Cooking panel throughout import, validation and publication.

Every import retains the native report's per-source output paths. Those
associations identify source-owned auxiliary files; the native index owns their
sizes and digests. Named texture outputs use the descriptor's `virtual_path` and
participate in the same cooking, reuse and reference-resolution workflow.

## Freshness and validation

Browser badges compare saved inputs and observed producer identity against the
last successful publication. They consume event-driven snapshots, launch no
native processes and hash no cooked payloads. Unknown output availability is
neutral; an observed missing output offers cooking. Filtering and resolving
unchanged rows reuse the snapshot.

Native Content owns complete file-integrity validation. Cooking verifies reused
content and newly produced output before publication. Dependency metadata is
cached only after full verification of the protected library opening. Normal
mount preparation checks index metadata, membership and sizes, and does not
populate dependency caches from unverified payloads.

Damage to shared data triggers **Rebuilding affected content**. The service
captures all affected sources and rebuilds an empty candidate root. Missing
sources or unowned auxiliary files produce specific diagnostics; failed repair
preserves the current publication. Replacement imports share that capture and
cannot remove a source file still needed by another asset.

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
