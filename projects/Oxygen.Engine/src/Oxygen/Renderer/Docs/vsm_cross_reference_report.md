# Oxygen Engine VSM — Full Cross-Reference Report Against UE5

I have read the entire VSM stack: the four GPU shaders (`VirtualShadowRequest.hlsl`, `VirtualShadowResolve.hlsl`, `VirtualShadowCoarseMark.hlsl`, `VirtualShadowAtlasDebug.hlsl`), the sampling library (`ShadowHelpers.hlsli`) and all its includes (`DirectionalVirtualShadowMetadata.hlsli`, `VirtualShadowPageAccess.hlsli`, `ShadowFrameBindings.hlsli`, `ShadowInstanceMetadata.hlsli`), the five C++ pass files, `ShadowManager.cpp`, and the full 4145-line `VirtualShadowMapBackend.cpp`. Below is the assessment.

---

## I. Architecture Overview

| Aspect | UE5 | Oxygen |
|---|---|---|
| Page management | 4-list GPU-side (LRU/Available/Empty/Requested), 15+ sequential compute dispatches | CPU free-list + LRU eviction heuristic, results uploaded via `CopyBuffer` |
| Physical pool | `Texture2DArray<uint>` (uint depth) | Single `Texture2D<float>` atlas (hardware-filterable depth) |
| Page table | Multi-level with hierarchical flags (H-page pyramid), GPU-authored | Flat `StructuredBuffer<uint>`, CPU-authored, uploaded per-frame |
| Shadow sampling | SMRT ray marching (configurable ray count, samples/ray, extrapolate slope) | Hardware `SamplerComparisonState` + page-aware tent 3x3/5x5 |
| Raster path | GPU-driven: `BuildPerPageDrawCommands` culls+bins entire scene per-page in one dispatch | CPU loop: for each resolved page, set viewport → clear → loop partitions → `Draw()` per mesh |
| Feedback | GPU page-flag marking → immediate same-frame allocation | GPU bitmask → readback → CPU integration next frame |
| Static/dynamic split | Full: static pages cached, only dynamic geometry invalidates | None: all pages are treated as potentially dirty |

---

## II. Strong Points

### 1. Filterable depth atlas
Storing depth as `float` in a standard `Texture2D` lets you use `SamplerComparisonState` and hardware PCF/tent. This is simpler than UE5's uint-depth + manual comparison, and the tent filter gives decent soft edges for the cost.

### 2. Footprint-driven clip selection
Both the request shader and the shading path use `SelectDirectionalVirtualClipForFootprint`, matching the clip level to the screen-space derivative. This is the correct approach — UE5 does the same via its `CalcClipmapLevel` — and you also have `ComputeDirectionalVirtualFootprintBlendToFinerClip` for smooth transitions.

### 3. Comprehensive bias system
The dual-path bias in `SampleDirectionalVirtualShadowClipVisibility` — normal+constant bias for fine pages, receiver-plane depth bias + slope bias for coarse fallback — is well thought out. The comment *"bias only perturbs the depth comparison, so it cannot translate the sampled shadow laterally off the caster base"* shows correct understanding of the anchoring problem.

### 4. Fallback LOD chain
`PopulateDirectionalFallbackPageTableEntries` builds transitive fallback chains, and the shader-side `TryResolveDirectionalVirtualPageLookup` + `BuildDirectionalVirtualClipRelativeTransform` correctly remaps UV/depth across clip levels. This matches UE5's `LODOffset` mechanism.

### 5. Clipmap origin snapping
Light-eye XY is snapped to the coarsest clip's page grid, eliminating sub-pixel shimmer. Per-clip `clip_grid_origin_x/y` in absolute integer space makes cache carry-forward independent of incremental panning — very clean.

### 6. Depth guardband for temporal stability
The previous frame's near/far planes are reused when the current frustum still fits within `kDirectionalVirtualDepthGuardbandSafety` of the previous range, preventing depth re-normalization flicker. UE5 doesn't expose this explicitly; you went further.

### 7. Request shader derivative safety
The request shader manually loads 4 neighbours and picks the shortest valid delta rather than using `ddx`/`ddy` in a compute shader (which wouldn't work). The `SelectShortestDirectionalFootprintDelta` pattern handles edge pixels and sky pixels correctly.

### 8. Multi-channel feedback (detail + coarse)
The separate `VirtualShadowFeedbackKind::kDetail` and `kCoarse` channels allow different feedback lifetimes and policies. UE5 merges everything into one flag buffer; having two channels gives you finer eviction control.

---

## III. Issues and Disasters

### CRITICAL — 1. CPU-authored page table + CPU-driven raster = the core scalability disaster

**What**: Every frame, the CPU iterates every selected page, builds per-page ortho `ViewConstants`, uploads the page table via `memcpy` → `CopyBuffer`, then the raster pass loops each page sequentially: `ClearDepthStencilView` → set viewport → for each partition → `Draw()` per mesh.

**Why it's a disaster**: With 12 clip levels × 32×32 pages = 12,288 virtual pages and hundreds of meshes, this produces **tens of thousands of individual draw calls** with per-page CPU overhead. UE5's entire point of `FCullPerPageDrawCommandsCs` is to do this on the GPU in a single dispatch — the CPU submits one `DispatchIndirect` and the GPU bins all geometry to all pages.

**Severity**: This will become the dominant frame-time bottleneck the moment you have a non-trivial scene. The CPU page loop in `VirtualShadowPageRasterPass::DoExecute` issues one `Draw()` per mesh per page. With 200 pages × 100 meshes = 20,000 draw calls, each incurring D3D12 command list recording overhead.

**Fix**: Implement GPU-driven indirect rendering: a compute shader that reads the page table and scene draw list, bins meshes to pages, and writes indirect draw arguments. The raster pass then issues `ExecuteIndirect` once per atlas.

---

### CRITICAL — 2. Page-aware tent sampling does full page-table re-resolution per tap

**What**: `SampleVirtualShadowComparisonTent3x3PageAware` calls `ResolveDirectionalVirtualAtlasUv` for **each of the 9 bilinear taps**. Each call does:
- `ProjectDirectionalVirtualClip` (bounds check + division)
- Page table load
- `DecodeVirtualShadowPageTableEntry`
- `ResolveVirtualShadowFallbackClipIndex`
- Guard texel clamping
- Pool dimension div

For 5x5, that's 25 full page-table lookups per pixel.

**Why it's a disaster**: This is the inner loop of every lit pixel. At 1080p that's ~2M pixels × 25 page table loads = **50M buffer reads** just for the filter kernel, plus all the ALU. UE5's SMRT avoids this entirely — it reads the page table once, then ray-marches within the single physical page.

**Fix**: Resolve the page table **once** for the center sample. If `current_lod_valid` is true, all 3x3/5x5 taps are guaranteed to land in the same physical page (that's what the guard texels are for!). Only re-resolve if you cross a page boundary, which the guard band exists to prevent. The current code defeats the entire purpose of the guard texels.

---

### CRITICAL — 3. No hierarchical page flag propagation

**What**: UE5 propagates page flags upward through a hierarchy: if any fine page under a coarse page is marked, the coarse page inherits the mark. This allows O(1) early-out tests per coarse page.

Oxygen has no equivalent. The coarse mark shader (`VirtualShadowCoarseMark.hlsl`) loops over **all** coarse clip levels per pixel with no spatial hierarchy. The request shader similarly iterates clip levels linearly.

**Why it matters**: Without hierarchical flags, you can't do efficient per-page overlap tests or fast streaming decisions. Every page decision requires scanning the full flat table.

---

### HIGH — 4. Multi-frame feedback loop introduces 2-3 frame latency

**What**: The request path is: Frame N: GPU dispatches `VirtualShadowRequest.hlsl` → readback. Frame N+1: CPU reads back bitmask in `ProcessCompletedFeedback` → submits to `ShadowManager`. Frame N+2: `PublishView` integrates feedback → builds page table → uploads. Frame N+3: raster pass renders the newly allocated pages.

**Why it matters**: When the camera moves quickly, the pages you need now were requested 2-3 frames ago. The fallback to coarser LODs covers this gracefully in steady state, but during rapid camera rotation you'll see a "quality pop" as fine pages arrive late. UE5's flag-based system allocates in the **same frame** the flag was set.

**Fix**: This is inherent to the readback architecture. The best mitigation is aggressive prefetching (which you partially do with the finer-clip prefetch), but the fundamental fix would be to move page allocation to the GPU.

---

### HIGH — 5. No static/dynamic page separation

**What**: UE5 separates physical pages into `static` (reused across frames, only invalidated when static geometry changes) and `dynamic` (re-rasterized every frame for moving objects). Oxygen treats all resident pages uniformly — `ResidentClean` pages are carried forward but re-rasterized whenever any caster bound changes (`global_dirty_resident_contents`).

**Why it matters**: In a typical scene, 80%+ of shadow-casting geometry is static. Without the split, you re-raster all pages when a single dynamic object moves. The `shadow_caster_content_hash` check is all-or-nothing — one moving tree forces every page dirty.

**Fix**: Track per-page which objects rasterized into them. Only invalidate pages that overlap with moved/modified objects. This is a major architectural change but gives the biggest quality-per-cost improvement after GPU-driven raster.

---

### HIGH — 6. Coarse mark shader is per-pixel brute force

**What**: `VirtualShadowCoarseMark.hlsl` dispatches one thread per screen pixel, and for each pixel loops over all coarse clip levels (`kMaxCoarseMarkClipCount`), projecting the world position into each. Every pixel does up to 4 matrix multiplications and 4 `InterlockedOr` operations.

**Why it matters**: At 1080p this is 2M threads × 4 clips = 8M projections + 8M atomics. UE5 uses groupshared cooperative marking where a group of 64 threads marks a tile's footprint collectively, with wave-level reduction before the atomic. It also uses the hierarchical page flags to skip entire subtrees.

**Fix**: Use groupshared reduction (one `InterlockedOr` per unique page per group rather than per thread), and add early-out when the pixel's clip level is coarser than the backbone begin.

---

### MEDIUM — 7. Slope bias not scaled by `fallback_lod_offset`

**What**: In `SampleDirectionalVirtualShadowClipVisibility`, when falling back to a coarser LOD, `ComputeDirectionalVirtualOptimalSlopeBias` uses the same texel size regardless of how many LODs the fallback skipped. UE5 scales its bias by `2^LODOffset` because coarser pages have proportionally larger texels.

**What happens**: Coarse fallback pages will exhibit either self-shadowing bands (bias too small) or peter-panning (if you tuned the bias up to compensate for fine pages).

**Fix**: Multiply the slope bias contribution by `pow(2, fallback_lod_offset)` for the resolved page.

---

### MEDIUM — 8. Guard texel count is hardcoded and incorrectly uniform

**What**: `kVirtualShadowMaxFilterGuardTexels = 2` is used for all clamp operations. But the tent 3x3 needs 1 guard texel and tent 5x5 needs 2. When `effective_filter_radius_texels == 1`, 2 guard texels are still reserved, wasting 2 texels of effective page resolution on every page.

Worse, if you ever increase filter radius beyond 2, the guard texels won't be sufficient and the filter will sample into adjacent pages' guard bands, getting stale or zeroed depth.

**Fix**: Pass `effective_filter_radius_texels` as the guard texel count to `ResolveDirectionalVirtualAtlasUv` rather than using the constant.

---

### MEDIUM — 9. Physical pool eviction is O(N) per allocation failure

**What**: `AcquirePhysicalTile` scans the **entire** `resident_pages` map (which can be thousands of entries) every time the free list is empty, looking for the best eviction victim. During a cold start with heavy page demand, this scan runs for every single page allocation.

**Why it matters**: With 200 pages to allocate and 200 pages to scan, this is O(N²) worst case per resolve cycle — entirely on the CPU, in the latency-sensitive resolve path.

**Fix**: Maintain an auxiliary sorted structure (priority queue / sorted set by eviction priority) instead of a full scan. Or batch eviction: evict K pages at once when the free list drops below a threshold.

---

### MEDIUM — 10. `ComputeVirtualDirectionalShadowVisibility` can sample up to 4 clip levels per pixel

**What**: The main shading function calls `SampleDirectionalVirtualShadowClipVisibility` for:
1. The selected clip
2. Optionally the finer clip (for `blend_to_finer`)
3. If neither valid: loops upward through all coarser clips
4. Optionally the next-coarser clip (for `blend_to_coarser`)

In the worst case, that's 4 full clip-level evaluations, each doing page table lookup + 9 or 25 comparison samples + bias computation.

**Why it matters**: 4 evaluations × 25 taps × page-table re-resolution per tap = **100 full page-table reads** per pixel in the worst case. This is a pixel shader running at full resolution.

**Fix**: The fallback loop should terminate more aggressively; and the blending paths should share resolved page data rather than re-computing everything independently. Also, applying fix #2 (single page-table lookup per evaluation) would reduce this to 4 comparison samples + 4 page-table reads = dramatically cheaper.

---

### LOW — 11. `VirtualShadowResolve.hlsl` uses `current_lod_valid` bit for raster scheduling

**What**: The resolve shader checks `kPageTableValidBit` (bit 28 = `OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT`) to decide whether to schedule a page for raster. But the page table is populated by the CPU before this shader runs, so this shader is essentially cross-checking CPU work on the GPU and producing a schedule that gets... read back to the CPU.

**Why it matters**: This is a CPU→GPU→CPU round-trip just to filter page table entries. The CPU already knows which pages need raster (it built `resolved_raster_pages`). The resolve pass adds a frame of latency for information the CPU already has.

---

### LOW — 12. No single-page optimization for small/distant lights

**What**: UE5 has a "single page" fast path for lights that project to <= 1 page, skipping all the multi-level clipmap machinery. Oxygen's VSM only supports directional lights currently, but when extending to spot/point, this optimization will be critical for performance with many small shadow-casting lights.

---

## IV. Summary Ranking

| Priority | Issue | Impact |
|---|---|---|
| **CRITICAL** | CPU-driven per-page raster loop | Kills draw call scalability |
| **CRITICAL** | 9-25 page-table re-resolutions per pixel in tent filter | Kills shading performance |
| **CRITICAL** | No hierarchical page flag propagation | Prevents efficient streaming/culling |
| **HIGH** | 2-3 frame feedback latency | Quality pops on camera motion |
| **HIGH** | No static/dynamic page separation | Unnecessary re-rasterization |
| **HIGH** | Coarse mark brute-force per-pixel atomics | Wasted GPU cycles |
| **MEDIUM** | Slope bias not scaled by fallback offset | Incorrect coarse shadows |
| **MEDIUM** | Guard texel count hardcoded | Wasted resolution or filter artifacts |
| **MEDIUM** | O(N) eviction scan | CPU stalls on cold start |
| **MEDIUM** | Up to 4 clip evaluations × tent filter per pixel | Worst-case pixel cost explosion |
| **LOW** | Redundant GPU resolve pass | Unnecessary latency |
| **LOW** | No single-page optimization | Future extensibility |

The **three critical items** — GPU-driven raster, single page-table lookup per filter kernel, and hierarchical page flags — should be the immediate priorities. Together they represent the architectural gap between a prototype VSM and a production-ready one.
