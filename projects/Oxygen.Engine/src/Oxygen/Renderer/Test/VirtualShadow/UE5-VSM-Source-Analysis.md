# UE5 VSM Source Analysis Reference

This document records the current source-grounded analysis of the UE5 virtual shadow map implementation from the local checkout at:

- `F:\projects\UnrealEngine2`

Purpose:

- preserve the results of the initial scan,
- provide a fast restart point for later targeted investigation,
- avoid rescanning the whole renderer and shader tree next time.

This is intentionally UE5-specific and source-oriented.

## 1. Scope Scanned

Primary shader scope:

- `Engine/Shaders/Private/VirtualShadowMaps/`

Primary renderer scope:

- `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/`
- `Engine/Source/Runtime/Renderer/Private/Shadows/`
- `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp`
- `Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp`
- `Engine/Source/Runtime/Renderer/Private/ShadowSetup.cpp`
- `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`
- `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`

## 2. Fast Entry Points

These are the best files/functions to open first for any future analysis.

### Main runtime orchestration

- `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp`
  - `VirtualShadowMapArray.BuildPageAllocations(...)` call around line `3697`
  - `CacheManager->ExtractFrameData(...)` call around line `3737`

- `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp`
  - `FSceneRenderer::RenderVirtualShadowMaps(...)` around line `1652`
  - `FSceneRenderer::RenderShadowDepthMaps(...)` around line `1656`

- `Engine/Source/Runtime/Renderer/Private/Shadows/ShadowSceneRenderer.cpp`
  - `AddLocalLightShadow(...)` line `145`
  - `RenderVirtualShadowMaps(...)` line `265`
  - `UpdateDistantLightPriorityRender(...)` line `313`
  - `DispatchVirtualShadowMapViewAndCullingSetup(...)` line `334`
  - `RenderVirtualShadowMapProjectionMaskBits(...)` line `400`
  - `ApplyVirtualShadowMapProjectionForLight(...)` line `454`

### Virtual shadow map core

- `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`
  - `Initialize(...)` line `393`
  - `MergeStaticPhysicalPages(...)` line `938`
  - `BuildPageAllocations(...)` line `1188`
  - `CreateVirtualShadowMapNaniteViews(...)` line `2465`
  - `RenderVirtualShadowMapsNanite(...)` line `2520`
  - `RenderVirtualShadowMapsNonNanite(...)` line `2606`
  - `UpdateHZB(...)` line `3084`
  - `AddRenderViews(const TSharedPtr<FVirtualShadowMapClipmap>&, ...)` line `3178`
  - `AddRenderViews(const FProjectedShadowInfo*, ...)` line `3244`

- `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapCacheManager.cpp`
  - `FVirtualShadowMapCacheEntry::UpdateClipmapLevel(...)` line `89`
  - `FVirtualShadowMapCacheEntry::Update(...)` line `148`
  - `FVirtualShadowMapPerLightCacheEntry::UpdateClipmap(...)` line `193`
  - `FVirtualShadowMapPerLightCacheEntry::UpdateLocal(...)` line `217`
  - `FVirtualShadowMapPerLightCacheEntry::Invalidate()` line `249`
  - `FVirtualShadowMapArrayCacheManager::Invalidate()` line `720`
  - `MarkCacheDataValid()` line `731`
  - `FindCreateLightCacheEntry(...)` line `758`
  - `UpdateUnreferencedCacheEntries(...)` line `810`
  - `ExtractFrameData(...)` line `906`
  - `ProcessInvalidations(FInvalidatingPrimitiveCollector...)` line `1139`
  - `ProcessInvalidations(FInstanceGPULoadBalancer...)` line `1250`

- `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapClipmap.cpp`
  - constructor line `87`
  - `OnPrimitiveRendered(...)` line `359`
  - `UpdateCachedFrameData()` line `382`

### Non-render-frame invalidation hook

- `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`
  - cache invalidation processing block around line `5470`

### Projection integration into lighting

- `Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp`
  - `ShadowSceneRenderer->ApplyVirtualShadowMapProjectionForLight(...)` around line `2302`

## 3. High-Level Call Chain

This is the practical call chain for the VSM system.

### Frame setup and allocations

1. `FDeferredShadingSceneRenderer` decides shadows need rendering.
2. If VSM is enabled:
   - `VirtualShadowMapArray.BuildPageAllocations(...)`
3. Then shadow depth rendering runs:
   - `RenderShadowDepthMaps(...)`
   - `RenderVirtualShadowMaps(...)`
4. Later, during lighting:
   - `RenderVirtualShadowMapProjectionMaskBits(...)`
   - `ApplyVirtualShadowMapProjectionForLight(...)`
5. At end of frame:
   - `CacheManager->ExtractFrameData(...)`

### Scene mutation invalidation

Outside the shadow depth frame path, scene updates flow through:

1. `RendererScene.cpp` collects removed/updated/moved primitives
2. builds `FInvalidatingPrimitiveCollector`
3. calls `CacheManager->ProcessInvalidations(...)`
4. which launches `VirtualSmInvalidateInstancePagesCS`

This path uses previous-frame buffers, not the current frame page table.

## 4. Key Source Files and What They Own

### `VirtualShadowMapArray.cpp`

Owns the frame-local VSM working set:

- current page requests,
- current page table and page flags,
- physical page list buffers,
- current projection upload,
- render passes for allocations,
- Nanite and non-Nanite shadow rendering,
- HZB update,
- static/dynamic page merge,
- render-view creation.

Think of this file as:

- "current frame VSM graph builder"

### `VirtualShadowMapCacheManager.cpp`

Owns persistent cross-frame state:

- physical page pool allocation,
- HZB pool allocation,
- previous extracted buffers,
- light cache entries,
- invalidation processing,
- readback feedback,
- cache validity state.

Think of this file as:

- "cross-frame cache and invalidation system"

### `VirtualShadowMapClipmap.cpp`

Owns directional light clipmap setup:

- clipmap level count,
- snapped centers,
- clipmap panning reuse logic,
- z-range stabilization,
- projection data generation,
- primitive reveal tracking.

Think of this file as:

- "directional light persistent virtual-space mapping"

### `ShadowSceneRenderer.cpp`

Owns shadow-system orchestration on the renderer side:

- local-light VSM setup,
- distant-light policy,
- per-frame scheduling of distant refresh,
- creation of packed Nanite shadow views,
- one-pass projection setup,
- final projection application.

Think of this file as:

- "bridge between legacy shadow setup and VSM runtime"

## 5. Cache State Model from Source

There is no explicit enum for the cache state machine.

The state is encoded by:

- global `bCacheDataValid`
- presence of `PrevBuffers.*`
- per-light `Prev/Current` frame-state
- page metadata flags

### Global cache manager state

Relevant code:

- `VirtualShadowMapArrayCacheManager::Invalidate()` line `720`
- `MarkCacheDataValid()` line `731`
- `IsCacheDataAvailable()` in header/cpp nearby
- `ExtractFrameData()` line `906`

Meaning:

- `Invalidate()`:
  - clears `CacheEntries`
  - sets `bCacheDataValid = false`

- `MarkCacheDataValid()`:
  - sets `bCacheDataValid = true`

- cache is only considered reusable if:
  - caching enabled,
  - cache valid,
  - previous page table/page flags/page rect bounds/projection/page lists all exist

### Per-light frame state

Defined in:

- `FVirtualShadowMapPerLightCacheEntry::FFrameState`

Fields:

- `bIsUncached`
- `bIsDistantLight`
- `RenderedFrameNumber`
- `ScheduledFrameNumber`

Key transitions:

- `UpdateLocal(...)` line `217`
  - local-light state update
  - invalidates if setup key changes and invalidation allowed
  - marks uncached when previous rendered frame is invalid

- `UpdateClipmap(...)` line `193`
  - directional-light state update
  - invalidates on light direction change, first clip level change, or forced invalidate

- `Invalidate()` line `249`
  - sets `Prev.RenderedFrameNumber = -1`

### Distant local light behavior

Implemented in:

- `ShadowSceneRenderer.cpp` `AddLocalLightShadow(...)` line `145`
- `UpdateDistantLightPriorityRender(...)` line `313`

Behavior:

- local lights may be downgraded to single-page VSMs when screen footprint is small
- fully cached distant lights can skip rendering this frame
- a priority queue selects a budgeted subset to refresh
- selected lights have:
  - `bShouldRenderVSM = true`
  - `Current.ScheduledFrameNumber = Scene.GetFrameNumber()`
  - `Invalidate()` called

### Unreferenced cache retention

Implemented in:

- `UpdateUnreferencedCacheEntries(...)` line `810`

Behavior:

- cache entries can live for a configured age even if the light is not referenced in the current render
- if retained, the entry gets newly allocated virtual IDs for this frame
- the physical pages remain alive through remapping

## 6. Physical Page Flags and Metadata

Primary source:

- `Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapPageAccessCommon.ush`

Important page flags:

- `VSM_ALLOCATED_FLAG`
- `VSM_DYNAMIC_UNCACHED_FLAG`
- `VSM_STATIC_UNCACHED_FLAG`
- `VSM_DETAIL_GEOMETRY_FLAG`

Important physical metadata flags:

- `VSM_PHYSICAL_FLAG_VIEW_UNCACHED`
- `VSM_PHYSICAL_FLAG_DIRTY`
- `VSM_PHYSICAL_FLAG_USED_THIS_FRAME`

Invalidation flag storage:

- `VSM_PHYSICAL_PAGE_INVALIDATION_FLAGS_SHIFT`

Physical metadata structure:

- `Flags`
- `Age`
- `VirtualShadowMapId`
- `MipLevel`
- `PageAddress`

This is the key link between:

- current contents,
- page age,
- virtual ownership,
- invalidation state.

## 7. Exact Allocation / Reuse Pipeline

Main source:

- `VirtualShadowMapArray.cpp` `BuildPageAllocations(...)` line `1188`

### Pass order inside `BuildPageAllocations`

Observed sequence:

1. `CacheManager->UpdateUnreferencedCacheEntries(*this)`
2. allocate/readback buffer for static invalidating primitives
3. upload projection data
4. allocate and clear:
   - `PageRequestFlagsRDG`
   - `DirtyPageFlagsRDG`
   - `PhysicalPageListsRDG`
   - `PageRectBoundsRDG`
5. `InitPageRectBounds`
6. per-view:
   - gather directional clipmap IDs
   - `MarkCoarsePages`
   - `PruneLightGrid`
   - `GeneratePageFlagsFromPixels(GBuffer)`
   - `GeneratePageFlagsFromPixels(HairStrands)` when relevant
7. create and clear:
   - `PageTableRDG`
   - `PageFlagsRDG`
8. `UpdatePhysicalPages`
9. `PackAvailablePages`
10. `AppendPhysicalPageList` with `EMPTY -> AVAILABLE`
11. `AllocateNewPageMappings`
12. `AppendPhysicalPageList` with `AVAILABLE -> REQUESTED`
13. `GenerateHierarchicalPageFlags`
14. `PropagateMappedMips`
15. page init sequence:
   - clear indirect args
   - `SelectPagesToInitialize`
   - `InitializePhysicalMemoryIndirect`
16. set current uniform SRVs
17. `Feedback Status`
18. `UpdateCachedUniformBuffer`
19. `CacheManager->MarkCacheDataValid()`

### Reuse semantics in `UpdatePhysicalPages`

Primary shader:

- `VirtualShadowMapPhysicalPageManagement.usf`
  - `UpdatePhysicalPages(...)` line `143`

Behavior:

- iterate all physical pages
- optionally read last frame's LRU ordering
- if previous page metadata is valid:
  - map previous virtual ID to current virtual ID through `NextVirtualShadowMapData`
  - apply clipmap page offset if present
  - check if current frame requested the page
  - check invalidation flags
  - preserve mapping if still usable
- if page no longer usable:
  - move to empty list

This is the core cache reuse logic.

### New allocation semantics

Primary shader:

- `AllocateNewPageMappingsCS(...)` line `425`

Behavior:

- for requested pages without existing mapping:
  - pop a physical page from available
  - clear old owner page-table entry if needed
  - create new page-table mapping
  - mark both dynamic and static uncached initially
  - set metadata owner, mip, page address

## 8. Selective Clear / Initialization

Primary shader:

- `SelectPagesToInitializeCS(...)` line `609`
- `InitializePhysicalPagesIndirectCS(...)` line `700`

Behavior:

- fully cached pages are left untouched
- uncached pages are selected into an indirect list
- initialization strategy:
  - if static slice is valid and separate static caching is active:
    - copy static slice into dynamic slice
  - otherwise:
    - clear the page to zero

This confirms:

- UE5 does not clear the full physical page pool every frame
- it clears only selected pages

## 9. Invalidation Path

CPU entry:

- `RendererScene.cpp` around line `5470`

GPU shader:

- `VirtualShadowMapCacheManagement.usf`
  - `VirtualSmInvalidateInstancePagesCS(...)` line `54`

Behavior:

- collect moved/updated/removed instances
- decode invalidation payload:
  - all VSMs or single VSM
  - static invalidation flag or not
- project changed bounds into previous VSM space
- overlap against valid cached pages
- if overlapped:
  - OR invalidation bits into physical page metadata

Important detail:

- this is previous-frame-space invalidation
- it uses previous page table and previous projection data

## 10. Non-Nanite Rendering Path

Primary source:

- `VirtualShadowMapArray.cpp` `RenderVirtualShadowMapsNonNanite(...)` line `2606`

Core helper:

- `AddCullingPasses(...)` around line `2205`

Main compute shaders:

- `CullPerPageDrawCommandsCs` line `177`
- `AllocateCommandInstanceOutputSpaceCs` line `467`
- `OutputCommandInstanceListsCs` line `498`

Behavior:

1. create shadow views
2. choose HZB source:
   - previous HZB or current HZB mode 2
3. cull mesh instances against shadow pages
4. output compact instance/page lists
5. raster only the relevant commands
6. update dirty page flags afterward

Important side effects in `CullPerPageDrawCommandsCs`:

- marks overlapped rendered pages dirty
- emits visible instance commands per page
- records static primitive invalidation feedback

## 11. Nanite Rendering Path

Primary source:

- `CreateVirtualShadowMapNaniteViews(...)` line `2465`
- `RenderVirtualShadowMapsNanite(...)` line `2520`

Behavior:

- build packed shadow views for all VSMs that actually render this frame
- use previous HZB view params when available
- raster directly into physical page pool
- if HZB enabled:
  - call `UpdateHZB(...)`

Important clipmap/local-light view setup:

- `AddRenderViews(...)` line `3178`
- `AddRenderViews(...)` line `3244`

These functions:

- mark cache entries rendered,
- set `NANITE_VIEW_FLAG_UNCACHED` when appropriate,
- optionally attach previous HZB view params,
- optionally store current HZB metadata.

## 12. HZB Pipeline

Primary source:

- `UpdateHZB(...)` line `3084`

Pass sequence:

1. create indirect args and page-selection buffer
2. `SelectPagesForHZBAndUpdateDirtyFlagsCS`
3. `BuildHZBPerPageCS`
4. `BuildHZBPerPageTopCS`

Important detail:

- `SelectPagesForHZBAndUpdateDirtyFlagsCS` also folds dirty/invalidation flags into physical page metadata
- HZB rebuild is selective:
  - dirty pages
  - newly allocated pages on first build this frame
  - or all pages if forced

## 13. Static/Dynamic Separate Caching

Primary source:

- `VirtualShadowMapArray.cpp` `MergeStaticPhysicalPages(...)` line `938`
- `VirtualShadowMapPhysicalPageManagement.usf`
  - `SelectPagesToMergeCS`
  - `MergeStaticPhysicalPagesIndirectCS`

Behavior:

- if static caching is enabled, physical pool uses two array slices
- static and dynamic content can live separately
- after rendering, dirty pages may need static slice merged into dynamic slice

Current array logic:

- slice `0`: dynamic
- slice `1`: static cached array when enabled

## 14. Projection Pipeline

Renderer integration:

- `ShadowSceneRenderer.cpp`
  - `RenderVirtualShadowMapProjectionMaskBits(...)` line `400`
  - `ApplyVirtualShadowMapProjectionForLight(...)` line `454`

Lighting integration:

- `ShadowRendering.cpp` around line `2302`

Projection C++ side:

- `VirtualShadowMapProjection.cpp`

Projection shader entry points:

- `VirtualShadowMapProjection.usf`
  - `VirtualShadowMapProjection(...)` line `565`

- `VirtualShadowMapProjectionComposite.usf`
  - `VirtualShadowMapCompositeTileVS(...)` line `14`
  - `VirtualShadowMapCompositePS(...)` line `36`
  - `VirtualShadowMapCompositeFromMaskBitsPS(...)` line `44`

Behavior:

### One-pass local-light mode

- `RenderVirtualShadowMapProjectionMaskBits(...)`
  - allocates packed mask-bit textures
  - runs one-pass projection for:
    - `GBuffer`
    - `HairStrands` if present

- later `ApplyVirtualShadowMapProjectionForLight(...)`
  - extracts a single light from packed mask bits using `CompositeVirtualShadowMapFromMaskBits`

### Per-light mode

- project one light at a time into a shadow factor texture
- composite into final screen mask

### Directional light mode

- directional clipmaps stay on explicit projection path
- not part of one-pass packed local-light path

## 15. Shader Inventory by Role

### Page request generation

- `VirtualShadowMapPageMarking.usf`
  - `PruneLightGridCS`
  - `GeneratePageFlagsFromPixels`
  - `MarkCoarsePages`

### Cache / physical page management

- `VirtualShadowMapPhysicalPageManagement.usf`
  - `UpdatePhysicalPages`
  - `AllocateNewPageMappingsCS`
  - `PackAvailablePages`
  - `AppendPhysicalPageLists`
  - `SelectPagesToInitializeCS`
  - `InitializePhysicalPagesIndirectCS`
  - `SelectPagesToMergeCS`
  - `MergeStaticPhysicalPagesIndirectCS`
  - `UpdateAndClearDirtyFlagsCS`
  - `SelectPagesForHZBAndUpdateDirtyFlagsCS`
  - `BuildHZBPerPageCS`
  - `BuildHZBPerPageTopCS`
  - `FeedbackStatusCS`

### Hierarchical page metadata

- `VirtualShadowMapPageManagement.usf`
  - `GenerateHierarchicalPageFlags`
  - `PropagateMappedMips`

### Invalidation

- `VirtualShadowMapCacheManagement.usf`
  - `VirtualSmInvalidateInstancePagesCS`

### Non-Nanite per-page command generation

- `VirtualShadowMapBuildPerPageDrawCommands.usf`
  - `CullPerPageDrawCommandsCs`
  - `AllocateCommandInstanceOutputSpaceCs`
  - `OutputCommandInstanceListsCs`

### Projection and composite

- `VirtualShadowMapProjection.usf`
- `VirtualShadowMapProjectionComposite.usf`
- helper includes:
  - `VirtualShadowMapProjectionCommon.ush`
  - `VirtualShadowMapProjectionDirectional.ush`
  - `VirtualShadowMapProjectionSpot.ush`
  - `VirtualShadowMapProjectionFilter.ush`
  - `VirtualShadowMapSMRTCommon.ush`
  - `VirtualShadowMapSMRTTemplate.ush`
  - `VirtualShadowMapTransmissionCommon.ush`

### Core shared structures

- `VirtualShadowMapPageAccessCommon.ush`
- `VirtualShadowMapPageCacheCommon.ush`
- `VirtualShadowMapProjectionStructs.ush`

## 16. Clipmap-Specific Notes

Primary source:

- `VirtualShadowMapClipmap.cpp`

Important behavior:

- clipmap base ID is contiguous for all levels
- clipmap centers are snapped in light space
- each level stores:
  - world center
  - corner offset
  - relative corner offset
  - view-to-clip matrix
- cache reuse depends on:
  - page-space pan staying valid
  - z-range guardband remaining valid
  - z-range size matching previous

Primitive reveal tracking:

- `OnPrimitiveRendered(...)`
  - compares current visibility against previous-frame `RenderedPrimitives`
  - sets `RevealedPrimitivesMask`

That mask is consumed later in non-Nanite culling to force rendering when a primitive goes from hidden to visible.

## 17. Local-Light-Specific Notes

Primary source:

- `ShadowSceneRenderer.cpp` `AddLocalLightShadow(...)`

Important behavior:

- local-light VSM ID allocation can be:
  - single-page for distant lights
  - full VSM otherwise
- per-face update for one-pass point light shadows
- projection data is regenerated each frame and stored in cache entry
- `ProjectedShadowInfo->bShouldRenderVSM` is set from cache state:
  - skip rendering when fully cached distant policy applies

## 18. Useful Search Terms for Follow-Up Work

If a future task needs deeper analysis, these search targets are likely enough:

- `VirtualShadowMapArray::BuildPageAllocations`
- `VirtualShadowMapArrayCacheManager::ProcessInvalidations`
- `FShadowSceneRenderer::AddLocalLightShadow`
- `FShadowSceneRenderer::UpdateDistantLightPriorityRender`
- `VirtualShadowMapClipmap::FVirtualShadowMapClipmap`
- `UpdatePhysicalPages`
- `AllocateNewPageMappingsCS`
- `SelectPagesToInitializeCS`
- `BuildHZBPerPageCS`
- `CullPerPageDrawCommandsCs`
- `VirtualShadowMapProjection`
- `CompositeVirtualShadowMapFromMaskBits`

## 19. Likely Next Deep-Dive Topics

Good focused next analyses:

- exact semantics of `ShouldCacheInstanceAsStatic(...)`
- detailed meaning of `GetPageFlagMaskForRendering(...)`
- clipmap projection math and `ClipmapCornerRelativeOffset`
- HZB usage differences between mode `1` and mode `2`
- one-pass packed mask bit layout and per-light extraction cost
- static invalidation feedback loop from GPU readback to CPU cache policy
- interaction of material WPO invalidation with static caching
- page overflow behavior and failure modes when allocation runs out

## 20. Practical Summary

The most important restart facts are:

- frame-local logic lives in `VirtualShadowMapArray.cpp`
- cross-frame logic lives in `VirtualShadowMapCacheManager.cpp`
- directional clipmap behavior lives in `VirtualShadowMapClipmap.cpp`
- local-light policy and projection orchestration live in `ShadowSceneRenderer.cpp`
- previous-frame invalidation enters through `RendererScene.cpp`
- all actual reuse happens in `UpdatePhysicalPages`
- selective clear happens in `InitializePhysicalPagesIndirectCS`
- non-Nanite page-aware draw generation happens in `CullPerPageDrawCommandsCs`
- lighting projection is split between:
  - one-pass packed local-light masks
  - explicit per-light projection

This file should be enough to resume specific VSM analysis without rewalking the full renderer tree.
