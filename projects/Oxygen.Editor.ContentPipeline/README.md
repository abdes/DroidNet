# Oxygen.Editor.ContentPipeline

Editor-side orchestration for producing runtime content from authoring data.

Read [workflow stages](#explicit-workflow-stages),
[worker ownership](#tool-process-boundary) and
[freshness and validation](#freshness-and-validation), and
[test organization](tests/README.md).

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
asset, folder, and project cook operations. The stages keep failures
attributable to the operation that produced them.

| Stage                     | Implementation                                                                                                                               | Purpose                                                                                 |
| ------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Scope                     | Project context and cook-scope provider                                                                                                      | Identify authored inputs and destination roots.                                         |
| Descriptors               | [SceneDescriptorGenerator](src/SceneDescriptorGenerator.cs)                                                                                  | Translate authored scenes into native descriptors; engine content is referenced as is.  |
| Manifest                  | [ContentImportManifestBuilder](src/ContentImportManifestBuilder.cs), [ContentImportManifestValidator](src/ContentImportManifestValidator.cs) | Build and validate the bounded import request.                                          |
| Execution                 | [IEngineContentPipelineApi](src/IEngineContentPipelineApi.cs)                                                                                | Isolate orchestration from the native execution adapter.                                |
| Inspection and validation | Native Inspector inventory under `CookOutputReadLease`                                                                                       | Verify files once per protected opening and derive structured results from that report. |

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
concurrently, preserving argument boundaries and avoiding serial pipe-read
stalls.

Cancellation stops the owned worker tree and drains both output readers before
releasing its input and tool leases. Failed termination retains ownership until
the worker drains. Progress and structured warnings/errors flow into the shared
Cooking panel throughout import, validation and publication.

Every import reads two lists per source from the native report. Its products
(assets and texture descriptors, by virtual path) say which source produced each
cooked asset: a model's or an input file's products are attributed from this
list, never from the folder they cook into, and become the provenance map that
status, library resolution and the next cook read. Its output files identify
source-owned auxiliary files; the native index owns their sizes and digests.
Named texture outputs use the descriptor's `virtual_path` and participate in the
same cooking, reuse and reference-resolution workflow.

## Cook on change

[AutomaticCookService](src/Cooking/AutomaticCookService.cs) turns the asset
catalog's changes (`IAssetCatalog.Changes`) into automatic runs while the
project's **Cook on change** setting is on. Changes collect over a quiet
window; the run starts once the project writer is idle
(`ICookRunService.WhenWriterIdleAsync`) and cooks only the changed sources
whose published output does not match what is on disk.
[ContentCookCoordinator](src/ContentCookCoordinator.cs) holds the one project
writer. Its [Automatic](src/ContentCookCoordinator.Automatic.cs) part stops a
running cook on change when foreground work waits (`CookPreemptedException`),
and its [Shared](src/ContentCookCoordinator.Shared.cs) part lets a request join
a pending cook of the same or a covering scope. The rules are in
[editor documents §5.3](../../design/editor/lld/editor-documents.md#53-behavior).

## Freshness and validation

Browser badges compare saved inputs against the last successful publication. They consume event-driven snapshots, launch no
native processes and hash no cooked payloads. Unknown output availability is
neutral; an observed missing output offers cooking. Filtering and resolving
unchanged rows reuse the snapshot.

Native Content owns complete file-integrity validation. Cooking verifies reused
content and newly produced output before publication. Dependency metadata is
cached only after full verification of the protected library opening. Normal
mount preparation checks index metadata, membership and sizes, and does not
populate dependency caches from unverified payloads.

Pak libraries are immutable: the engine content pak (`IEngineContentPak`, always
the lowest library) and the pak files a project declares. A pak's catalog digest
is its fingerprint, a shared read handle (`PakReadLease`) keeps its bytes fixed
while a cook or runtime uses it, and the engine verifies its integrity when it
mounts it. Publication roots record whether a library is a loose folder or a
pak, and mount preparation refuses a pak whose identity or digest changed since
publication.

Damage to shared data triggers **Rebuilding affected content**. The service
captures all affected sources and rebuilds an empty candidate root. Missing
sources or unowned auxiliary files produce specific diagnostics; failed repair
preserves the current publication. Replacement imports share that capture and
cannot remove a source file still needed by another asset.

## Verification boundaries

[Unit tests](tests/Unit) cover orchestration, descriptors, source snapshots and
status using controlled dependencies. [Integration tests](tests/Integration)
exercise the installed native tools, real worker processes and filesystem
publication/recovery. [Benchmarks](tests/Benchmarks) retain large-workload
measurements separately from routine correctness checks.

See the [test guide](tests/README.md) for project selection and fixture
ownership.

Keep source-level tests, real tool execution, cooked-root validation, and
standalone/visual validation distinct. Current workflow evidence belongs in
[IMPLEMENTATION_STATUS.md](../../design/editor/IMPLEMENTATION_STATUS.md);
test files alone do not close those gates.
