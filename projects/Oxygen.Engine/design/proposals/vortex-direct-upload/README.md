# Vortex direct uploads

Status: proposal

Last updated: 2026-10-30

A texture payload travels today from disk to a heap vector, from the vector to
upload staging row by row, and from staging to the GPU. This proposal removes
the CPU copies: a payload whose cooked layout is the one the GPU copy needs is
read straight into upload staging, and, where DirectStorage is available,
straight into the texture with no staging at all. It builds on the
[Serio IO proposal](../serio-io/README.md) (regions, awaitable reads, 4 KiB
payload alignment).

## Contents

- [Scenarios](#scenarios)
- [Today](#today)
- [A placement alignment bug](#a-placement-alignment-bug)
- [Design](#design)
  - [Payload regions from Content](#payload-regions-from-content)
  - [Streaming staging](#streaming-staging)
  - [Staged texture uploads](#staged-texture-uploads)
  - [DirectStorage](#directstorage)
  - [Admission and residency](#admission-and-residency)
- [Buffers and geometry](#buffers-and-geometry)
- [Debt removed](#debt-removed)
- [Tests](#tests)
- [Order](#order)
- [Open questions](#open-questions)
- [Decisions](#decisions)

## Scenarios

| Id  | Scenario                                                                                                             |
| --- | -------------------------------------------------------------------------------------------------------------------- |
| U1  | A texture bound for the first time reaches the GPU from its pak with no CPU copy of its texels                       |
| U2  | With DirectStorage, the same texture reaches the GPU with no CPU involvement in its bytes and no upload staging      |
| U3  | A burst of texture loads (a level load) is bounded in the memory it holds in flight, and never stalls the frame loop |
| U4  | A texture evicted from the GPU and needed again is read again from its pak, without a CPU copy kept for it           |

## Today

- **Loading.** `TextureLoader` allocates `std::vector<uint8_t>(size_bytes)`
  (zero-filled), reads the payload into it, and builds a
  `data::TextureResource`, which parses its `TexturePayloadHeader` and
  `SubresourceLayout[]` from the vector (`Content/Loaders/TextureLoader.h`,
  `Data/TextureResource.h`).
- **Binding.** `TextureBinder` binds lazily: it publishes a placeholder, starts
  the load, and queues a `PendingUpload` when the resource arrives, counting
  its size against a 128 MiB backlog and a 128 MiB per-frame submit limit
  (`Vortex/Resources/TextureBinder.cpp`, `TextureBinder.h:95`). An over-budget
  payload is dropped and reloaded later.
- **Uploading.** `UploadCoordinator::Submit` is synchronous: it plans the
  footprints (`PlanTexture2D`), allocates staging from a ring, zero-fills the
  whole allocation (`UploadPolicy::filler`, on by default), copies the payload
  into it row by row (`Pack2D`), records the copy on the upload queue and
  registers a fence ticket (`Vortex/Upload/UploadCoordinator.cpp`). The CPU
  payload is released once copied.
- **Staging.** `RingBufferStaging` is one persistently mapped upload buffer in
  three frame partitions, bump-allocated, reset when its frame slot recycles;
  it grows without a cap and trims after idle frames. An allocation lives
  about three frames.
- **Layout.** The cooker's D3D12 packing policy writes rows at a 256-byte
  pitch and subresources at 512-byte offsets, layer-major then mip
  (`Cooker/Import/TexturePackingPolicy.h`); the upload planner computes the
  same pitch and order (`UploadPolicy`: 256 and 512). A D3D12-packed payload
  is therefore already in the layout of the copy source; only the
  tightly-packed policy needs repacking.
- **DirectStorage** is not a dependency, and no code refers to it.

The CPU passes over every texel: the zero-fill of the vector, the copy from
the stream buffer into it, the zero-fill of staging, and the row copy into
staging.

## A placement alignment bug

D3D12 requires a texture copy's placed footprint offset to be a multiple of
512 bytes. The renderer creates its upload ring with 16-byte alignment
(`Vortex/Renderer.cpp:243`), and `SubmitTexture2D` and `SubmitTexture3D`
allocate with `Allocate`, then add the allocation's offset to each 512-aligned
region offset (`UploadCoordinator.cpp:384`, `:425`, `:479`, `:517`). A texture
upload that follows, in the same frame partition, an allocation whose size is
not a multiple of 512 copies from a misaligned footprint. The fix allocates
texture staging with `AllocateAligned(total, policy.placement_alignment)`; it
lands before this proposal, with a test that submits a texture after an odd
sized buffer upload.

## Design

### Payload regions from Content

A staged upload must know where a payload is without reading it. Content
resolves a texture to its metadata and its payload's location:

```cpp
struct TexturePayloadLocation {
  data::TextureResourceDesc desc;
  data::TexturePayloadHeader header;
  std::vector<data::SubresourceLayout> layouts;
  serio::Region texels;  // the data section, in the pak or loose data file
};
co_await loader.ResolveTexturePayload(key)  // -> Result<TexturePayloadLocation>
```

The header and layouts are a few hundred bytes, read with one awaitable read;
`texels` is a region of the source's device, so a pak payload is a range of the
pak, already 4 KiB aligned. The resource stays pinned for as long as the upload
uses its region, so an unmount waits for it.

### Streaming staging

The frame ring cannot hold a staged read: its slots recycle every frame, and a
read from disk takes longer than a frame. Staged uploads get their own
provider:

- **`StreamingStagingProvider`** suballocates large persistently mapped upload
  buffers (blocks of 64 MiB by default) with a first-fit free list, each
  allocation 512-aligned (4 KiB for an unbuffered read).
- An allocation lives from its request until its copy's fence completes; it is
  then returned. Retirement polls the upload queue's completed fence at frame
  start, as the ring does.
- **Its capacity bounds staging in flight** (U3), not frame partitions:
  `co_await provider.Allocate(size)` returns at once when a block has room,
  adds a block while the provider is under its capacity, and otherwise waits,
  in arrival order, until retirements free enough. A request larger than the
  capacity waits for an empty provider and is served alone.
- Idle blocks are freed after idle frames, like the ring's trimming.

### Staged texture uploads

A new request source, `UploadStagedRead`, carries the cooked layouts and the
payload region. `UploadCoordinator` serves it asynchronously:

1. Await an allocation of the payload size from the streaming provider,
   512-aligned.
2. `co_await texels.AsyncReadAt(...)` into the mapped staging span: the file's
   bytes land where the GPU copy reads them. Nothing zero-fills, nothing
   packs.
3. Build the copy regions from the cooked `SubresourceLayout`s
   (`buffer_offset`, `buffer_row_pitch`, `buffer_slice_pitch`) instead of
   `PlanTexture2D`, check that they equal the footprints the device reports for
   the texture, and record the copies.
4. Signal and return the ticket; the binder publishes on completion as today.

A tightly-packed payload, or any whose layouts differ from the device's
footprints, takes the existing path: read into a `serio::ByteBuffer`, then
`Pack2D` into ring staging. Cancelling the binder's task (an eviction, a
shutdown) cancels the read, and the allocation and lease return.

`TextureBinder` resolves the payload location instead of loading a
`TextureResource` for D3D12-packed textures; `PrepareTexture2DUpload` takes the
location. The default fill policy stays for the paths that pack, and is never
applied to a staged read.

### DirectStorage

DirectStorage reads a file region straight into a GPU resource: with the
cooked D3D12 layout, the destination is the texture's subresources, and no
upload staging exists at all (U2).

- **A backend of the staged path.** Where the DirectStorage runtime is present,
  a staged texture upload enqueues one request: source = the payload region's
  native file and offset (Serio exposes both), destination =
  `DSTORAGE_DESTINATION_MULTIPLE_SUBRESOURCES` of the texture, uncompressed.
  Where it is not, the streaming-staging path serves the same request.
- **Completion.** Each DirectStorage queue signals a fence after a submitted
  batch; the upload tracker registers that fence as a timeline of its own, so
  an `UploadTicket` completes the same way whichever backend served it, and the
  binder does not know which did.
- **Batching.** Requests enqueued during a frame are submitted together at
  frame start, as DirectStorage expects; a request's ticket completes with its
  batch.
- **GPU decompression** of GDeflate, the engine's codec
  ([pak compression](../pak-compression/README.md)): a compressed texture's
  stored region is enqueued with `DSTORAGE_COMPRESSION_FORMAT_GDEFLATE`; a
  stored one with `DSTORAGE_COMPRESSION_FORMAT_NONE`.
- **Dependency.** The DirectStorage SDK (`Microsoft.Direct3D.DirectStorage`)
  becomes a dependency of the D3D12 backend, its redistributable DLLs shipped
  with the engine.

### Admission and residency

- **Admission counts bytes in flight**, not decoded sizes: the staging capacity
  replaces `TextureBinder`'s `pending_upload_bytes_` backlog for staged
  uploads, so an upload waits for memory instead of being dropped and reloaded.
- **No CPU copy is kept.** A staged texture has no CPU payload once uploaded;
  re-uploading an evicted texture reads its region again (U4). The upload pins
  the content source only while it reads.

## Buffers and geometry

- **Buffer payloads** cooked GPU-ready (`data::BufferResource`) take the same
  staged path, with a buffer destination and a plain byte region.
- **Geometry** is not uploaded from a payload: `GeometryUploader` builds its
  uploads from mesh CPU arrays and widens indices to 32 bits in a temporary
  vector. Whether meshes should be cooked GPU-ready, so they can stream like
  textures, is a geometry pipeline decision; until then the geometry uploader
  keeps its path and gains only the alignment fix.

## Debt removed

| Debt                                                                   | Where                                                                            |
| ---------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| Texture staging misaligned for D3D12 placement                         | `Vortex/Upload/UploadCoordinator.cpp`                                            |
| Zero-fill of staging the copy then overwrites                          | `Vortex/Upload/UploadCoordinator.cpp` (`UploadPolicy::filler`) for staged reads  |
| Row-by-row repacking of payloads already in the copy layout            | `Vortex/Upload/TextureUploadPacking.cpp` (`Pack2D`) for D3D12-packed payloads    |
| A CPU copy of every texture payload before it reaches staging          | `Content/Loaders/TextureLoader.h`, `Data/TextureResource.h`, `TextureBinder.cpp` |
| Admission by decoded size, dropping and reloading over-budget payloads | `Vortex/Resources/TextureBinder.cpp`, `TextureBinder.h`                          |

## Tests

- **Alignment fix:** a texture submitted after an odd-sized buffer upload in
  the same partition copies from a 512-aligned footprint offset.
- **Streaming staging:** allocations are 512-aligned; a retired allocation is
  reused only after its fence; the capacity bounds the total, and waiting
  allocations are served in arrival order; idle blocks are
  freed; an allocation never crosses a block.
- **Layout check:** the copy regions built from cooked layouts equal the
  device's footprints for every supported format, array, cube and mip chain,
  including mips smaller than a block; a mismatch falls back to packing.
- **Staged upload:** a texture read through the staged path matches, texel for
  texel, the same texture uploaded through `Pack2D`; cancelling a staged upload
  mid-read returns its allocation.
- **DirectStorage backend:** the same comparison through DirectStorage, and a
  ticket that completes through the DirectStorage fence; the fallback serves
  the request when the runtime is missing.
- **Admission:** a burst of staged uploads larger than the capacity completes
  in order with staging never above the capacity, and no payload is dropped.

## Order

1. The placement alignment fix, on its own.
2. `ResolveTexturePayload` (after Serio's regions and awaitable reads).
3. `StreamingStagingProvider` with its capacity.
4. Staged texture uploads and the binder's move to them.
5. The DirectStorage dependency and backend.
6. GPU decompression of GDeflate-compressed textures, after the pak
   compression format lands.

## Open questions

- **Packaging the DirectStorage SDK:** a Conan recipe of the NuGet package, or
  vendored headers and redistributable DLLs.
- **GPU-ready geometry:** whether the geometry pipeline cooks meshes so they
  stream like textures.

## Decisions

- **The cooked layout is the copy layout.** A D3D12-packed payload is read
  where the GPU copies from; repacking stays only for payloads that need it.
- **Staging for reads outlives frames.** Staged reads use a fence-retired,
  capacity-bounded provider; the frame ring stays for per-frame data.
- **One ticket, whichever backend.** Streaming staging and DirectStorage
  complete through the same upload tracker, so binders never see the backend.
- **No CPU copy is kept for staged textures.** A texture needed again is read
  again from its pak.
