
# Texture Import & Cooking Design (Optional BC7)

This document designs Oxygen's *image loader / texture cooker* for authoring-time import.

It is intentionally aligned with:

- `oxygen::data::pak::TextureResourceDesc` in `src/Oxygen/Data/PakFormat.h`
- the existing loose-cooked container model (`container.index.bin` + `textures.table` + `textures.data`)
- the existing append-only writing utilities (`ResourceAppender`, `TextureEmissionState`)

**This design introduces PAK format v4** — a breaking change that adds structured texture payload headers. See [PAK Format v4 (Breaking Change)](#pak-format-v4-breaking-change) for details.

The goal is to ingest common authoring image formats (via **stb_image** and **tinyexr**) and produce GPU-ready texture payloads in the loose cooked resource files.

BC7 compression is supported, but only BC7 (no BC1/3/5/6H) and it is optional per texture.

---

## Goals

- Decode: all formats supported by `stb_image` (common LDR formats + `.hdr`) plus `.exr` via `tinyexr`.
- Normalize: produce a well-defined in-memory representation inspired by DirectXTex (`Image` + `ScratchImage`).
- Derive: generate mip chains and optional transforms (color space, normal map renormalization, channel packing).
- Encode: optionally compress to **BC7 only** (UNORM or UNORM_SRGB) using the in-tree `bc7enc` implementation.
- Emit: write textures into the **existing loose cooked** resource layout:
  - `Resources/textures.table` (array of `TextureResourceDesc`)
  - `Resources/textures.data` (append-only blob file)
  - `container.index.bin` registers both files via `FileKind::kTexturesTable` / `kTexturesData`

Non-goals (for now):

- BC1/BC3/BC5/BC6H, ASTC, KTX2.
- Runtime decoding/transcoding.
- Multiplanar/video formats.
- Equirectangular → cubemap conversion (see [HDR Cubemap Assembly](#hdr-cubemap-assembly) for guidance).

---

## Terminology

- **Image**: a single 2D surface (one mip of one array layer).
- **ScratchImage**: an owning container holding the full texture (all mips, all layers) + metadata.
- **Subresource**: one `(array_layer, mip_level)` surface.
- **BC7 block**: 4x4 pixels → 16 bytes.

---

## In-memory representation (DirectXTex-inspired)

We keep the conceptual split of DirectXTex (`Image` vs `ScratchImage`) but adapt it to Oxygen's needs.

### Metadata

```cpp
enum class ColorSpace : uint8_t { kLinear, kSRGB };

enum class TextureIntent : uint8_t {
  kAlbedo,
  kNormalTS,
  kRoughness,
  kMetallic,
  kAO,
  kEmissive,
  kOpacity,
  kORMPacked,
  kHDR_Environment,
  kHDR_LightProbe,
  kData,
};

enum class MipPolicy : uint8_t {
  kNone,
  kFullChain,
  kMaxCount,
};

enum class Compression : uint8_t {
  kNone,
  kFast,
  kDefault,
  kHigh,
};

enum class MipFilter : uint8_t {
  kBox,      // 2×2 average — fast, slight aliasing on high-frequency content
  kKaiser,   // Kaiser-windowed sinc — good balance of sharpness and aliasing reduction
  kLanczos,  // Lanczos-3 — sharper than Kaiser, minor ringing artifacts
};

enum class PixelFormat : uint8_t {
  // Working (ScratchImage) surface formats.
  // These are internal representations used during cooking, not final output formats.
  kR8_UNorm,        // 1 byte/pixel  — single-channel masks
  kRG8_UNorm,       // 2 bytes/pixel — two-channel (e.g., normal XY)
  kRGBA8_UNorm,     // 4 bytes/pixel — standard LDR
  kR16_Float,       // 2 bytes/pixel — single-channel HDR
  kRG16_Float,      // 4 bytes/pixel — two-channel HDR
  kRGBA16_Float,    // 8 bytes/pixel — HDR intermediate
  kRGBA32_Float,    // 16 bytes/pixel — full-precision HDR
};

// Complete import + cook contract.
// This metadata must contain everything needed to decode, assemble, transform,
// generate mips, and choose the final stored format.
//
// Packing is NOT part of the importer contract. Packing is a cook-time strategy
// selected per target backend (D3D12/Vulkan) and expressed in the payload
// header for the runtime loader.
struct TextureMetadata {
  // Identity
  std::string source_id;

  // Shape / dimensionality
  oxygen::TextureType texture_type = oxygen::TextureType::kTexture2D;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t depth = 1;         // 3D only; otherwise 1
  uint32_t array_layers = 1;  // cube uses 6; cube-array uses 6*N

  // Semantic intent (drives validation + presets)
  TextureIntent intent = TextureIntent::kData;

  // Decode options
  bool flip_y_on_decode = false;
  bool force_rgba_on_decode = true;

  // Color and sampling policy
  ColorSpace color_space = ColorSpace::kLinear;

  // Stored sampling behavior is implied by output_format:
  // - `Format::*SRGB` means the runtime binds an sRGB view and hardware
  //   performs sRGB->linear conversion during sampling.
  // - `Format::*UNorm` means shaders must do any gamma conversion manually.

  // Normal map conventions (meaningful when intent == kNormalTS)
  bool flip_normal_green = false;
  bool renormalize_normals_in_mips = true;

  // Mip policy
  MipPolicy mip_policy = MipPolicy::kFullChain;
  uint32_t max_mip_levels = 0; // used only when mip_policy == kMaxCount

  // Mip filter selection
  MipFilter mip_filter = MipFilter::kKaiser;  // Default: good quality/performance balance
  ColorSpace mip_filter_space = ColorSpace::kLinear;

  // Output format. BC7 is inferred from the chosen output_format.
  oxygen::Format output_format = oxygen::Format::kRGBA8UNorm;
  // Compression level (for BC7 output formats only).
  Compression compression = Compression::kDefault;

  // HDR handling (relevant for .hdr/.exr inputs)
  // If true: apply exposure + tonemap and store as LDR UNorm/BC7.
  // If false: keep HDR and store as float format.
  bool bake_hdr_to_ldr = false;
  float exposure_ev = 0.0F;
  // Tonemap: when baking HDR->LDR, use ACES fitted.

};
```

Auto-derived working representation

The working representation is an internal cooker detail and is not specified by importers.
The cooker derives it deterministically from the input decoding result and `TextureMetadata`:

- **Working color space**: always linear for transforms and mip filtering.
  - If `color_space == kSRGB`, mip0 is converted sRGB→linear before mipgen.
- **Working pixel format** (ScratchImage storage):
  - HDR sources (`.hdr`/`.exr`) decode to float (`PixelFormat::kRGBA32_Float`).
  - LDR sources decode to `PixelFormat::kRGBA8_UNorm`.
  - If `bake_hdr_to_ldr == true` and the chosen `output_format` is UNorm/BC7, the cooker tonemaps/exposes in linear space then quantizes to RGBA8 before BC7.

Importers specify *intent* and *final stored format*; the cooker chooses the intermediate representation.

### `Image` view

```cpp
struct ImageView {
 uint32_t width = 0;
 uint32_t height = 0;
 PixelFormat format;
 uint32_t row_pitch_bytes = 0; // bytes between rows
 std::span<const std::byte> pixels; // size = row_pitch_bytes * height (for uncompressed)
};
```

### `ScratchImage` owner

```cpp
class ScratchImage {
public:
 const TextureMetadata& Meta() const;
 ImageView GetImage(uint16_t array_layer, uint16_t mip) const;
 // Alloc / initialize helpers, mipgen helpers, etc.
private:
 TextureMetadata meta_;
 std::vector<std::byte> storage_;
 // plus per-subresource offsets and row pitches when needed.
};
```

**Rule of thumb**:

- Decoders produce a `ScratchImage` with **one mip** (mip 0), `array_layers=1`.
- Mip generation produces additional subresources.
- The BC7 encoder produces an encoded blob (not a `ScratchImage`), because the final on-disk layout is already “GPU upload ready”.

---

## Decode layer (stb_image + tinyexr)

### Supported inputs

- `stb_image`: typical LDR formats (png/jpg/tga/bmp/psd/gif/pic/pnm…), plus `.hdr`.
- `tinyexr`: `.exr` (half/float channels, scanline or tiled).

### Decoder API

Provide a single entry point that chooses the backend:

```cpp
struct DecodeOptions {
 bool flip_y = false;
 // If true, request 4 channels (RGBA) always.
 bool force_rgba = true;
};

// Error taxonomy for texture decoding and cooking.
enum class TextureError {
  kSuccess = 0,
  // Decode errors
  kUnsupportedFormat,       // File format not recognized or unsupported
  kCorruptedData,           // File data is malformed or truncated
  kDecodeFailed,            // Backend (stb_image/tinyexr) returned an error
  kOutOfMemory,             // Allocation failed during decode
  // Validation errors
  kInvalidDimensions,       // Width/height/depth is zero or exceeds limits
  kDimensionMismatch,       // Array/cube inputs have inconsistent dimensions
  kArrayLayerCountInvalid,  // Cube requires multiple of 6 layers
  kDepthInvalidFor2D,       // depth > 1 for non-3D texture type
  // Cook errors
  kMipGenerationFailed,     // Mip chain generation failed
  kCompressionFailed,       // BC7 encoding failed
  kOutputFormatInvalid,     // Requested format incompatible with input/intent
  kHdrRequiresFloatFormat,  // HDR input without bake_hdr_to_ldr needs float output
  // I/O errors
  kFileNotFound,
  kFileReadFailed,
  kWriteFailed,
};

Result<ScratchImage, TextureError> DecodeToScratchImage(
    std::span<const std::byte> bytes,
    std::string_view source_id,
    const DecodeOptions& options);
```

Selection:

- If the file extension is `.exr` *or* the byte signature matches EXR, decode via tinyexr.
- Otherwise decode via stb_image.

Output policy:

- LDR inputs → `PixelFormat::kRGBA8_UNorm`.
- HDR inputs (`.hdr`, `.exr`) → decode to float (`PixelFormat::kRGBA32_Float`) first.

### HDR to BC7 constraint

BC7 is UNORM (8-bit endpoints). For HDR sources, `TextureMetadata` must explicitly choose one of these policies:

- **Keep HDR** (`bake_hdr_to_ldr = false`): store uncompressed float (recommended default: `Format::kRGBA16Float`).
- **Bake to LDR** (`bake_hdr_to_ldr = true`): apply exposure + tonemap + (optional) gamma encode, then quantize to RGBA8 and optionally compress to BC7.

Default (no open questions):

- If output format is `Format::kRGBA16Float` or `Format::kRGBA32Float`: store linear float values (no tonemap).
- If output format is UNorm or BC7:
  - apply `exposure_ev` (default: 0.0)
  - tonemap with **ACES fitted** (default)
  - if storing as `*SRGB`, apply linear→sRGB encode before quantization
  - clamp to [0..1] and quantize to 8-bit

---

## Mip generation

Mip generation should be deterministic and match runtime expectations.

### Mip count

Default is “full chain”:

```cpp
mips = floor(log2(max(w, h))) + 1
```

Allow overrides:

- `mip_levels = 1` (no mips)
- `max_mip_levels = N`

### Filter

The `mip_filter` field in `TextureMetadata` selects the downsample kernel:

| Filter | Kernel | Quality | Performance | Use case |
| ------ | ------ | ------- | ----------- | -------- |
| `kBox` | 2×2 average | Lowest — visible aliasing on high-frequency content | Fastest | Previews, masks where aliasing is acceptable |
| `kKaiser` | Kaiser-windowed sinc (α=4, width=6) | Good — minimal aliasing, slight blur | Moderate | **Default** — general-purpose textures |
| `kLanczos` | Lanczos-3 (a=3, width=6) | Best — sharp, minor ringing on hard edges | Slowest | High-quality final assets, UI, text |

All filtering is performed in **linear** space.

#### Filter kernel implementation notes

```cpp
// Kaiser window: I₀(α * sqrt(1 - x²)) / I₀(α), where I₀ is modified Bessel function
// α = 4.0 provides a good stopband attenuation (~40 dB)
float KaiserWindow(float x, float alpha = 4.0f) {
  if (std::abs(x) >= 1.0f) return 0.0f;
  float arg = alpha * std::sqrt(1.0f - x * x);
  return BesselI0(arg) / BesselI0(alpha);
}

// Lanczos kernel: sinc(x) * sinc(x/a) for |x| < a, else 0
float Lanczos(float x, float a = 3.0f) {
  if (std::abs(x) < 1e-6f) return 1.0f;
  if (std::abs(x) >= a) return 0.0f;
  float pi_x = std::numbers::pi_v<float> * x;
  return (std::sin(pi_x) / pi_x) * (std::sin(pi_x / a) / (pi_x / a));
}
```

For 2D textures, apply the 1D kernel separably (horizontal then vertical).
For 3D textures, apply separably in all three dimensions.

Color space rules:

- For `ColorSpace::kSRGB` sources:
  - convert mip0 from sRGB→linear before filtering
  - filter in linear
  - convert each mip back to sRGB *only when storing as RGBA8 UNorm* for subsequent BC7_sRGB encoding

### Content-type-specific mip handling

The cooker applies special processing based on `TextureIntent`:

- **Normal maps** (`intent == kNormalTS`):
  - Decode XY channels as linear (Z is reconstructed at runtime if stored as 2-channel).
  - Apply selected `mip_filter` in linear space.
  - If `renormalize_normals_in_mips == true`: after filtering each mip, renormalize each texel to unit length.

  ```cpp
  // Per-texel renormalization after mip filtering
  float3 n = filtered_normal;
  float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);
  if (len > 1e-6f) n /= len;
  ```

  - Rationale: averaging normals shortens them; renormalization preserves correct lighting.

- **Single-channel masks** (`intent == kRoughness`, `kMetallic`, `kAO`, `kOpacity`):
  - Treat as linear data.
  - Apply selected `mip_filter` directly (no color space conversion).

- **ORM packed** (`intent == kORMPacked`):
  - Each channel is independent linear data.
  - Apply `mip_filter` to each channel independently (standard RGBA filtering works).

- **HDR content** (`intent == kHDR_Environment`, `kHDR_LightProbe`):
  - Filter in linear float space.
  - No clamping during filtering (preserve HDR range).
  - Tonemap only after mipgen if `bake_hdr_to_ldr == true`.

### 3D texture mip generation

For `TextureType::kTexture3D`, mip generation downsamples in all three dimensions using the selected `mip_filter`:

- **Box filter** (`kBox`): 2×2×2 average (8 voxels → 1 voxel).
- **Kaiser/Lanczos** (`kKaiser`, `kLanczos`): separable 1D filter applied in X, Y, Z order.

Dimensions halve in width, height, and depth per mip level (each clamped to ≥1).
When a dimension reaches 1, subsequent mips sample only the remaining dimensions.

```cpp
// 3D mip dimensions
for (uint32_t m = 0; m < mip_levels; ++m) {
  w_m = std::max(1u, w_0 >> m);
  h_m = std::max(1u, h_0 >> m);
  d_m = std::max(1u, d_0 >> m);
}

// 3D separable filter (pseudocode for Kaiser/Lanczos)
void GenerateMip3D(const Volume& src, Volume& dst, MipFilter filter) {
  // Pass 1: filter along X into temp1
  // Pass 2: filter along Y into temp2
  // Pass 3: filter along Z into dst
  // Each pass uses the 1D kernel from MipFilter
}

// 3D box filter (optimized path for kBox)
auto BoxSample3D = [](const Volume& src, uint32_t x, uint32_t y, uint32_t z) {
  float4 sum = {0, 0, 0, 0};
  for (uint32_t dz = 0; dz < 2; ++dz)
    for (uint32_t dy = 0; dy < 2; ++dy)
      for (uint32_t dx = 0; dx < 2; ++dx)
        sum += src.Sample(std::min(x + dx, src.w - 1),
                          std::min(y + dy, src.h - 1),
                          std::min(z + dz, src.d - 1));
  return sum / 8.0f;
};
```

The same linear-space filtering rules apply: convert sRGB → linear before filtering if needed.

---

## BC7 encoding (bc7enc)

### Encoder responsibilities

Input: a `ScratchImage` containing RGBA8 mips (or float mips that have been mapped to RGBA8).

Output: a single contiguous byte buffer containing *all subresources* in a deterministic order and packed according to a selected packing policy.

### bc7enc API notes

- `bc7enc_compress_block_init()` must be called **exactly once** before encoding blocks.
- `bc7enc_compress_block(void* out16, const void* rgba16px, const bc7enc_compress_block_params*)` encodes one 4x4 RGBA block.

#### Thread safety

`bc7enc_compress_block_init()` writes global lookup tables and is **not thread-safe**.
Call it once during application/cooker startup (e.g., in a `std::call_once` or static initializer) before spawning worker threads.
After initialization, `bc7enc_compress_block()` is reentrant and can be called concurrently from multiple threads with independent `bc7enc_compress_block_params` instances.

```cpp
// Example: thread-safe one-time initialization (C++11 and later)
inline void EnsureBc7EncInitialized() noexcept {
  static std::once_flag flag;
  std::call_once(flag, [] { bc7enc_compress_block_init(); });
}
```

Parameters:

- For color textures (especially sRGB): use perceptual weights (`bc7enc_compress_block_params_init_perceptual_weights`).
- For linear data (normals/masks): prefer linear weights.
- Always enable alpha handling; bc7enc returns whether block had alpha < 255.

Quality tiers (design-time knobs):

- Fast: lower `m_max_partitions`, `m_uber_level=0`
- Default: moderate partitions, `m_uber_level=1`
- High: max partitions, `m_uber_level=2..4` (slower)

### Block iteration and edge handling

For each subresource `(layer, mip)`:

- Compute block dimensions:
  - `blocks_x = max(1, (width + 3) / 4)`
  - `blocks_y = max(1, (height + 3) / 4)`
- For each block, gather a 4x4 RGBA tile from the source mip.
- If the block touches edges, replicate border texels to fill the 4x4 tile (deterministic).

### BC7 tail mip optimization

For mips smaller than 4×4, a full BC7 block (16 bytes) still encodes the entire mip, but most of the block is padding.
This is correct per the BC7 spec, but wastes ~50% of storage on tail mips.

**Design decision:** Accept the overhead for simplicity. Rationale:

- Tail mips contribute negligible bytes relative to mip 0 (geometric series).
- A 1×1 mip costs 16 bytes; a 2048² BC7 texture's mip 0 alone is ~4 MiB.
- Keeping a uniform BC7 layout simplifies the runtime uploader.

**Optional future optimization:** Store tail mips (width < 4 or height < 4) as uncompressed RGBA8 and set `TexturePayloadFlags::kTailMipsUncompressed`.
The runtime loader would then upload those mips via a separate uncompressed path.
This is not implemented initially but the flag is reserved for future use.

### GPU upload packing (policy/strategy)

The importer/cooker is authoring-time.
The runtime component is the **loader/uploader**, which reads the cooked payload and performs API-specific buffer→image copies.

We make packing an explicit **policy/strategy** so the cooker can emit different layouts for D3D12 vs Vulkan without guessing at runtime.

At a high level:

- The cooker always produces canonical subresources in canonical order.
- A `ITexturePackingPolicy` chooses row pitch rules, per-subresource placement alignment, and optionally emits a per-subresource layout table.
- The cooked payload begins with a small header so the runtime loader knows how to interpret bytes.

#### Packing policy interface (design sketch)

```cpp
enum class TexturePackingPolicyId : uint8_t {
  kD3D12 = 1,
  kTightPacked = 2,  // Minimal alignment; usable as fallback for Vulkan or tools
};

struct SubresourceLayout {
  uint32_t offset_bytes = 0;     // from start of payload data section (after header)
  uint32_t row_pitch_bytes = 0;  // bytes between rows (or block-rows for BC formats)
  uint32_t size_bytes = 0;       // total bytes of this subresource
};

class ITexturePackingPolicy {
public:
  virtual ~ITexturePackingPolicy() = default;
  virtual TexturePackingPolicyId Id() const noexcept = 0;
  virtual uint32_t AlignRowPitchBytes(uint32_t row_bytes, oxygen::Format fmt) const noexcept = 0;
  virtual uint32_t AlignSubresourceOffset(uint32_t running_offset, oxygen::Format fmt) const noexcept = 0;
};
```

#### Required payload header (runtime loader contract)

Because packing varies by backend, the runtime loader must not infer offsets/pitches from hard-coded constants.
Instead, each cooked texture payload starts with a header that describes the packed layout.

> **Relationship to `TextureResourceDesc`:**
>
> The existing `oxygen::data::pak::v3::TextureResourceDesc` (40 bytes, defined in `PakFormat.h`) is the **table entry** stored in `textures.table`. It contains:
>
> - `data_offset` / `size_bytes`: location of the payload in `textures.data`
> - `texture_type`, `format`, `width`, `height`, `depth`, `array_layers`, `mip_levels`: texture metadata
> - `content_hash`: deduplication key
>
> The `TexturePayloadHeader` below is a **new header stored at the start of each texture's payload data** (inside `textures.data`). It describes the internal layout of subresources within that payload.

---

### PAK Format v4 (Breaking Change)

This texture import design introduces **PAK format version 4**. The new structures below must be added to `PakFormat.h` in a new `oxygen::data::pak::v4` namespace.

**Summary of v4 changes:**

| Change | Description |
| ------ | ----------- |
| `TexturePayloadHeader` | New 28-byte header at the start of each texture payload in `textures.data` |
| `SubresourceLayout` | New 12-byte per-subresource layout descriptor |
| `TexturePackingPolicyId` | New enum identifying packing strategy (D3D12, TightPacked) |
| `TexturePayloadFlags` | New flags enum for extensibility (premultiplied alpha, tail mips) |

**Required additions to `PakFormat.h`:**

```cpp
namespace oxygen::data::pak::v4 {

using namespace v3;  // Inherit all v3 types

//=== Version Notes ===------------------------------------------------------//

//! PAK file format v4.
/*!
 v4 introduces structured texture payloads with explicit subresource layouts.
 Texture data in `textures.data` now begins with a `TexturePayloadHeader`
 followed by a `SubresourceLayout` array, then raw subresource bytes.

 This enables backend-specific packing policies (D3D12 alignment, Vulkan tight)
 without runtime recomputation of offsets.
*/

//! Scene asset descriptor version for PAK v4.
constexpr uint8_t kSceneAssetVersion = v3::kSceneAssetVersion;

//=== PAK Header (v4) ===----------------------------------------------------//

#pragma pack(push, 1)
struct PakHeader {
  char magic[8] = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };
  uint16_t version = 4;  // Format version
  uint16_t content_version = 0;
  uint8_t guid[16] = {};
  uint8_t reserved[228] = {};
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 256);

//=== Texture Payload Structures (v4) ===------------------------------------//

//! Packing policy identifier stored in texture payload headers.
enum class TexturePackingPolicyId : uint8_t {
  kD3D12 = 1,       //!< D3D12 alignment: 256-byte row pitch, 512-byte subresource
  kTightPacked = 2, //!< Minimal alignment; suitable for Vulkan or offline tools
};

//! Extensibility flags for texture payloads.
enum class TexturePayloadFlags : uint8_t {
  kNone = 0,
  kPremultipliedAlpha = 1 << 0,   //!< Alpha is pre-multiplied
  kTailMipsUncompressed = 1 << 1, //!< Mips < 4×4 stored uncompressed (BC7 opt)
  // Bits 2-7 reserved.
};

//! Per-subresource layout descriptor within a texture payload.
/*!
 Stored as an array immediately after `TexturePayloadHeader`.
 Subresources are ordered: layer 0 mips 0..N-1, layer 1 mips 0..N-1, ...
*/
#pragma pack(push, 1)
struct SubresourceLayout {
  uint32_t offset_bytes = 0;     //!< From start of payload data section
  uint32_t row_pitch_bytes = 0;  //!< Bytes between rows (or block-rows for BC)
  uint32_t size_bytes = 0;       //!< Total bytes of this subresource
};
#pragma pack(pop)
static_assert(sizeof(SubresourceLayout) == 12);

//! Magic value for texture payload headers: 'OTX1' as little-endian.
inline constexpr uint32_t kTexturePayloadMagic = 0x3158544F;

//! Header at the start of each texture's payload data in `textures.data`.
/*!
 The runtime loader reads this header to determine subresource layout without
 recomputing offsets/pitches. Allows backend-specific packing policies.

 Layout:
   [TexturePayloadHeader]                          // 28 bytes
   [SubresourceLayout × subresource_count]         // 12 bytes each
   [raw subresource data...]                       // as described by layouts

 @note `layouts_offset_bytes` is typically `sizeof(TexturePayloadHeader)`.
 @note `data_offset_bytes` is `layouts_offset_bytes + 12 * subresource_count`.
*/
#pragma pack(push, 1)
struct TexturePayloadHeader {
  uint32_t magic = kTexturePayloadMagic;
  uint16_t version = 1;            //!< Payload header version (currently 1)
  uint8_t packing_policy = 0;      //!< TexturePackingPolicyId
  uint8_t flags = 0;               //!< TexturePayloadFlags

  uint16_t mip_levels = 0;
  uint16_t array_layers = 0;
  uint32_t subresource_count = 0;  //!< array_layers × mip_levels

  uint32_t layouts_offset_bytes = 0;  //!< From start of payload
  uint32_t data_offset_bytes = 0;     //!< From start of payload
};
#pragma pack(pop)
static_assert(sizeof(TexturePayloadHeader) == 28);

} // namespace oxygen::data::pak::v4

namespace oxygen::data::pak {
//! Default namespace alias for latest version of the PAK format
using namespace v4;
} // namespace oxygen::data::pak
```

> **Breaking change implications:**
>
> - **PAK version bump**: `PakHeader::version` changes from `3` to `4`.
> - **Binary incompatibility**: v4 PAK files cannot be read by v3 loaders; v3 PAK files can be read by v4 loaders (with fallback logic).
> - **Texture data format change**: texture payloads now start with `TexturePayloadHeader` instead of raw pixel/block data.
>
> **Affected tools requiring updates:**
>
> | Tool | Required changes |
> | ---- | ---------------- |
> | **PakGen** | Emit `TexturePayloadHeader` + `SubresourceLayout[]` when writing texture payloads. Select `ITexturePackingPolicy` based on target platform. Bump output version to 4. |
> | **PakDump** | Parse and display `TexturePayloadHeader` fields, decode `packing_policy` and `flags`, list subresource layouts. Handle v3/v4 version detection. |
> | **Inspector** | Visualize texture payload structure, show per-subresource offsets/pitches, validate magic and version. Support both v3 (legacy) and v4 layouts. |
> | **Runtime Loader** | Detect payload header magic; if present, use layout table; otherwise fall back to v3 implicit layout computation. |
>
> **Migration path:**
>
> 1. Tools emit v4 PAK files with `TexturePayloadHeader`.
> 2. Runtime loader checks for `kTexturePayloadMagic` at `data_offset`; if found, use v4 path.
> 3. If magic not found (legacy v3 texture), compute layouts implicitly (backward compatibility).

The cooker writes this header + layout table; the runtime loader reads it and performs the correct copy path for D3D12 or Vulkan.

#### Default policies

- **D3D12PackingPolicy** (upload-friendly; matches D3D12 copy-from-buffer requirements):
  - constants:
    - `kRowPitchAlignmentBytes = 256` (matches `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`)
    - `kSubresourcePlacementAlignmentBytes = 512` (matches `D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT`)
  - `AlignRowPitchBytes(row_bytes, fmt) = AlignUp(row_bytes, kRowPitchAlignmentBytes)`
  - `AlignSubresourceOffset(running, fmt) = AlignUp(running, kSubresourcePlacementAlignmentBytes)`
- **TightPackedPolicy** (minimal alignment; works for Vulkan and offline tools):
  - uncompressed: `AlignRowPitchBytes(row_bytes, fmt) = row_bytes`
  - BC7: `AlignRowPitchBytes(row_bytes, fmt) = row_bytes` (already block-row aligned)
  - `AlignSubresourceOffset(running, fmt) = AlignUp(running, 4)` (conservative for buffer offsets)

These are defaults; the point is the **strategy**, not the exact constants.

> **Note:** Vulkan has no strict row-pitch alignment requirement for buffer→image copies, so `TightPackedPolicy` is suitable. If a future Vulkan extension imposes alignment, add a dedicated policy.

For D3D12, the 256/512 alignment is a property of the D3D12 packing policy, not an engine-wide invariant.

#### Mip dimensions

For base dimensions $(w_0, h_0, d_0)$, mip $m$ dimensions are:

$$
\begin{aligned}
w_m &= \max(1, \lfloor w_0 / 2^m \rfloor) \\
h_m &= \max(1, \lfloor h_0 / 2^m \rfloor) \\
d_m &= \max(1, \lfloor d_0 / 2^m \rfloor) \\
\end{aligned}
$$

#### Subresource order inside a texture payload

We use a single canonical ordering for all texture types:

1. array layer 0: mip 0, mip 1, ..., mip N-1
2. array layer 1: mip 0, mip 1, ..., mip N-1
3. ...

This matches the common D3D “subresource index = mip + layer*mip_levels” convention.

Notes:

- For `TextureType::kTexture2D`, `array_layers = 1`.
- For `TextureType::kTexture2DArray`, `array_layers = N`.
- For `TextureType::kTextureCube`, `array_layers = 6` (one per face).
- For `TextureType::kTextureCubeArray`, `array_layers = 6 * cube_count`.
- For `TextureType::kTexture3D`, `array_layers = 1` and each mip subresource contains `d_m` slices.

#### Per-subresource byte layout (policy-driven)

We store each subresource as tightly as the selected policy allows, but always record its layout in the payload header.

Uncompressed formats (examples: `Format::kR8UNorm`, `kRG8UNorm`, `kRGBA8UNorm`, `kRGBA16Float`, `kRGBA32Float`):

- `bytes_per_pixel` depends on `format`
- `row_bytes = w_m * bytes_per_pixel`
- `row_pitch = packing.AlignRowPitchBytes(row_bytes, format)`
- `slice_bytes = row_pitch * h_m`

Reference `bytes_per_pixel` (uncompressed formats we expect to cook initially):

| Format | bytes_per_pixel |
| --- | ---: |
| `Format::kR8UNorm` | 1 |
| `Format::kRG8UNorm` | 2 |
| `Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` | 4 |
| `Format::kR16Float` | 2 |
| `Format::kRG16Float` | 4 |
| `Format::kRGBA16Float` | 8 |
| `Format::kRGBA32Float` | 16 |

BC7 formats (`Format::kBC7UNorm`, `Format::kBC7UNormSRGB`):

- `blocks_x = max(1, (w_m + 3) / 4)`
- `blocks_y = max(1, (h_m + 3) / 4)`
- `row_bytes = blocks_x * 16`
- `row_pitch = packing.AlignRowPitchBytes(row_bytes, format)`
- `slice_bytes = row_pitch * blocks_y`

For 3D textures (`TextureType::kTexture3D`), the per-mip subresource stores `d_m` slices back-to-back:

- `subresource_bytes = slice_bytes * d_m`

The policy may or may not align row pitch; do not assume any specific multiple at runtime. The runtime loader uses the recorded `row_pitch_bytes`.

#### Total payload size

Total payload bytes include header + layout table + packed subresource bytes.

Compute (and record) offsets with a single deterministic walk:

- `running = 0` (offset within the payload data section)
- for each subresource in canonical order:
  - `offset = packing.AlignSubresourceOffset(running, format)`
  - emit `SubresourceLayout{ offset, row_pitch, subresource_bytes }`
  - `running = offset + subresource_bytes`
- `data_size_bytes = running`

Then:

- `payload_size_bytes = sizeof(TexturePayloadHeader) + sizeof(SubresourceLayout) * subresource_count + data_size_bytes`
- `TextureResourceDesc::size_bytes = payload_size_bytes`

---

## Scenario matrix (dimensionality × mips × slices × format)

This section validates the design across the common authoring scenarios.

### Format selection (sRGB vs UNorm)

Pick the stored `format` based on *how the shader interprets the texels*:

- Color/albedo/base-color/emissive/UI authored in sRGB → store as `Format::kRGBA8UNormSRGB` or `Format::kBC7UNormSRGB`.
- Data textures sampled as linear values (normals, roughness/metalness/AO/masks, height, motion vectors, LUTs) → store as `Format::kRGBA8UNorm` or `Format::kBC7UNorm`.

Rule of thumb: if the shader expects gamma-correct sampling, use the `*SRGB` format; otherwise use the linear `*UNorm` format.

Note: Oxygen can support either policy, but the choice must be consistent engine-wide:

- **Hardware sRGB sampling**: bind textures as `*SRGB` and treat sampled values as linear in shaders.
- **Manual conversion**: bind textures as `*UNorm` and explicitly call `SrgbToLinear()` in shaders for color textures.

The current Forward shading path contains comments indicating manual conversion for base-color sampling; if that remains true, prefer storing base-color as `Format::kRGBA8UNorm` / `Format::kBC7UNorm` and keep the conversion in shader.

### 2D textures

- **Single image file → 2D texture**
  - `texture_type = TextureType::kTexture2D`
  - `array_layers = 1`
  - `mip_levels = 1` or full chain
  - `format = Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` or `Format::kBC7UNorm` / `Format::kBC7UNormSRGB`
  - typical: albedo/base-color/emissive/UI → `*SRGB`; masks/normals/roughness/metalness/AO → `*UNorm`
  - payload order: layer 0, mips 0..N-1

- **Single image file → 2D texture with mips**
  - same as above, but `mip_levels = N` and payload size is the sum of each mip’s `slice_bytes`

### 2D texture arrays

- **Multiple image files → 2D texture array**
  - `texture_type = TextureType::kTexture2DArray`
  - `array_layers = N` (one per file)
  - `mip_levels` is either 1 or generated (same for all layers)
  - `format = Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` or `Format::kBC7UNorm` / `Format::kBC7UNormSRGB`
  - typical: per-layer sprite sheets / UI atlases → `*SRGB`; per-layer material masks → `*UNorm`
  - payload order: layer 0 mips, layer 1 mips, ...

### Cube maps and cube arrays

- **6 image files → cube map**
  - `texture_type = TextureType::kTextureCube`
  - `array_layers = 6`
  - `mip_levels` is either 1 or generated
  - `format = Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` or `Format::kBC7UNorm` / `Format::kBC7UNormSRGB`
  - typical: skybox/background → `*SRGB`; IBL/prefiltered/specular-diffuse convolution data → `*UNorm`
  - payload order: face 0 mips, face 1 mips, ... (faces are represented as array layers)

- **6×N image files → cube map array**
  - `texture_type = TextureType::kTextureCubeArray`
  - `array_layers = 6 * N`
  - `mip_levels` is either 1 or generated
  - `format = Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` or `Format::kBC7UNorm` / `Format::kBC7UNormSRGB`
  - typical: multiple skyboxes → `*SRGB`; probe data / reflection capture arrays → usually `*UNorm`
  - payload order: cube 0 faces+mips, cube 1 faces+mips, ... (still just array-layer order)

Face ordering must be defined by the importer (recommended): `+X, -X, +Y, -Y, +Z, -Z`.

#### Oxygen cubemap orientation (impact of engine conventions)

Oxygen world-space conventions (from `oxygen::space`):

- Right-handed
- Z-up
- Forward = -Y
- Right = +X

Cubemap faces are defined by **world axes**, not by “front/back” naming.
Using the recommended face order above:

- `+X` points to world Right
- `-X` points to world Left
- `+Y` points to world Back
- `-Y` points to world Forward
- `+Z` points to world Up
- `-Z` points to world Down

Practical consequence: if an authoring tool calls a face “front”, in Oxygen that usually maps to `-Y` (Forward).

Sky rendering confirms the Z-up contract: the SkySphere shader rotates skybox sampling **around +Z** (world up).
Any cubemap import/conversion (equirectangular → cube, tool-specific face exports, etc.) must produce texels such that sampling `TextureCube.Sample(rotated_dir)` returns the radiance from that **world-space direction**.

### HDR cubemap assembly

Equirectangular → cubemap conversion is **out of scope** for the initial texture cooker implementation.
This section documents the expected workflow for authoring HDR environment cubemaps.

#### Recommended workflow (external tooling)

1. **Author HDR panorama** — Create or acquire an equirectangular `.hdr` or `.exr` panorama.
2. **Convert externally** — Use a dedicated tool to convert to 6 face images:
   - [cmft](https://github.com/dariomanesku/cmft) (cross-platform, CLI)
   - [cmgen](https://github.com/AcademySoftwareFoundation/MaterialX) (Filament's tool)
   - Blender / Substance / Photoshop plugins
3. **Export faces** — Save as 6 separate images named by face (`+X.exr`, `-X.exr`, etc.) or a single vertical/horizontal cross layout.
4. **Import as cubemap** — Use the texture cooker's cubemap manifest to assemble the 6 faces.

#### In-engine equirectangular-to-cubemap converter

For workflows that require direct `.hdr`/`.exr` panorama import without external tools, the cooker provides a built-in converter.

```cpp
// Equirectangular → cubemap conversion API
struct EquirectToCubeOptions {
  uint32_t face_resolution = 1024;  // Output face size (width = height)
  MipFilter sample_filter = MipFilter::kLanczos;  // Filter for sampling equirect
  bool generate_mips = true;
};

Result<ScratchImage, TextureError> ConvertEquirectangularToCube(
    const ScratchImage& equirect,
    const EquirectToCubeOptions& options);
```

**Algorithm:**

For each of the 6 faces, for each texel (u, v) in [0, face_resolution)²:

1. Compute the 3D direction vector from face index and normalized (u, v).
2. Convert direction to spherical coordinates (θ, φ).
3. Map spherical coordinates to equirectangular UV:

   ```cpp
   float theta = std::atan2(dir.y, dir.x);  // [-π, π]
   float phi = std::asin(std::clamp(dir.z, -1.0f, 1.0f));  // [-π/2, π/2]
   float equirect_u = (theta / std::numbers::pi_v<float> + 1.0f) * 0.5f;
   float equirect_v = (phi / (std::numbers::pi_v<float> * 0.5f) + 1.0f) * 0.5f;
   ```

4. Sample the equirectangular image using the selected `sample_filter` (bilinear for `kBox`, bicubic for `kKaiser`/`kLanczos`).
5. Store the sampled value in the output face.

**Face direction vectors** (Oxygen Z-up, RH conventions):

| Face | Direction at center | Right | Up |
| ---- | ------------------- | ----- | -- |
| +X | (+1, 0, 0) | (0, +1, 0) | (0, 0, +1) |
| -X | (-1, 0, 0) | (0, -1, 0) | (0, 0, +1) |
| +Y | (0, +1, 0) | (-1, 0, 0) | (0, 0, +1) |
| -Y | (0, -1, 0) | (+1, 0, 0) | (0, 0, +1) |
| +Z | (0, 0, +1) | (+1, 0, 0) | (0, -1, 0) |
| -Z | (0, 0, -1) | (+1, 0, 0) | (0, +1, 0) |

The converter produces a `ScratchImage` with `texture_type = kTextureCube`, `array_layers = 6`, ready for standard mip generation and BC7 encoding.

### 3D textures

- **Volume source → 3D texture**
  - `texture_type = TextureType::kTexture3D`
  - `array_layers = 1`
  - `depth = d_0` (number of slices)
  - `mip_levels` is either 1 or generated
  - `format = Format::kRGBA8UNorm` / `Format::kRGBA8UNormSRGB` or `Format::kBC7UNorm` / `Format::kBC7UNormSRGB`
  - typical: density/temperature/fog volumes → `*UNorm` (linear); 3D LUTs often `*UNorm` (but treat as data)
  - payload order: mip 0 slices 0..d0-1, then mip 1 slices 0..d1-1, ...

---

## Loose cooked output layout (index + textures.table + textures.data)

Oxygen already has a loose cooked container model:

- `container.index.bin` stores:
  - asset entries (materials/geometry/scenes)
  - file records for resource blobs
- resource blob files live under `Resources/` by convention

### Files

- `Resources/textures.table`: array of `oxygen::data::pak::TextureResourceDesc`.
- `Resources/textures.data`: append-only raw payload blobs.

Both must be registered in the index as:

- `oxygen::data::loose_cooked::v1::FileKind::kTexturesTable`
- `oxygen::data::loose_cooked::v1::FileKind::kTexturesData`

### `TextureResourceDesc` field semantics (loose cooked)

Even though `TextureResourceDesc::data_offset` is described as “absolute offset” in PAK context, in **loose cooked** we interpret it as:

- **absolute offset from the start of `textures.data`**

This is consistent with the existing append-only writer (`ResourceAppender`) and with how `LoadTextureResource()` seeks into the data stream.

Field usage:

- `data_offset`: offset in `textures.data` returned by the appender
- `size_bytes`: total byte size of this texture’s payload (all subresources)
- `texture_type`: one of `kTexture2D`, `kTexture2DArray`, `kTextureCube`, `kTextureCubeArray`, `kTexture3D`
- `compression_type`: currently `0` (reserved; runtime can key off `format`)
- `width`, `height`, `depth`, `array_layers`, `mip_levels`: final cooked dimensions
- `format`: the final stored format (for example `Format::kRGBA8UNorm`, `Format::kRGBA8UNormSRGB`, `Format::kBC7UNorm`, `Format::kBC7UNormSRGB`)
- `alignment`: 256
- `content_hash`: first 8 bytes of SHA256 over the *stored payload bytes* (dedup/incremental)

Runtime loader contract:

- `TextureResourceDesc::data_offset`/`size_bytes` gives the byte range in `textures.data`.
- The payload begins with `TexturePayloadHeader` + layout table (PAK v4).
- The runtime loader uses `packing_policy` + `SubresourceLayout[]` to drive API-specific uploads.

### Fallback texture (table index 0)

The engine reserves texture index 0 as the fallback.

Recommendation: keep the fallback as a 1x1 white texture encoded as BC7 (small, GPU-friendly), but it can also be stored uncompressed if needed.
Implementation detail for BC7: encode a 4x4 block of white RGBA and store it as a 1-block BC7 payload.

---

## Incremental cooking and deduplication

We preserve the existing incremental strategy used by the current RGBA8 emitter:

- Load existing `textures.table` if present.
- Build a signature map from stored `content_hash` (no need to read `textures.data`).
- For each candidate input texture:
  - cook to payload bytes (uncompressed or BC7)
  - compute `content_hash`
  - if hash exists in table, reuse its index
  - else append payload to `textures.data` and append descriptor to `textures.table`

This yields stable indices across incremental imports and avoids re-encoding duplicates.

---

## Proposed module boundaries in Oxygen

Keep the current layering (importer backend → emitter → cooked writer):

1. **Decode** (`Import/ImageDecode.*`)

- Extend to support tinyexr (and optional float decode)
- Add a higher-level `DecodeToScratchImage()` that returns a `ScratchImage`

1. **Cook** (new: `Import/TextureCooker.*`)

- Input: source bytes/path(s) + `TextureMetadata` + `const ITexturePackingPolicy&`
- Output: `CookedTexturePayload { TextureResourceDesc desc; std::vector<std::byte> payload; }` (payload starts with `TexturePayloadHeader`)

1. **Emit** (`Import/emit/TextureEmitter.*`)

- Replace the current RGBA8 row-pitch repack with:
- decode → mipgen → (optional) BC7 encode → pack via `ITexturePackingPolicy` (write header + layout table) → append payload
- Keep fallback handling and index/dedup policy unchanged.

### Emitter redesign (recommended if we can start fresh)

If we can enhance or redesign texture emission, the biggest win is making **multi-source assembly and conventions explicit** and moving policy out of ad-hoc importer code.

Key ideas:

- **Declarative texture “source set”**: treat a texture as a set of input images + a mapping into `(array_layer, mip, slice)` rather than assuming “one file = one texture”.
- **Texture manifest assets**: introduce a tiny authored sidecar (e.g. `.texture.json`) that declares:
  - `TextureMetadata` (see below)
  - inputs (files) and how they map to faces/layers/slices
  - optional per-input overrides (rare; prefer presets)

- **Single front-door API**: provide one emitter entry point that always takes a `TextureSourceSet` + `TextureMetadata` and returns a stable texture table index.

This makes the API intuitive because importers no longer need special-case logic for arrays/cubes/3D or for convention fixes; they just author a manifest (or build a `TextureSourceSet`) and call the same function.

It also makes incremental cooking more reliable: the dedup key becomes “(all inputs + mapping + options) → content_hash”, which is stable and reproducible.

### Cooker API

The texture cooker provides two overloads of `CookTexture()` to handle single-source and multi-source textures:

```cpp
//! Result of cooking a texture.
struct CookedTexturePayload {
  TextureResourceDesc desc;         //!< Table entry for textures.table
  std::vector<std::byte> payload;   //!< Payload bytes (header + layouts + data)
};

//! Cook a single-source texture (most common case).
/*!
 @param source_bytes Raw bytes of the source image file (PNG, JPG, HDR, EXR, etc.)
 @param desc         Import descriptor specifying how to cook the texture
 @param policy       Packing policy for the target backend (D3D12, TightPacked)
 @return Cooked payload or error
*/
Result<CookedTexturePayload, TextureError> CookTexture(
    std::span<const std::byte> source_bytes,
    const TextureMetadata& desc,
    const ITexturePackingPolicy& policy);

//! Cook a multi-source texture (cube maps, arrays, 3D volumes).
/*!
 @param sources Set of source files mapped to subresources
 @param desc    Import descriptor specifying how to cook the texture
 @param policy  Packing policy for the target backend
 @return Cooked payload or error

 Use this overload when assembling a texture from multiple input files,
 such as 6 face images for a cube map or N slices for a 3D texture.
*/
Result<CookedTexturePayload, TextureError> CookTexture(
    const TextureSourceSet& sources,
    const TextureMetadata& desc,
    const ITexturePackingPolicy& policy);
```

### Texture Source Set

`TextureSourceSet` maps input files to subresources (array layers, mip levels, 3D slices).

```cpp
//! Cube face identifiers matching the face ordering convention.
enum class CubeFace : uint8_t {
  kPositiveX = 0, kNegativeX = 1,
  kPositiveY = 2, kNegativeY = 3,
  kPositiveZ = 4, kNegativeZ = 5,
};

//! Identifies a subresource within a multi-source texture.
struct SubresourceId {
  uint16_t array_layer = 0;  //!< Array layer (or cube face index 0-5)
  uint16_t mip_level = 0;    //!< Mip level
  uint16_t depth_slice = 0;  //!< Depth slice for 3D textures
};

//! A single source file mapped to a subresource.
struct TextureSource {
  std::vector<std::byte> bytes;  //!< Source file contents
  SubresourceId subresource;     //!< Target subresource
  std::string source_id;         //!< Diagnostic identifier (filename, path)
};

//! Collection of source files for multi-source texture assembly.
class TextureSourceSet {
public:
  //! Add a source file mapped to a specific subresource.
  void Add(TextureSource source);

  //! Add a source file for a specific cube face (convenience for cube maps).
  void AddCubeFace(CubeFace face, std::vector<std::byte> bytes, std::string source_id);

  //! Get all sources.
  std::span<const TextureSource> Sources() const noexcept;

  //! Get source count.
  size_t Count() const noexcept;

private:
  std::vector<TextureSource> sources_;
};
```

**Example: Assembling a cube map from 6 face images**:

```cpp
TextureSourceSet sources;
sources.AddCubeFace(CubeFace::kPositiveX, LoadFile("sky_px.hdr"), "sky_px.hdr");
sources.AddCubeFace(CubeFace::kNegativeX, LoadFile("sky_nx.hdr"), "sky_nx.hdr");
sources.AddCubeFace(CubeFace::kPositiveY, LoadFile("sky_py.hdr"), "sky_py.hdr");
sources.AddCubeFace(CubeFace::kNegativeY, LoadFile("sky_ny.hdr"), "sky_ny.hdr");
sources.AddCubeFace(CubeFace::kPositiveZ, LoadFile("sky_pz.hdr"), "sky_pz.hdr");
sources.AddCubeFace(CubeFace::kNegativeZ, LoadFile("sky_nz.hdr"), "sky_nz.hdr");

TextureMetadata desc;
desc.texture_type = TextureType::kTextureCube;
desc.intent = TextureIntent::kHDR_Environment;
desc.output_format = Format::kRGBA16Float;

auto result = CookTexture(sources, desc, D3D12PackingPolicy{});
```

1. **Write** (`Import/LooseCookedWriter`)

- Write updated `textures.table`
- Register externally-written `textures.data` (append-only)
- Finish index

---

## TextureMetadata (complete import + cook contract)

Design intent: **no open questions at emission time**.
`TextureMetadata` must fully specify everything needed to decode, assemble, transform, generate mips, compress, and choose the final stored format.

Packing is intentionally *not* part of `TextureMetadata`; it is a cook-time strategy selected per backend and encoded in the payload header for the runtime loader.

### Required fields

- **Identity**
  - `source_id`: stable identifier for diagnostics and hashing (path, URI, or virtual ID)

- **Shape / dimensionality**
  - `texture_type`: `TextureType::kTexture2D`, `kTexture2DArray`, `kTextureCube`, `kTextureCubeArray`, `kTexture3D`
  - `array_layers`: number of array layers (for cube/cube-array, includes faces: 6 or 6*N)
  - `depth`: for 3D textures only (number of slices); otherwise 1
  - `cube_face_order`: fixed to `+X, -X, +Y, -Y, +Z, -Z` when `texture_type` is cube* (importers must map authoring inputs to this)

- **Content intent** (drives defaults and validations)
  - `intent`: one of `Albedo`, `NormalTS`, `Roughness`, `Metallic`, `AO`, `Emissive`, `Opacity`, `ORMPacked`, `HDR_Environment`, `HDR_LightProbe`, `Data`

- **Color and sampling policy**
  - `color_space`: `Linear` or `SRGB`
  - sampling policy is derived from `output_format` (`*SRGB` vs `*UNorm`)

- **Normal map conventions** (only meaningful for `NormalTS`)
  - `flip_normal_green`: bool (default preset picks one)
  - `renormalize_normals_in_mips`: bool

- **Mip policy**
  - `mip_policy`: `None` (1 mip) or `FullChain` or `MaxCount(N)`
  - `mip_filter`: `Box`, `Kaiser` (default), or `Lanczos`
  - `mip_filter_space`: `Linear`

- **Output format and compression**
  - `output_format`: explicit final `oxygen::Format` (examples: `Format::kRGBA8UNorm`, `Format::kRGBA8UNormSRGB`, `Format::kBC7UNorm`, `Format::kBC7UNormSRGB`, `Format::kRGBA16Float`)
  - `compression`: `None`, `Fast`, `Default`, `High` (for BC7 formats only)

- **HDR handling** (for HDR inputs)
  - `bake_hdr_to_ldr`: bool
  - `exposure_ev`: float (default 0.0)
  - tonemap is currently fixed to ACES fitted when baking HDR->LDR

### Packing policy (backend-selected strategy)

Packing is selected by the cook pipeline based on the target backend (D3D12/Vulkan). The selected policy is recorded in `TexturePayloadHeader` (PAK v4) and interpreted by the runtime loader.

Validation rule (cook-time): the chosen packing policy must be compatible with the target backend uploader.

### Validation rules (must be deterministic)

Validation is performed at cook time and must produce clear, actionable error messages.

- If `texture_type` is cube*: `array_layers` must be a multiple of 6.
- If `texture_type` is 3D: `array_layers` must be 1 and `depth >= 1`.
- If `output_format` is BC7: it must be `Format::kBC7UNorm` or `Format::kBC7UNormSRGB`.
- If `bake_hdr_to_ldr` is false and input is HDR: `output_format` must be a float format (recommended: `Format::kRGBA16Float`).
- If `intent` is `kNormalTS`: `output_format` should be `*UNorm` (not `*SRGB`).
- All array layers must have identical dimensions (width × height).
- Dimensions must be ≥ 1 and ≤ `kMaxTextureDimension` (e.g., 16384).

Validation errors map to `TextureError` codes (see [Error taxonomy](#decoder-api)).

### Hashing contract (dedup)

The `content_hash` should be computed over the **stored payload bytes**.
To prevent ambiguous results across presets/options, the signature used for incremental cooking should include:

- `TextureMetadata` (normalized)
- the full set of input bytes (or stable content hashes of each input)
- the input-to-subresource mapping

---

## Import presets

Presets are named bundles that populate `TextureMetadata` with sensible defaults for typical authoring workflows.
Importers should select a preset first, then apply minimal overrides.

### LDR material presets

- **Albedo/BaseColor**
  - `intent = Albedo`
  - `color_space = SRGB`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - `output_format = Format::kBC7UNorm` (with manual conversion) or `Format::kBC7UNormSRGB` (with hardware sRGB)
  - `compression = Compression::kDefault`

- **Normal (tangent-space)**
  - `intent = NormalTS`
  - `color_space = Linear`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - `flip_normal_green = false` (override to `true` for OpenGL-style normal maps)
  - `renormalize_normals_in_mips = true`
  - `output_format = Format::kBC7UNorm`
  - `compression = Compression::kDefault`

- **Roughness / Metallic / AO (single-channel masks)**
  - `intent = Data` (or specific channel intent)
  - `color_space = Linear`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - `output_format = Format::kBC7UNorm`
  - `compression = Compression::kDefault`

- **ORM packed (R=AO, G=Roughness, B=Metallic)**
  - `intent = ORMPacked`
  - `color_space = Linear`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - `output_format = Format::kBC7UNorm`
  - `compression = Compression::kDefault`

- **Emissive**
  - `intent = Emissive`
  - `color_space = SRGB`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - `output_format = Format::kBC7UNorm` (manual) or `Format::kBC7UNormSRGB` (hardware)
  - `compression = Compression::kDefault`

- **UI / Text (high-frequency detail)**
  - `intent = Data`
  - `color_space = SRGB`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kLanczos`  // Preserve sharpness for text/icons
  - `output_format = Format::kBC7UNormSRGB`
  - `compression = Compression::kDefault`

### HDR environment presets

- **HDR environment (skybox)**
  - `intent = HDR_Environment`
  - `texture_type = TextureType::kTextureCube` (built from equirectangular HDRI or 6 faces)
  - `color_space = Linear`
  - `mip_policy = FullChain`
  - `mip_filter = MipFilter::kKaiser`
  - default: `bake_hdr_to_ldr = false`, `output_format = Format::kRGBA16Float`
  - optional LDR bake: `bake_hdr_to_ldr = true`, `output_format = Format::kBC7UNorm` or `Format::kBC7UNormSRGB`, `compression = Compression::kDefault`

- **HDR light probe (IBL source)**
  - `intent = HDR_LightProbe`
  - `mip_filter = MipFilter::kKaiser`
  - same as HDR environment, but treated as data for filtering/prefilter workflows
