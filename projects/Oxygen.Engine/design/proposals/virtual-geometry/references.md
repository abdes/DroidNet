# Virtual geometry references

Status: proposal. Reading map for [virtual geometry](README.md). The module
documents own Oxygen's contracts; references explain mechanisms and tradeoffs.
A reference implementation is not permission to import its full architecture.

Last updated: 2026-10-24

Read: [Oxygen sources](#oxygen-sources), [UE 5.7](#ue-57), [D3D12](#d3d12),
[geometry research](#geometry-research), [decision evidence](#decision-evidence).

## Oxygen sources

Reviewed against `master` on 2026-10-24. These are discovery entry points for
the capabilities the design reuses; none implies the new capability exists.

| Area                 | Source                                                                                                                                                                                                                                                         | Read for                                                               |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| Identity retirement  | [IndexReuse](../../../src/Oxygen/Nexus/IndexReuse.h), [RetirementState](../../../src/Oxygen/Nexus/RetirementState.h), [slot retirement](../../../src/Oxygen/Nexus/Docs/slot-retirement.md)                                                                     | Ticketed retirement, owner/use/pin accounting, allocation-free release |
| Managed resources    | [Registration](../../../src/Oxygen/Graphics/Common/Registration.h), [ResourceRegistry](../../../src/Oxygen/Graphics/Common/ResourceRegistry.h)                                                                                                                 | Ownership, immutable views, pins versus leases, quarantine             |
| Submission           | [RecordingUseBatch](../../../src/Oxygen/Graphics/Common/RecordingUseBatch.h), [Submission](../../../src/Oxygen/Graphics/Common/Submission.h), [CommandQueueSubmission](../../../src/Oxygen/Graphics/Common/CommandQueueSubmission.cpp)                         | Dependencies, discard, receipts, uncertain execution                   |
| Budget               | [AllocationBudget](../../../src/Oxygen/Graphics/Common/AllocationBudget.h), [D3D12 Graphics](../../../src/Oxygen/Graphics/Direct3D12/Graphics.cpp)                                                                                                             | Charging at the factory, driver headroom                               |
| Indirect execution   | [CommandRecorder](../../../src/Oxygen/Graphics/Common/CommandRecorder.h), [D3D12 recorder](../../../src/Oxygen/Graphics/Direct3D12/CommandRecorder.cpp)                                                                                                        | `ExecuteIndirect`, root-less signatures, state transitions             |
| Upload and feedback  | [Upload LLD](../../vortex/lld/upload.md), [GPU feedback LLD](../../vortex/lld/gpu-feedback.md), [UploadCoordinator](../../../src/Oxygen/Vortex/Upload/UploadCoordinator.h)                                                                                     | Ticket semantics, immutable uploads, bounded feedback                  |
| Data contract rework | [Immutable inputs and receipt lifetimes](../../vortex/milestones/VX-GPU-01/README.md#target-model)                                                                                                                                                             | The rule that nothing reachable by recorded work is rewritten in place |
| Stages and depth     | [SceneRenderer](../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp), [stage contract](../../vortex/ARCHITECTURE.md#62-runtime-stage-contract), [DepthPrePassPolicy](../../../src/Oxygen/Vortex/SceneRenderer/DepthPrePassPolicy.h)                     | Reserved Stage 4, depth completeness, fixed stage order                |
| Culling              | [Occlusion LLD](../../vortex/lld/occlusion.md), [IndirectListBuilder](../../../src/Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.cpp)                                                                                              | Two-phase culling, visibility history, the planned cluster level       |
| Shadows              | [ShadowCasterDependencies](../../../src/Oxygen/Vortex/Shadows/Internal/ShadowCasterDependencies.cpp), [LocalShadowRequest](../../../src/Oxygen/Vortex/Shadows/Internal/LocalShadowRequest.cpp), [virtual shadow maps](../../vortex/lld/virtual-shadow-maps.md) | Caster dependency reuse rules; the planned VSM page allocation         |
| Geometry             | [GeometryUploader](../../../src/Oxygen/Vortex/Resources/GeometryUploader.h), [Data overview](../../../src/Oxygen/Data/README.md), [packing discipline](../../../src/Oxygen/Data/Docs/binary_packing_discipline.md)                                             | Existing asset and mesh-LOD ownership; wire rules                      |
| Cooking              | [Geometry import pipeline](../../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md)                                                                                                                                                                | Job scheduling, cancellation, resource emission, schemas               |
| Loading              | [IAssetLoader](../../../src/Oxygen/Content/IAssetLoader.h), [Content overview](../../../src/Oxygen/Content/Docs/overview.md)                                                                                                                                   | Source-scoped typed loading; GPU-independent Content                   |

## UE 5.7

Inspected UE 5.7.4, changelist 51494982, compatible changelist 47537391,
branch `++UE5+Release-5.7` (`Engine/Build/Build.version`), at
`F:/Epic Games/UE_5.7/Engine`. Another licensed checkout works if its
`Build.version` is recorded. Neither the older `UnrealEngine2` checkout nor
the current online 5.8 documentation may be substituted while claiming 5.7
parity, and no UE source is vendored. Paths are relative to the Engine root and
every file and symbol below was located in that checkout.

| Topic          | File and symbols                                                                                                                                                                                                                                | Lesson                                                                                                                      |
| -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| VSM pages      | `Shaders/Private/VirtualShadowMaps/VirtualShadowMapPhysicalPageManagement.usf`: `UpdatePhysicalPages`, `AllocateNewPageMappings`, `PackAvailablePages`                                                                                          | GPU page authority; retained invalidation; empty, LRU and requested lists                                                   |
| VSM init/merge | Same file: `SelectPagesToInitializeCS`, `InitializePhysicalPagesIndirectCS`, `SelectPagesToMergeCS`, `BuildHZBPerPage`                                                                                                                          | GPU-selected indirect work; reversed-Z merge versus conservative HZB min                                                    |
| VSM cache      | `Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapCacheManager.cpp`: `ExtractFrameData`, `PreSceneUpdate`, `PostGPUSceneUpdate`, `UpdateClipmapLevel`                                                                          | Retain GPU resources rather than CPU page snapshots; old/new footprint invalidation                                         |
| VSM feedback   | Same file: `FVirtualShadowMapFeedback::SubmitFeedbackBuffer`, `GetLatestReadbackBuffer`                                                                                                                                                         | Bounded nonblocking feedback                                                                                                |
| VSM draw       | `Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`: `AddCullingPasses`, `AddRasterPass`; `Shaders/Private/ShadowDepthPixelShader.usf` (`VIRTUAL_TEXTURE_TARGET`)                                                     | Compact indirect instance work; virtual lookup with atomic depth writes (future VSM, not geometry paging)                   |
| Nanite stream  | `Source/Runtime/Engine/Private/Rendering/NaniteStreamingManager.cpp`: `RegisterStreamingPage`, `ArePageDependenciesCommitted`, `InstallReadyPages`, `UninstallResidentPage`, `ApplyFixups`; header in `Source/Runtime/Engine/Public/Rendering/` | Streaming and root pages are distinct identities; dependency refcounts; fixups applied only when dependencies are resident  |
| Nanite upload  | `Source/Runtime/Engine/Private/Nanite/NaniteStreamingPageUploader.cpp`: `Add_GetRef`, transcode passes                                                                                                                                          | Explicit dependency-ordered install passes; bounded installs per update                                                     |
| Nanite cut     | `Shaders/Private/Nanite/NaniteClusterCulling.usf`: `ShouldVisitChildInternal`, `SmallEnoughToDraw`, `RequestPageRange`; `Shaders/Private/Nanite/NaniteHierarchyTraversal.ush`: `PersistentNodeAndClusterCull`                                   | Per-node parent-error test then per-cluster draw test; missing data issues streaming requests; persistent-thread work queue |
| Nanite builder | `Source/Developer/NaniteBuilder/Private/ClusterDAG.cpp`: `FClusterDAG::ReduceGroup`, `FClusterDAG::FindCut`                                                                                                                                     | Offline group hierarchy; `FindCut` is builder code, not the runtime cut                                                     |

Facts drawn from the inspection:

- UE picks victim physical pages on the CPU before I/O, walking an LRU and
  requiring a zero reference count; Oxygen deliberately does not.
- Root pages share the physical buffer but are allocated separately, are never
  streaming or LRU candidates, are versioned so stale requests for a reused
  slot are discarded, and fixups never wait on them. This is the reference for
  guaranteed coarse coverage.
- Installs are bounded (`r.Nanite.Streaming.MaxPendingPages`, default 128) and
  a pool resize uninstalls all streaming pages and reapplies root fixups.
- UE has no GPU permit protocol: its guarantee is CPU refcount and LRU
  bookkeeping plus GPU transcode ordering. Oxygen's permit and abstract
  credits are its own design choice.

Adopt invariants and workload shape, not hidden assumptions. VSM, streamed
geometry and GPU-generated geometry have different production policies.

## D3D12

Microsoft references, reviewed 2026-09-27:

| Topic           | Reference                                                                                                                                                                                                      | Review question                                                             |
| --------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| ExecuteIndirect | [ExecuteIndirect](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-executeindirect)                                                                                | Signature compatibility, argument and count bounds, states                  |
| Barriers        | [Resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)                                                          | UAV visibility and state transitions are separate from queue fences         |
| Tiled resources | [UpdateTileMappings](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-updatetilemappings)                                                                                 | CPU queue mapping API; do not make each GPU page decision a host round trip |
| Residency       | [Residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency), [EnqueueMakeResident](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device3-enqueuemakeresident) | Backing residency and budget differ from page-content availability          |

## Geometry research

| Topic         | Reference                                                                                                                                                                                                               | Use and limits                                                                                                            |
| ------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| meshoptimizer | [meshoptimizer](https://github.com/zeux/meshoptimizer), [clusterlod.h](https://github.com/zeux/meshoptimizer/blob/master/demo/clusterlod.h)                                                                             | Candidate clustering and simplification; group DAG versus traversal hierarchy; pin a reviewed revision before adoption    |
| Nanite talk   | [Karis, Stubbe, Wihlidal: A Deep Dive into Nanite](https://advances.realtimerendering.com/s2021/Karis_Nanite_SIGGRAPH_Advances_2021_final.pdf), [course index](https://advances.realtimerendering.com/s2021/index.html) | Research reading on clustering, streaming and rendering; not a substitute for UE 5.7 source or Oxygen's bounded cut proof |
| Quantization  | [AMD meshlet compression](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-meshlet_compression/)                                                                                                                     | Compatible global and local integer grids; numerical cracks are separate from topology compatibility                      |
| Mesh shaders  | [AMD optimization and best practices](https://gpuopen.com/learn/mesh_shaders/mesh_shaders-optimization_and_best_practices/)                                                                                             | Output allocation, occupancy and cluster limits for the later execution profile                                           |

## Decision evidence

These are Oxygen design choices with their costs, not claims that UE supplies
the proposed mechanisms.

| Decision                               | Mechanism and evidence                                                                                                                                                                                                                                                               | Rejected alternative and cost                                                                                                                                                                                                                |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Partition/recluster incidence DAG      | Unique incidence and whole-group rewrites. `FClusterDAG::ReduceGroup`/`FindCut` and meshoptimizer `clodBuild`/`clodGroup` establish collective coarse siblings, generating groups and hierarchical error.                                                                            | A tree constrains regrouping; arbitrary overlapping rewrites need another solver. The bounded rank, prefix and checkpoint protocol is Oxygen's conservative v1; UE `FindCut` is offline evidence.                                            |
| One completion permit, protect-first   | A single non-preemptive permit; the GPU protects dependencies and reserves abstract credits before eviction. UE `RegisterStreamingPage`, victim refcount checks and the dependency-ordered page uploader support the ordering.                                                       | UE chooses CPU physical slots before I/O. Abstract credits permit partial delivery at the cost of one unit's head-of-line delay and a conservative closure footprint. Full-closure staging and multiple permits are not part of this design. |
| Conservative conventional-shadow reuse | `LocalShadowRequest` already filters per-product influence before disabling reuse, and the target allocator and shadow pass honour it.                                                                                                                                               | GPU-conditional clear, draw and publication is a larger cache change; CPU epochs alone miss GPU approximation changes. Affected products lose cache and sharing benefits.                                                                    |
| Owned immutable install buffers        | `StagingProvider` retains a buffer and `RingBufferStaging` resets partition heads, so ring ranges are not long-lived sources. `UploadTicket::Cancel` is logical, not copy completion. Use owned destinations filled through Upload and retained by Graphics during one-shot install. | A new pin-aware range allocator or repurposing `AtlasBuffer` is a larger lifetime API. Batches cost extra buffer and copy capacity and possible frame latency; recycling needs no per-entry acknowledgement.                                 |

`NaniteClusterCulling.usf` separates hierarchy visit from cluster draw error
tests and handles streaming leaves, but neither it nor the builder proves
Oxygen's coverage, capacity or liveness contract. Audit the library's sloppy
simplification, border dilation and empty-result behavior before adoption.
Do not copy a recommendation from a paper into a normative constant without its
applicable conditions and a verification case.

Keep reusable tests and compact proof summaries in Git; raw captures, logs,
traces and source bundles stay in ignored output with reproduction,
configuration and source identity. No archived VSM result is proof of this GPU
pool or of virtual geometry.
