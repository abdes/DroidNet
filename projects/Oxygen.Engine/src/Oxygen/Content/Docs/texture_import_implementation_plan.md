# Texture Import & Cooking Implementation Plan

This document provides a phased implementation plan for the texture import and cooking system as specified in [texture_import.md](texture_import.md).

## Overview

The implementation delivers a complete texture pipeline supporting:

- **Decoding**: stb_image (LDR + HDR) and tinyexr (EXR) input formats
- **Normalization**: DirectXTex-inspired `Image`/`ScratchImage` representation
- **Processing**: Mip generation, color space conversion, normal map handling
- **Compression**: Optional BC7 encoding via in-tree `bc7enc`
- **Emission**: PAK format v4 with explicit subresource layouts

---

## Phase 1: Foundation & Data Structures

Establish core types, enums, and in-memory representations.

### 1.1 Core Engine Types

**Files to create:**

- `src/Oxygen/Core/Types/ColorSpace.h`

> **Note:** `ColorSpace` is an engine-wide concept used in shaders, render passes, and swapchain configuration — not just texture import.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.1.1 | Define `oxygen::ColorSpace` enum (`kLinear`, `kSRGB`) in Core/Types | ☑ |
| 1.1.2 | Add `to_string(ColorSpace)` function | ☑ |
| 1.1.3 | Add static_assert for uint8_t size (PAK compatibility) | ☑ |

### 1.2 Texture Import Types

**Files to create:**

- `src/Oxygen/Content/Import/TextureImportTypes.h`

> **Note:** These types are specific to the import/cooking pipeline. The existing `oxygen::Format` enum (in Core/Types/Format.h) is used for working surface formats — no new `PixelFormat` enum needed.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.2.1 | Define `TextureIntent` enum (kAlbedo, kNormalTS, kRoughness, kMetallic, kAO, kEmissive, kOpacity, kORMPacked, kHdrEnvironment, kHdrLightProbe, kData) | ☑ |
| 1.2.2 | Define `MipPolicy` enum (`kNone`, `kFullChain`, `kMaxCount`) | ☑ |
| 1.2.3 | Define `MipFilter` enum (`kBox`, `kKaiser`, `kLanczos`) | ☑ |
| 1.2.4 | Define `Bc7Quality` enum (`kNone`, `kFast`, `kDefault`, `kHigh`) — renamed from `Compression` for clarity | ☑ |
| 1.2.5 | Add `to_string()` functions for each enum | ☑ |

### 1.3 Texture Import Error Taxonomy

**Files to create:**

- `src/Oxygen/Content/Import/TextureImportError.h`

> **Note:** Error types deserve their own header for clarity and to enable focused includes.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.3.1 | Define `TextureImportError` enum with decode errors (kUnsupportedFormat, kCorruptedData, kDecodeFailed, kOutOfMemory) | ☑ |
| 1.3.2 | Add validation errors (kInvalidDimensions, kDimensionMismatch, kArrayLayerCountInvalid, kDepthInvalidFor2D) | ☑ |
| 1.3.3 | Add cook errors (kMipGenerationFailed, kCompressionFailed, kOutputFormatInvalid, kHdrRequiresFloatFormat) | ☑ |
| 1.3.4 | Add I/O errors (kFileNotFound, kFileReadFailed, kWriteFailed) | ☑ |
| 1.3.5 | Add `to_string(TextureImportError)` function | ☑ |
| 1.3.6 | Add `IsDecodeError()`, `IsValidationError()`, `IsCookError()`, `IsIoError()` category helpers | ☑ |

### 1.4 TextureImportDesc Structure

**Files to create:**

- `src/Oxygen/Content/Import/TextureImportDesc.h`

> **Note:** This is the *import contract* — a descriptor for the cooking pipeline, not runtime metadata. Uses `oxygen::Format` (from Core/Types/Format.h), `oxygen::TextureType` (from Core/Types/TextureType.h), and `oxygen::ColorSpace` (new in Core/Types/ColorSpace.h).

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.4.1 | Define `TextureImportDesc` struct with all required fields | ☑ |
| 1.4.2 | Add identity fields (`source_id`) | ☑ |
| 1.4.3 | Add shape/dimensionality fields (`texture_type`, `width`, `height`, `depth`, `array_layers`) | ☑ |
| 1.4.4 | Add content intent field (`intent`) | ☑ |
| 1.4.5 | Add decode options (`flip_y_on_decode`, `force_rgba_on_decode`) | ☑ |
| 1.4.6 | Add color/sampling policy fields (`color_space`) | ☑ |
| 1.4.7 | Add normal map convention fields (`flip_normal_green`, `renormalize_normals_in_mips`) | ☑ |
| 1.4.8 | Add mip policy fields (`mip_policy`, `max_mip_levels`, `mip_filter`, `mip_filter_space`) | ☑ |
| 1.4.9 | Add output format field (`output_format` using `oxygen::Format`) | ☑ |
| 1.4.10 | Add BC7 quality field (`bc7_quality` using `Bc7Quality` enum) | ☑ |
| 1.4.11 | Add HDR handling fields (`bake_hdr_to_ldr`, `exposure_ev`) | ☑ |
| 1.4.12 | Add `Validate()` method returning `std::optional<TextureImportError>` | ☑ |

### 1.5 ImageView and ScratchImage

**Files to create:**

- `src/Oxygen/Content/Import/ScratchImage.h`
- `src/Oxygen/Content/Import/ScratchImage.cpp`

> **Note:** `ImageView` is defined inline in ScratchImage.h as a simple view struct. Uses `oxygen::Format` for pixel format.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.5.1 | Define `ImageView` struct (width, height, format, row_pitch_bytes, pixels span) | ☑ |
| 1.5.2 | Define `ScratchImage` class with metadata and storage | ☑ |
| 1.5.3 | Implement `Meta()` accessor | ☑ |
| 1.5.4 | Implement `GetImage(array_layer, mip)` method | ☑ |
| 1.5.5 | Implement allocation/initialization helpers | ☑ |
| 1.5.6 | Implement `ComputeMipCount()` static helper | ☑ |
| 1.5.7 | Implement `ComputeSubresourceIndex()` helper | ☑ |
| 1.5.8 | Implement `ComputeMipDimensions()` helper | ☑ |
| 1.5.9 | Add unit tests for `ScratchImage` | ☑ |

### 1.6 PAK Format v4 Structures

**Files to modify:**

- `src/Oxygen/Data/PakFormat.h`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 1.6.1 | Create `oxygen::data::pak::v4` namespace | ☑ |
| 1.6.2 | Define `TexturePackingPolicyId` enum (`kD3D12`, `kTightPacked`) | ☑ |
| 1.6.3 | Define `TexturePayloadFlags` enum | ☑ |
| 1.6.4 | Define `SubresourceLayout` struct (12 bytes packed) | ☑ |
| 1.6.5 | Define `kTexturePayloadMagic` constant (`'OTX1'`) | ☑ |
| 1.6.6 | Define `TexturePayloadHeader` struct (28 bytes packed) | ☑ |
| 1.6.7 | Update `PakHeader::version` to 4 in v4 namespace | ☑ |
| 1.6.8 | Add `using namespace v4;` alias in `oxygen::data::pak` | ☑ |
| 1.6.9 | Add static_asserts for struct sizes | ☑ |
| 1.6.10 | Document breaking changes in header comments | ☑ |

---

## Phase 2: Decode Layer

Extend the existing decode layer to support HDR formats and produce `ScratchImage`.

### 2.1 TinyEXR Integration

**Files to modify:**

- `src/Oxygen/Content/Import/ImageDecode.h`
- `src/Oxygen/Content/Import/ImageDecode.cpp`

> **Note:** tinyexr is already integrated in the project. This section focuses on using it for EXR decoding.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 2.1.1 | Implement EXR signature detection function (`IsExrSignature()`) | ☑ |
| 2.1.2 | Implement `DecodeExrToFloat()` internal function using tinyexr API | ☑ |
| 2.1.3 | Handle tinyexr error codes and map to `TextureImportError` | ☑ |
| 2.1.4 | Add unit tests for EXR decoding | ☑ |

### 2.2 Extended stb_image Support

**Files to modify:**

- `src/Oxygen/Content/Import/ImageDecode.h`
- `src/Oxygen/Content/Import/ImageDecode.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 2.2.1 | Implement `.hdr` format detection | ☑ |
| 2.2.2 | Implement `DecodeHdrToFloat()` using `stbi_loadf` | ☑ |
| 2.2.3 | Add unit tests for HDR decoding | ☑ |

### 2.3 Unified Decode API

**Files to modify:**

- `src/Oxygen/Content/Import/ImageDecode.h`
- `src/Oxygen/Content/Import/ImageDecode.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 2.3.1 | Define `DecodeOptions` struct | ☑ |
| 2.3.2 | Implement `DecodeToScratchImage()` unified entry point | ☑ |
| 2.3.3 | Implement format detection (EXR signature, HDR extension, fallback to stb_image) | ☑ |
| 2.3.4 | Ensure LDR outputs `PixelFormat::kRGBA8_UNorm` | ☑ |
| 2.3.5 | Ensure HDR outputs `PixelFormat::kRGBA32_Float` | ☑ |
| 2.3.6 | Implement Y-flip option | ☑ |
| 2.3.7 | Implement force-RGBA option | ☑ |
| 2.3.8 | Add comprehensive unit tests for unified decoder | ☑ |

---

## Phase 3: Image Processing

Implement mip generation, color space conversion, and content-specific processing as stateless utility functions.

**Files to create:**

- `src/Oxygen/Content/Import/ImageProcessing.h`
- `src/Oxygen/Content/Import/ImageProcessing.cpp`

> **Note:** All functions are stateless utilities organized under nested namespaces: `image::color`, `image::hdr`, `image::mip`, `image::content`.

### 3.1 Color Space Conversion (`image::color`)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 3.1.1 | Implement `image::color::SrgbToLinear(float)` function | ☑ |
| 3.1.2 | Implement `image::color::LinearToSrgb(float)` function | ☑ |
| 3.1.3 | Implement `image::color::SrgbToLinear(float4)` for RGBA | ☑ |
| 3.1.4 | Implement `image::color::LinearToSrgb(float4)` for RGBA | ☑ |
| 3.1.5 | Implement whole-image conversion helpers | ☑ |
| 3.1.6 | Add unit tests for color space conversion | ☑ |

### 3.2 HDR Processing (`image::hdr`)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 3.2.1 | Implement `image::hdr::ApplyExposure()` | ☑ |
| 3.2.2 | Implement `image::hdr::AcesTonemap()` (ACES fitted) | ☑ |
| 3.2.3 | Implement `image::hdr::BakeToLdr()` pipeline (exposure → tonemap → quantize) | ☑ |
| 3.2.4 | Add unit tests for HDR processing | ☑ |

### 3.3 Mip Filter Kernels (`image::mip`)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 3.3.1 | Implement `image::mip::BesselI0()` (modified Bessel function) | ☑ |
| 3.3.2 | Implement `image::mip::KaiserWindow(x, alpha)` function | ☑ |
| 3.3.3 | Implement `image::mip::LanczosKernel(x, a)` function | ☑ |
| 3.3.4 | Implement 1D separable filter application | ☑ |
| 3.3.5 | Implement 2D box filter (2×2 average) | ☑ |
| 3.3.6 | Implement 2D Kaiser filter (separable) | ☑ |
| 3.3.7 | Implement 2D Lanczos filter (separable) | ☑ |
| 3.3.8 | Add unit tests for filter kernels | ☑ |

### 3.4 Mip Generation (`image::mip`)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 3.4.1 | Implement `image::mip::ComputeMipCount(width, height)` | ☑ |
| 3.4.2 | Implement `image::mip::GenerateChain2D()` for 2D textures | ☑ |
| 3.4.3 | Implement `image::mip::GenerateChain3D()` for 3D textures | ☑ |
| 3.4.4 | Implement sRGB→linear conversion before filtering | ☑ |
| 3.4.5 | Implement linear→sRGB conversion after filtering (when needed) | ☑ |
| 3.4.6 | Add unit tests for mip generation | ☑ |

### 3.5 Content-Specific Processing (`image::content`)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 3.5.1 | Implement `image::content::RenormalizeNormal()` per-texel | ☑ |
| 3.5.2 | Implement `image::content::GenerateNormalMapMips()` with renormalization option | ☑ |
| 3.5.3 | Implement `image::content::FlipNormalGreen()` for normal maps | ☑ |
| 3.5.4 | Add unit tests for content-specific processing | ☑ |

---

## Phase 4: BC7 Encoding

Integrate bc7enc for optional BC7 compression.

### 4.1 BC7 Encoder Wrapper

**Files to create:**

- `src/Oxygen/Content/Import/bc7/Bc7Encoder.h`
- `src/Oxygen/Content/Import/bc7/Bc7Encoder.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 4.1.1 | Implement thread-safe one-time `bc7enc_compress_block_init()` wrapper | ☑ |
| 4.1.2 | Define `Bc7EncoderParams` struct for quality tiers | ☑ |
| 4.1.3 | Implement `Bc7EncodeBlock()` wrapper function | ☑ |
| 4.1.4 | Implement perceptual vs linear weight selection | ☑ |
| 4.1.5 | Add unit tests for single block encoding | ☑ |

### 4.2 Full Texture BC7 Encoding

**Files to modify:**

- `src/Oxygen/Content/Import/bc7/Bc7Encoder.h`
- `src/Oxygen/Content/Import/bc7/Bc7Encoder.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 4.2.1 | Implement block iteration (blocks_x, blocks_y calculation) | ☑ |
| 4.2.2 | Implement edge handling with border replication | ☑ |
| 4.2.3 | Implement `EncodeSurfaceBc7()` for single mip | ☑ |
| 4.2.4 | Implement `EncodeTextureBc7()` for full texture with all mips | ☑ |
| 4.2.5 | Implement quality tier mapping to bc7enc parameters | ☑ |
| 4.2.6 | Add unit tests for full texture encoding | ☑ |

### 4.3 Parallel BC7 Encoding (Optional)

**Files to modify:**

- `src/Oxygen/Content/Import/bc7/Bc7Encoder.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 4.3.1 | Implement parallel block encoding using thread pool | ☐ |
| 4.3.2 | Add thread count configuration | ☐ |
| 4.3.3 | Add performance benchmarks | ☐ |

---

## Phase 5: Packing Policies

Implement backend-specific texture packing strategies.

### 5.1 Packing Policy Interface

**Files to create:**

- `src/Oxygen/Content/Import/TexturePackingPolicy.h`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 5.1.1 | Define `ITexturePackingPolicy` interface | ☑ |
| 5.1.2 | Define `Id()` method | ☑ |
| 5.1.3 | Define `AlignRowPitchBytes()` method | ☑ |
| 5.1.4 | Define `AlignSubresourceOffset()` method | ☑ |

### 5.2 D3D12 Packing Policy

**Files to create:**

- `src/Oxygen/Content/Import/TexturePackingPolicy.h`
- `src/Oxygen/Content/Import/TexturePackingPolicy.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 5.2.1 | Define `kD3D12RowPitchAlignment = 256` constant | ☑ |
| 5.2.2 | Define `kD3D12SubresourcePlacementAlignment = 512` constant | ☑ |
| 5.2.3 | Implement `D3D12PackingPolicy` class | ☑ |
| 5.2.4 | Add unit tests for D3D12 packing alignment | ☑ |

### 5.3 Tight Packed Policy

**Files to create:**

- `src/Oxygen/Content/Import/TexturePackingPolicy.h`
- `src/Oxygen/Content/Import/TexturePackingPolicy.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 5.3.1 | Implement `TightPackedPolicy` class | ☑ |
| 5.3.2 | Implement minimal alignment (4-byte subresource offset) | ☑ |
| 5.3.3 | Add unit tests for tight packing | ☑ |

### 5.4 Subresource Layout Computation

**Files to create:**

- `src/Oxygen/Content/Import/TexturePackingPolicy.h`
- `src/Oxygen/Content/Import/TexturePackingPolicy.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 5.4.1 | Implement `ComputeBytesPerPixel(Format)` helper | ☑ |
| 5.4.2 | Implement `ComputeRowBytes()` for uncompressed formats | ☑ |
| 5.4.3 | Implement `ComputeRowBytes()` for BC7 formats | ☑ |
| 5.4.4 | Implement `ComputeSubresourceLayouts()` for full texture | ☑ |
| 5.4.5 | Implement `ComputeTotalPayloadSize()` | ☑ |
| 5.4.6 | Add unit tests for layout computation | ☑ |

---

## Phase 6: Texture Cooker

Create the main cooking pipeline that orchestrates all components.

### 6.1 Cooker Core

**Files to create:**

- `src/Oxygen/Content/Import/TextureCooker.h`
- `src/Oxygen/Content/Import/TextureCooker.cpp`

> **API Design:** See [Cooker API](texture_import.md#cooker-api) in the design document. The cooker provides two `CookTexture()` overloads — single-source and multi-source — both taking `TextureImportDesc` and `ITexturePackingPolicy`.

| Task | Description | Status |
| ---- | ----------- | ------ |
| 6.1.1 | Define `CookedTexturePayload` struct (desc + payload bytes) | ☑ |
| 6.1.2 | Implement `CookTexture(span<byte>, desc, policy)` for single-source textures | ☑ |
| 6.1.3 | Implement `CookTexture(TextureSourceSet, desc, policy)` for multi-source textures | ☑ |
| 6.1.4 | Implement decode → working format pipeline | ☑ |
| 6.1.5 | Implement mip generation step | ☑ |
| 6.1.6 | Implement content-specific processing dispatch | ☑ |
| 6.1.7 | Implement BC7 encoding step (when output is BC7) | ☑ |
| 6.1.8 | Implement payload header + layout table writing | ☑ |
| 6.1.9 | Implement subresource data packing | ☑ |
| 6.1.10 | Implement content hash computation | ☑ |

### 6.2 Validation

**Files to modify:**

- `src/Oxygen/Content/Import/TextureCooker.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 6.2.1 | Implement texture type vs array_layers validation | ☑ |
| 6.2.2 | Implement cube map layer count validation (multiple of 6) | ☑ |
| 6.2.3 | Implement 3D texture depth validation | ☑ |
| 6.2.4 | Implement HDR input vs output format validation | ☑ |
| 6.2.5 | Implement dimension limit validation | ☑ |
| 6.2.6 | Implement intent vs format compatibility validation | ☑ |
| 6.2.7 | Add unit tests for all validation rules | ☑ |

### 6.3 Unit Tests

**Files to create:**

- `src/Oxygen/Content/Test/TextureCooker_basic_test.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 6.3.1 | Test LDR 2D texture cooking | ☑ |
| 6.3.2 | Test LDR 2D texture with mips | ☑ |
| 6.3.3 | Test BC7 encoding path | ☑ |
| 6.3.4 | Test HDR to float16 path | ☐ |
| 6.3.5 | Test HDR bake to LDR path | ☐ |
| 6.3.6 | Test normal map with renormalization | ☑ |
| 6.3.7 | Test 3D texture mip generation | ☐ |
| 6.3.8 | Test cube map assembly | ☐ |
| 6.3.9 | Test array texture assembly | ☐ |
| 6.3.10 | Test D3D12 vs TightPacked layouts | ☑ |

---

## Phase 7: Import Presets

Implement preset system for common texture types.

### 7.1 Preset Definitions

**Files to create:**

- `src/Oxygen/Content/Import/TextureImportPresets.h`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 7.1.1 | Define `TexturePreset` enum | ✅ |
| 7.1.2 | Implement `GetPresetMetadata(TexturePreset)` function | ✅ |
| 7.1.3 | Implement Albedo/BaseColor preset | ✅ |
| 7.1.4 | Implement Normal (tangent-space) preset | ✅ |
| 7.1.5 | Implement Roughness/Metallic/AO preset | ✅ |
| 7.1.6 | Implement ORM packed preset | ✅ |
| 7.1.7 | Implement Emissive preset | ✅ |
| 7.1.8 | Implement UI/Text preset (Lanczos filter) | ✅ |
| 7.1.9 | Implement HDR environment preset | ✅ |
| 7.1.10 | Implement HDR light probe preset | ✅ |
| 7.1.11 | Add unit tests for preset application | ✅ |

---

## Phase 8: Texture Source Assembly

Implement multi-source texture assembly (cubes, arrays, 3D volumes).

> **API Design:** See [Texture Source Set](texture_import.md#texture-source-set) in the design document. `TextureSourceSet` maps input files to subresources and is passed to the multi-source `CookTexture()` overload.

### 8.1 Texture Source Set ✅

**Files created:**

- `src/Oxygen/Content/Import/TextureSourceAssembly.h`
- `src/Oxygen/Content/Import/TextureSourceAssembly.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 8.1.1 | Define `TextureSource` struct (bytes + SubresourceId + source_id) | ✅ |
| 8.1.2 | Define `TextureSourceSet` class with sources_ vector | ✅ |
| 8.1.3 | Implement `AddArrayLayer(bytes, layer)` for arrays/cubes | ✅ |
| 8.1.4 | Implement `AddCubeFace(bytes, CubeFace)` convenience overload | ✅ |
| 8.1.5 | Implement `AddDepthSlice(bytes, slice)` for 3D textures | ✅ |
| 8.1.6 | Implement `AddMipLevel(bytes, mip)` for pre-mipped sources | ✅ |
| 8.1.7 | Implement `Count()`, `Sources()`, `GetSource(index)` accessors | ✅ |
| 8.1.8 | Add 20 unit tests for source set assembly | ✅ |

### 8.2 Cube Map Assembly ✅

**Files modified:**

- `src/Oxygen/Content/Import/TextureSourceAssembly.h`
- `src/Oxygen/Content/Import/TextureSourceAssembly.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 8.2.1 | Define face ordering constants (+X, -X, +Y, -Y, +Z, -Z) | ✅ |
| 8.2.2 | Implement `AssembleCubeFromFaces()` helper | ✅ |
| 8.2.3 | Document Oxygen Z-up convention in comments | ✅ |
| 8.2.4 | Add unit tests for cube map assembly | ✅ |

### 8.3 Equirectangular to Cube Conversion ✅

**Files modified:**

- `src/Oxygen/Content/Import/TextureSourceAssembly.h`
- `src/Oxygen/Content/Import/TextureSourceAssembly.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 8.3.1 | Define `EquirectToCubeOptions` struct | ✅ |
| 8.3.2 | Implement face direction vectors for Oxygen conventions | ✅ |
| 8.3.3 | Implement direction → spherical → UV mapping | ✅ |
| 8.3.4 | Implement bilinear sampling for box filter | ✅ |
| 8.3.5 | Implement bicubic sampling for Kaiser/Lanczos | ✅ |
| 8.3.6 | Implement `ConvertEquirectangularToCube()` function | ✅ |
| 8.3.7 | Add unit tests for equirect conversion | ✅ |

---

## Phase 9: Emitter Integration

Integrate the new cooker into the existing emission pipeline.

### 9.1 Emitter Refactoring

**Files to modify:**

- `src/Oxygen/Content/Import/emit/TextureEmitter.h`
- `src/Oxygen/Content/Import/emit/TextureEmitter.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 9.1.1 | Replace RGBA8 row-pitch repack with cooker pipeline | ✅ |
| 9.1.2 | Update `GetOrCreateTextureResourceIndex()` to use cooker | ✅ |
| 9.1.3 | Integrate `TextureImportDesc` into emission flow | ✅ |
| 9.1.4 | Add packing policy selection based on target backend | ✅ |
| 9.1.5 | Update fallback texture to use BC7 (optional) | ✅ |
| 9.1.6 | Preserve existing deduplication logic | ✅ |
| 9.1.7 | Update signature computation to include metadata | ✅ |

## Phase 10: Texture Manifest (Optional Enhancement)

Implement declarative texture manifests for complex imports.

### 10.1 Manifest Format

**Files to create:**

- `src/Oxygen/Content/Import/TextureManifest.h`
- `src/Oxygen/Content/Import/TextureManifest.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 10.1.1 | Define JSON schema for `.texture.json` manifests | ☐ |
| 10.1.2 | Implement manifest parsing | ☐ |
| 10.1.3 | Implement manifest → TextureImportDesc conversion | ☐ |
| 10.1.4 | Implement manifest → TextureSourceSet conversion | ☐ |
| 10.1.5 | Add unit tests for manifest parsing | ☐ |

---

## Phase 11: Runtime Loader Updates

Update runtime loader to handle v4 payloads.

### 11.1 Loader Updates

**Files to modify:**

- `src/Oxygen/Content/Loaders/TextureLoader.cpp` (or equivalent)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 11.1.1 | Implement v4 payload header detection (`kTexturePayloadMagic`) | ✅ |
| 11.1.2 | Implement v4 layout table parsing | ✅ |
| 11.1.3 | Implement subresource upload using layout table | ✅ |
| 11.1.5 | Add unit tests for loader | ✅ |

---

## Phase 12: Tools Updates

Update asset tools for v4 compatibility.

### 12.1 PakGen Updates

**Files to modify:**

- `src/Oxygen/Content/Tools/PakGen/` (or equivalent)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 12.1.1 | Emit `TexturePayloadHeader` when writing textures | ✅ |
| 12.1.2 | Add packing policy selection (D3D12/TightPacked) | ✅ |
| 12.1.3 | Bump output version to 4 | ✅ |

### 12.2 PakDump Updates

**Files to modify:**

- `src/Oxygen/Content/Tools/PakDump/` (or equivalent)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 12.2.1 | Parse and display `TexturePayloadHeader` | ✅ |
| 12.2.2 | Decode and display packing policy | ✅ |
| 12.2.3 | List subresource layouts | ✅ |
| 12.2.4 | Handle v3/v4 version detection | ✅ |

### 12.3 Inspector Updates

**Files to modify:**

- Inspector tool files (if applicable)

| Task | Description | Status |
| ---- | ----------- | ------ |
| 12.3.1 | Validate magic and version | ✅ |

---

## Phase 13: Integration Testing

End-to-end validation of the complete pipeline.

### 13.1 Integration Tests

**Files to create:**

- `src/Oxygen/Content/Test/TextureImport_integration_test.cpp`

| Task | Description | Status |
| ---- | ----------- | ------ |
| 13.1.1 | Test PNG → BC7 → load → render | ☐ |
| 13.1.2 | Test EXR → float16 → load → render | ☐ |
| 13.1.3 | Test HDR → BC7 (baked) → load → render | ☐ |
| 13.1.4 | Test cube map assembly → load → render | ☐ |
| 13.1.5 | Test 2D array texture → load → render | ☐ |
| 13.1.6 | Test 3D texture → load → render | ☐ |
| 13.1.7 | Test incremental cooking (deduplication) | ☐ |
| 13.1.8 | Test v3→v4 migration path | ☐ |

### 13.2 Example Application

**Files to create/modify:**

- `Examples/TexturedCube/` or similar

| Task | Description | Status |
| ---- | ----------- | ------ |
| 13.2.1 | Update example to use BC7 textures | ☑ |
| 13.2.2 | Add HDR environment example | ☑ |
| 13.2.3 | Document example usage | ☑ |

---

## Phase 14: Documentation

### 14.1 API Documentation

| Task | Description | Status |
| ---- | ----------- | ------ |
| 14.1.1 | Add Doxygen comments to all public APIs | ☐ |
| 14.1.2 | Document `TextureImportDesc` field semantics | ☐ |
| 14.1.3 | Document preset usage | ☐ |
| 14.1.4 | Document packing policies | ☐ |

### 14.2 User Documentation

| Task | Description | Status |
| ---- | ----------- | ------ |
| 14.2.1 | Update README with texture import workflow | ☐ |
| 14.2.2 | Document supported input formats | ☐ |
| 14.2.3 | Document output format selection | ☐ |
| 14.2.4 | Document manifest format (if implemented) | ☐ |

---

## Dependencies Between Phases

```text
Phase 1 ─────┬───────────────────────────────────────────────────┐
             │                                                   │
             v                                                   │
Phase 2 ─────┬───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 3 ─────┬───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 4 ─────┬───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 5 ─────┬───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 6 ◄────┴───────────────────────────────────────────────────┤
             │                                                   │
             ├────► Phase 7 (can start after Phase 6)            │
             │                                                   │
             ├────► Phase 8 (can start after Phase 6)            │
             │                                                   │
             v                                                   │
Phase 9 ◄────┴───────────────────────────────────────────────────┤
             │                                                   │
             ├────► Phase 10 (optional, can start after Phase 9) │
             │                                                   │
             v                                                   │
Phase 11 ◄───┴───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 12 ◄───┴───────────────────────────────────────────────────┤
             │                                                   │
             v                                                   │
Phase 13 ◄───┴───────────────────────────────────────────────────┘
             │
             v
Phase 14 (can run in parallel with any phase)
```

---

## Summary Statistics

| Phase | Tasks | Description |
| ----- | ----- | ----------- |
| 1 | 37 | Foundation & Data Structures (1.1–1.6) |
| 2 | 16 | Decode Layer |
| 3 | 22 | Image Processing |
| 4 | 11 | BC7 Encoding |
| 5 | 14 | Packing Policies |
| 6 | 17 | Texture Cooker |
| 7 | 11 | Import Presets |
| 8 | 17 | Texture Source Assembly |
| 9 | 10 | Emitter Integration |
| 10 | 5 | Texture Manifest (Optional) |
| 11 | 5 | Runtime Loader Updates |
| 12 | 10 | Tools Updates |
| 13 | 11 | Integration Testing |
| 14 | 6 | Documentation |
| **Total** | **189** | |

---

## Revision History

| Date | Version | Author | Changes |
| ---- | ------- | ------ | ------- |
| 2026-01-10 | 1.0 | - | Initial implementation plan |
| 2026-01-10 | 1.1 | - | Reorganized Phase 1: removed redundant PixelFormat (use oxygen::Format), moved ColorSpace to Core/Types, separated TextureImportError to own header, renamed Compression→Bc7Quality |
| 2026-01-10 | 1.2 | - | Renamed TextureMetadata→TextureImportDesc (it's an import descriptor, not intrinsic metadata) |
| 2026-01-10 | 1.3 | - | Clarified CookTexture API: two overloads (single-source, multi-source via TextureSourceSet), both taking TextureImportDesc; tinyexr already integrated |
| 2026-01-10 | 1.4 | - | Moved API design to design document (texture_import.md); implementation plan now references design doc |
| 2026-01-10 | 1.5 | - | Phase 3: Consolidated into single ImageProcessing.h/cpp with nested namespaces (image::color, image::hdr, image::mip, image::content) |
