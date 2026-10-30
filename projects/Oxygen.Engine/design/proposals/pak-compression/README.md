# Pak compression

Status: proposal

Last updated: 2026-10-31

Paks can store resource data compressed, with GDeflate, so they are smaller
on disk and faster to read, and so a texture can be decompressed on the GPU.
Compression is a pak concern only: loose cooked roots and the resource
descriptors stay as they are, and a pak can be built with compression on or
off.

## Contents

- [Scenarios](#scenarios)
- [Why GDeflate](#why-gdeflate)
- [What is compressed](#what-is-compressed)
- [Modules](#modules)
- [Format](#format)
- [Building](#building)
- [Reading](#reading)
- [Integrity](#integrity)
- [Dependency](#dependency)
- [Tools](#tools)
- [Tests](#tests)
- [Order](#order)
- [Decisions](#decisions)

## Scenarios

| Id  | Scenario                                                                                                                  |
| --- | ------------------------------------------------------------------------------------------------------------------------- |
| C1  | A shipped pak is smaller, and loading it reads fewer bytes, with no change to what the runtime does with the data         |
| C2  | On D3D12, a compressed texture is decompressed on the GPU as it is read, with no CPU work on its bytes                    |
| C3  | On a backend or GPU without GPU decompression, the same pak decompresses on the CPU, in parallel                          |
| C4  | A developer builds paks without compression for fast iteration, profiling or comparison, and the runtime reads both alike |
| C5  | Data already encoded (Opus or Vorbis audio, video) is stored as it is, never compressed twice                             |

## Why GDeflate

GDeflate is DEFLATE with its bits rearranged into 32 independent sub-streams,
one per SIMD lane, in independent 64 KiB pages
([bitstream specification](https://www.ietf.org/archive/id/draft-uralsky-gdeflate-00.html)).
It is the one codec every decode path of the engine can serve:

| Path                    | Decoder                                                                                                                               |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------- |
| D3D12, any vendor       | DirectStorage's GPU decompression: a vendor metacommand, or its DirectCompute decoder on any Shader Model 6.0 GPU, or its CPU decoder |
| Vulkan (future backend) | The engine's own GDeflate compute shader in HLSL, compiled to SPIR-V by DXC, or CPU decoding; `VK_NV_memory_decompression` on NVIDIA  |
| CPU, every platform     | Microsoft's open-source reference library (Apache 2.0), tile-parallel on `ThreadPool` workers                                         |

zstd compresses better, but its entropy stages are sequential within a block,
and no vendor-neutral GPU decoder exists outside DirectStorage 1.4 on D3D12.
The method field keeps another codec possible without a format break.

## What is compressed

The pak builder decides per resource. The kind and encoding of the data set
the default; compressed bytes are kept only when they save enough.

| Data                                                                 | Default                                    |
| -------------------------------------------------------------------- | ------------------------------------------ |
| Texture data, block-compressed (BC) or uncompressed formats          | GDeflate                                   |
| Buffer data (vertex, index, raw)                                     | GDeflate                                   |
| Script bytecode and sources                                          | GDeflate                                   |
| Cooked physics data                                                  | GDeflate                                   |
| Audio data as PCM                                                    | GDeflate                                   |
| Audio data already encoded (`audio_format` Opus, Vorbis)             | Stored                                     |
| Asset descriptors, resource tables, directory, catalog, browse index | Stored (small, mapped and parsed in place) |

A resource whose compressed size does not save at least the threshold
(default 5%) is stored. `TextureResourceDesc::compression_type` is the GPU
block format (BC1, BC7) and is unrelated to storage compression.

## Modules

Each part lives where its knowledge lives
([Serio's layering](../serio-io/README.md#scope-and-layering)):

| Module                                          | Holds                                                                                                                                                                                                          |
| ----------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Compression** (`src/Oxygen/Compression`, new) | The GDeflate codec over spans: compress a buffer into its stream, decompress one page into a caller's destination, the bounds. No IO, no threads, no pak knowledge; depends on Base and the `gdeflate` package |
| **Data**                                        | The storage section and its records, their encoding and decoding, the method values                                                                                                                            |
| **Cooker** (pak builder, PakTool)               | The compression policy, compressing resources on its workers, writing storage records                                                                                                                          |
| **Content**                                     | Resolving a resource's storage record, reading the stored bytes through Serio, scheduling page decompression on `ThreadPool` workers                                                                           |
| **Vortex**                                      | Handing stored regions to DirectStorage for GPU decompression, and later the GDeflate compute shader                                                                                                           |
| **Serio**                                       | Reading and writing stored bytes; it knows nothing of the codec                                                                                                                                                |

`ARCHITECTURE.md`'s module table gains the Compression module.

## Format

Storage compression lives in the pak container, not in the resource
descriptors, so the descriptors keep their layout, loose cooked roots keep
theirs, and a descriptor's `size_bytes` stays the size of the data a consumer
receives.

- **Storage records.** For each resource table (textures, buffers, scripts,
  physics, audio) the pak holds a parallel storage table, one fixed-size
  record per resource, indexed like the resource table:

  ```cpp
  struct ResourceStorageRecord {  // packed
    uint8_t method;          // 0 stored, 1 GDeflate; other values reserved
    uint8_t _reserved[3];
    uint64_t stored_offset;  // absolute, 4 KiB aligned
    uint64_t stored_size;    // bytes on disk
  };
  ```

  The descriptor's `data_offset` is the stored offset of a stored resource; a
  reader uses the storage record for every resource, so no reader confuses
  the two.

- **The storage section** holds the storage tables, one after another, and
  the footer locates it (offset and size). With the
  [signature section](../pak-signing/README.md) it needs more than the footer's
  28 reserved bytes, so the footer grows with the format version that
  introduces both.
- **The GDeflate stream** is stored exactly as the reference library and
  DirectStorage produce and consume it, so its bytes go to either decoder
  unchanged. If that stream carries its own page index (to confirm when the
  dependency lands), the pak stores no separate block table; otherwise the
  storage record gains a block table offset.
- **Explicit, never detected.** Uncompressed data is arbitrary bytes and can
  start with anything, so a method is recorded, never guessed from a magic.
- **Versions.** The storage tables raise the pak format version
  (`PakFormatVersions.inc`). A reader refuses a method it does not know with
  `kUnsupportedVersion`, naming the resource.

## Building

- **PakTool:** `--compression gdeflate|none`, `gdeflate` by default and `none`
  to opt out; per-kind overrides such as `--compression-of audio=none`; and
  `--compression-min-savings <percent>`.
- **The planner** compresses each eligible resource on `ThreadPool` workers,
  64 KiB pages in parallel, keeps or drops the result against the threshold,
  and plans stored sizes; the writer streams the stored bytes through Serio's
  hashing stream as it does uncompressed ones.
- **Repacking** a compressed pak with a different policy decompresses and
  recompresses; repacking with the same policy copies stored bytes as they
  are.

## Reading

Content resolves a resource to its descriptor and its storage record; what
happens next depends on the destination.

- **CPU decoding (any backend).** Content reads the stored bytes with one
  awaitable Serio read into a `serio::ByteBuffer`, then schedules the pages on
  `ThreadPool` workers, where the Compression module decompresses each
  straight into the destination: the resource's own `ByteBuffer`, or upload
  staging for a staged texture. The decompression is the one CPU pass that
  writes the destination. Content's payload budget covers the stored and
  the uncompressed bytes while both are held.
- **GPU decoding on D3D12.** The [Vortex direct uploads](../vortex-direct-upload/README.md)
  DirectStorage backend enqueues the stored region with
  `DSTORAGE_COMPRESSION_FORMAT_GDEFLATE`, destination the texture's
  subresources; the CPU never touches the bytes.
- **Vulkan (future backend).** The engine's GDeflate compute shader
  decompresses into a GPU buffer that the texture is copied from, or the CPU
  path serves it. The shader is one HLSL source compiled by DXC for both
  backends, verified against the reference library byte for byte; on Vulkan it
  requests a 32-lane subgroup (`VK_EXT_subgroup_size_control`) or processes the
  32 sub-streams within the subgroup it gets.
- **Stored resources** read exactly as today.

## Integrity

- The pak CRC and the pak's SHA-256 cover the file's bytes: stored bytes,
  compressed or not.
- Descriptor `content_hash` values, catalog entry digests and requirements
  cover the uncompressed data, so a pak's identity, its requirements and its
  patch diffs do not change with the compression policy.
- A GDeflate stream that fails to decode is `kCorrupt`, naming the resource; a
  decoded size different from the descriptor's `size_bytes` is `kCorrupt`.

## Dependency

The GDeflate reference implementation (the `GDeflate` folder of
`microsoft/DirectStorage`, Apache 2.0) builds two static libraries with CMake:
its modified libdeflate and the GDeflate CPU codec
([README](https://github.com/microsoft/DirectStorage/blob/main/GDeflate/README.md)).
It is not on Conan Center, and Conan Center's `libdeflate` is not the fork it
needs. A `gdeflate` recipe in the engine's Conan recipe fork fetches the
repository at a pinned commit with its submodules, builds that folder and
packages both libraries and their headers. Only the Compression module links
it. The recipe step also confirms the codec's API and stream format before the
module is written against them.

## Tools

- **PakDump** prints, per resource, its method, stored and uncompressed sizes
  and ratio, and pak totals.
- **The build report** records the policy and the totals per kind.

## Tests

- **Format:** storage records encode and decode; an unknown method is refused
  with `kUnsupportedVersion`; stored offsets are 4 KiB aligned.
- **Policy:** per-kind defaults, overrides, encoded audio stored, the savings
  threshold keeping incompressible data stored.
- **Round trip:** every compressible kind, compressed and decompressed on the
  CPU, equals its source byte for byte, at sizes around page boundaries
  (0, 1, 64 KiB - 1, 64 KiB, 64 KiB + 1, many pages).
- **Corruption:** a damaged stream and a wrong decoded size are `kCorrupt`.
- **Integrity:** a pak built with and without compression has the same catalog
  digest and requirements and a different CRC.
- **Decoders agree:** the DirectStorage path and, when it lands, the compute
  shader produce the same bytes as the CPU decoder.

## Order

1. The `gdeflate` Conan recipe, confirmation of its API and stream format,
   and the Compression module over it, with its round-trip tests.
2. Storage records in the pak format, written uncompressed (`method` 0), with
   the version bump.
3. Compression in the pak builder, with PakTool options and the threshold.
4. CPU decoding in Content: stored bytes read through Serio, pages
   decompressed by the Compression module on workers.
5. GPU decoding through DirectStorage, with the Vortex direct-upload backend.
6. The GDeflate compute shader, with the Vulkan backend; until then the CPU
   path serves it.

## Decisions

- **GDeflate is the engine's codec.** It is the only codec with a GPU decoder
  on every D3D12 GPU and an open, GPU-designed format a portable compute shader
  can decode.
- **Compression belongs to the pak, not the resource.** Descriptors and loose
  cooked roots stay unchanged; storage records say how each resource is
  stored.
- **The method is recorded, never detected.** Raw data can look like anything.
- **Decide per resource, keep only what saves.** Already-encoded data is stored,
  and compression that does not pay its threshold is dropped.
- **Digests describe content, not storage.** Changing the compression policy
  changes no identity, requirement or patch.
- **Compression is a build choice.** A pak may be built without it, and the
  runtime reads both.
