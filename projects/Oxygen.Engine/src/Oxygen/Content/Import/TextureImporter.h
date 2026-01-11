//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportPresets.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/Import/TextureSourceAssembly.h> // For CubeFace, kCubeFaceCount
#include <Oxygen/Content/api_export.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>

namespace oxygen::content::import {

// Forward declarations
struct TextureImportDesc;
class ITexturePackingPolicy;

//===----------------------------------------------------------------------===//
// High-Level Import Result
//===----------------------------------------------------------------------===//

//! Result of a high-level texture import operation.
/*!
  Wraps `CookedTexturePayload` with additional diagnostic information
  about the import process.

  @see ImportTexture, TextureImportBuilder
*/
struct TextureImportResult {
  //! Cooked texture data ready for runtime use.
  CookedTexturePayload payload;

  //! Original source path(s) for diagnostics.
  std::string source_path;

  //! Preset that was applied during import.
  TexturePreset applied_preset = TexturePreset::kData;
};

//===----------------------------------------------------------------------===//
// Preset Auto-Detection
//===----------------------------------------------------------------------===//

//! Detect texture preset from filename conventions.
/*!
  Examines the filename for common suffixes to guess the appropriate preset.

  ### Recognized Patterns

  | Suffix Pattern       | Detected Preset      |
  | -------------------- | -------------------- |
  | `*_albedo.*`         | kAlbedo              |
  | `*_basecolor.*`      | kAlbedo              |
  | `*_diffuse.*`        | kAlbedo              |
  | `*_color.*`          | kAlbedo              |
  | `*_normal.*`         | kNormal              |
  | `*_nrm.*`            | kNormal              |
  | `*_roughness.*`      | kRoughness           |
  | `*_rough.*`          | kRoughness           |
  | `*_metallic.*`       | kMetallic            |
  | `*_metal.*`          | kMetallic            |
  | `*_ao.*`             | kAO                  |
  | `*_occlusion.*`      | kAO                  |
  | `*_orm.*`            | kORMPacked           |
  | `*_emissive.*`       | kEmissive            |
  | `*_emission.*`       | kEmissive            |
  | `*_height.*`         | kHeightMap           |
  | `*_displacement.*`   | kHeightMap           |
  | `*_disp.*`           | kHeightMap           |
  | `*_bump.*`           | kHeightMap           |
  | `.hdr` extension     | kHdrEnvironment      |
  | `.exr` extension     | kHdrEnvironment      |
  | `*_env.*`            | kHdrEnvironment      |
  | `*_hdri.*`           | kHdrEnvironment      |

  @param filename Filename (with or without path)
  @return Detected preset, or kData if no pattern matched
*/
OXGN_CNTT_NDAPI auto DetectPresetFromFilename(
  const std::filesystem::path& filename) noexcept -> TexturePreset;

//===----------------------------------------------------------------------===//
// ScratchImage Loading API (Composable)
//===----------------------------------------------------------------------===//

//! Load an image file into a ScratchImage without cooking.
/*!
  Decodes the image and returns a ScratchImage for inspection or composition.
  Use this when you need to:
  - Inspect image metadata before deciding on a preset
  - Compose multiple images manually
  - Apply custom processing before cooking

  @param path Path to the source image file
  @return Decoded ScratchImage on success, or error

  ### Usage Example

  ```cpp
  /\/ Load and inspect before cooking
  auto image = LoadTexture("textures\/mystery.png");
  if (image) {
    const auto& meta = image->Meta();
    if (meta.format == Format::kRGBA32Float) {
      /\/ It's HDR, use HDR preset
      auto result = CookImportedTexture(std::move(*image),
                                TexturePreset::kHdrEnvironment,
                                D3D12PackingPolicy::Instance());
    }
  }
  ```

  @see CookImportedTexture for cooking a loaded ScratchImage
*/
OXGN_CNTT_NDAPI auto LoadTexture(const std::filesystem::path& path)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Load an image file into a ScratchImage with custom options.
/*!
  @param path Path to the source image file
  @param desc Import descriptor (uses flip_y_on_decode)
  @return Decoded ScratchImage on success, or error
*/
OXGN_CNTT_NDAPI auto LoadTexture(
  const std::filesystem::path& path, const TextureImportDesc& desc)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Load an image from memory into a ScratchImage without cooking.
/*!
  @param data      Raw image data (PNG, JPG, HDR, EXR, etc.)
  @param source_id Identifier for diagnostics and error messages
  @return Decoded ScratchImage on success, or error
*/
OXGN_CNTT_NDAPI auto LoadTexture(
  std::span<const std::byte> data, std::string_view source_id)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Load an image from memory with custom options.
/*!
  @param data      Raw image data (PNG, JPG, HDR, EXR, etc.)
  @param desc      Import descriptor (uses flip_y_on_decode)
  @return Decoded ScratchImage on success, or error
*/
OXGN_CNTT_NDAPI auto LoadTexture(
  std::span<const std::byte> data, const TextureImportDesc& desc)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Load multiple image files into separate ScratchImages.
/*!
  Loads each file independently. Useful for manual composition of
  cube maps, texture arrays, or 3D textures.

  @param paths Paths to source image files
  @return Vector of decoded ScratchImages on success, or first error encountered

  ### Usage Example

  ```cpp
  std::vector<std::filesystem::path> faces = {
    "sky_px.hdr", "sky_nx.hdr", "sky_py.hdr",
    "sky_ny.hdr", "sky_pz.hdr", "sky_nz.hdr"
  };
  auto images = LoadTextures(faces);
  if (images) {
    /\/ Manually compose or process before cooking
    auto cube = AssembleCubeFromFaces(std::span<const ScratchImage,
  6>(*images));
  }
  ```
*/
OXGN_CNTT_NDAPI auto LoadTextures(std::span<const std::filesystem::path> paths)
  -> oxygen::Result<std::vector<ScratchImage>, TextureImportError>;

//===----------------------------------------------------------------------===//
// ScratchImage Cooking API
//===----------------------------------------------------------------------===//

//! Cook a pre-loaded ScratchImage with a preset.
/*!
  Use this when you have already loaded or composed a ScratchImage and
  want to apply a preset and cook it.

  @param image  Pre-loaded ScratchImage (takes ownership via move)
  @param preset Texture preset to apply
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  /\/ Load, inspect, then cook with appropriate preset
  auto image = LoadTexture("textures\/mystery.png");
  if (image) {
    TexturePreset preset = image->Meta().format == Format::kRGBA32Float
      ? TexturePreset::kHdrEnvironment
      : TexturePreset::kAlbedo;
    auto result = CookImportedTexture(std::move(*image), preset,
                              D3D12PackingPolicy::Instance());
  }
  ```

  @see LoadTexture for loading images
*/
OXGN_CNTT_NDAPI auto CookScratchImage(ScratchImage&& image,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Cook a pre-loaded ScratchImage with a custom descriptor.
/*!
  Use this when you need full control over the import descriptor
  beyond what presets offer.

  @param image  Pre-loaded ScratchImage (takes ownership via move)
  @param desc   Custom import descriptor
  @param policy Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto CookScratchImage(ScratchImage&& image,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//===----------------------------------------------------------------------===//
// Single-File Import API
//===----------------------------------------------------------------------===//

//! Import a single texture file with automatic preset detection.
/*!
  Main entry point for importing textures. Automatically detects:
  - **Format** from file extension and content sniffing
  - **Preset** from filename conventions (e.g., `*_albedo.png` → kAlbedo)
  - **Dimensions** from decoded image

  @param path   Path to the source image file
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  \/\/ Simple import with auto-detection
  auto result = ImportTexture("textures\/brick_albedo.png",
                              D3D12PackingPolicy::Instance());
  if (result) {
    \/\/ Use result->payload
  }
  ```

  @see DetectPresetFromFilename for auto-detection rules
  @see TextureImportBuilder for advanced control
*/
OXGN_CNTT_NDAPI auto ImportTexture(
  const std::filesystem::path& path, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a single texture file with explicit preset.
/*!
  Use when automatic preset detection is not desired or when the filename
  does not follow naming conventions.

  @param path   Path to the source image file
  @param preset Texture preset to apply
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  auto result = ImportTexture("textures\/brick.png",
                              TexturePreset::kAlbedo,
                              D3D12PackingPolicy::Instance());
  ```
*/
OXGN_CNTT_NDAPI auto ImportTexture(const std::filesystem::path& path,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a single texture file with a custom descriptor.
/*!
  Use when you need full control over the import configuration beyond
  what presets offer. The descriptor's `source_id` field will be set
  to the file path if not already specified.

  @param path   Path to the source image file
  @param desc   Custom import descriptor (dimensions can be left at 0 to infer)
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  TextureImportDesc desc;
  desc.intent = TextureIntent::kNormalTS;
  desc.mip_filter = MipFilter::kLanczos;
  desc.flip_normal_green = true;
  desc.output_format = Format::kBC7UNorm;
  desc.bc7_quality = Bc7Quality::kHigh;

  auto result = ImportTexture("textures\/brick_normal.png",
                              desc,
                              D3D12PackingPolicy::Instance());
  ```

  @see TextureImportDesc for all configurable options
*/
OXGN_CNTT_NDAPI auto ImportTexture(const std::filesystem::path& path,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a single texture from memory with explicit preset.
/*!
  Use when the source data is already loaded into memory.

  @param data      Raw image data (PNG, JPG, HDR, EXR, etc.)
  @param source_id Identifier for diagnostics and error messages
  @param preset    Texture preset to apply
  @param policy    Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportTexture(std::span<const std::byte> data,
  std::string_view source_id, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a single texture from memory with a custom descriptor.
/*!
  Use when you need full control over the import configuration.

  @param data   Raw image data (PNG, JPG, HDR, EXR, etc.)
  @param desc   Custom import descriptor (source_id should be set for
  diagnostics)
  @param policy Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportTexture(std::span<const std::byte> data,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//===----------------------------------------------------------------------===//
// Cube Map Import API
//===----------------------------------------------------------------------===//

//! Import a cube map from 6 individual face files.
/*!
  Loads 6 face images and assembles them into a cube map texture.
  All face images must have identical dimensions and format.

  @param face_paths Array of 6 face paths in order: +X, -X, +Y, -Y, +Z, -Z
  @param preset     Texture preset to apply
  @param policy     Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  std::array<std::filesystem::path, 6> faces = {
    "sky_px.hdr", "sky_nx.hdr",
    "sky_py.hdr", "sky_ny.hdr",
    "sky_pz.hdr", "sky_nz.hdr"
  };
  auto cube = ImportCubeMap(faces,
                            TexturePreset::kHdrEnvironment,
                            D3D12PackingPolicy::Instance());
  ```

  @see ImportCubeMapFromEquirect for panorama conversion
*/
OXGN_CNTT_NDAPI auto ImportCubeMap(
  std::span<const std::filesystem::path, kCubeFaceCount> face_paths,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from 6 individual face files with a custom descriptor.
/*!
  Loads 6 face images and assembles them into a cube map texture.
  The descriptor's texture_type will be set to kTextureCube and array_layers
  to 6.

  @param face_paths Array of 6 face paths in order: +X, -X, +Y, -Y, +Z, -Z
  @param desc       Custom import descriptor
  @param policy     Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportCubeMap(
  std::span<const std::filesystem::path, kCubeFaceCount> face_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from a base path with auto-discovered faces.
/*!
  Attempts to find 6 face files using common naming conventions:
  - `base_path` + `_px`, `_nx`, `_py`, `_ny`, `_pz`, `_nz`
  - `base_path` + `_posx`, `_negx`, `_posy`, `_negy`, `_posz`, `_negz`
  - `base_path` + `_right`, `_left`, `_top`, `_bottom`, `_front`, `_back`

  @param base_path Base path without face suffix
  @param preset    Texture preset to apply
  @param policy    Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  \/\/ Will look for sky_px.hdr, sky_nx.hdr, etc.
  auto cube = ImportCubeMap("textures\/sky",
                            TexturePreset::kHdrEnvironment,
                            D3D12PackingPolicy::Instance());
  ```
*/
OXGN_CNTT_NDAPI auto ImportCubeMap(const std::filesystem::path& base_path,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from an equirectangular panorama.
/*!
  Converts a 2:1 aspect ratio equirectangular (lat-long) panorama into
  a cube map with 6 faces.

  @param equirect_path Path to the equirectangular panorama
  @param face_size     Output cube face resolution (square, in pixels)
  @param preset        Texture preset to apply
  @param policy        Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  auto cube = ImportCubeMapFromEquirect("environment.hdr",
                                        1024,
                                        TexturePreset::kHdrEnvironment,
                                        D3D12PackingPolicy::Instance());
  ```

  @see ConvertEquirectangularToCube for the underlying conversion
*/
OXGN_CNTT_NDAPI auto ImportCubeMapFromEquirect(
  const std::filesystem::path& equirect_path, uint32_t face_size,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from a single image containing all faces in a layout.
/*!
  Loads an image containing all 6 cube faces arranged in a supported layout
  (strip or cross), automatically detects the layout, extracts the faces,
  and produces a cooked cube map.

  ### Supported Layouts

  | Layout            | Aspect | Face Arrangement                      |
  | ----------------- | ------ | ------------------------------------- |
  | Horizontal Strip  | 6:1    | Left-to-right: +X, -X, +Y, -Y, +Z, -Z |
  | Vertical Strip    | 1:6    | Top-to-bottom: +X, -X, +Y, -Y, +Z, -Z |
  | Horizontal Cross  | 4:3    | Standard cross layout                 |
  | Vertical Cross    | 3:4    | Vertical cross layout                 |

  @param path   Path to the layout image
  @param preset Texture preset to apply
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  \/\/ Auto-detect layout from image dimensions
  auto cube = ImportCubeMapFromLayoutImage("skybox_cross.hdr",
                                           TexturePreset::kHdrEnvironment,
                                           D3D12PackingPolicy::Instance());
  ```

  @see DetectCubeMapLayout for layout detection rules
  @see ExtractCubeFacesFromLayout for face extraction
*/
OXGN_CNTT_NDAPI auto ImportCubeMapFromLayoutImage(
  const std::filesystem::path& path, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from a layout image with explicit layout override.
/*!
  Use this overload when automatic layout detection is not desired or when
  the image dimensions are ambiguous.

  @param path   Path to the layout image
  @param layout Explicit layout type (must not be kUnknown)
  @param preset Texture preset to apply
  @param policy Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  \/\/ Force horizontal cross interpretation
  auto cube = ImportCubeMapFromLayoutImage("ambiguous.png",
                                           CubeMapImageLayout::kHorizontalCross,
                                           TexturePreset::kHdrEnvironment,
                                           D3D12PackingPolicy::Instance());
  ```
*/
OXGN_CNTT_NDAPI auto ImportCubeMapFromLayoutImage(
  const std::filesystem::path& path, CubeMapImageLayout layout,
  TexturePreset preset, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a cube map from a layout image with custom descriptor.
/*!
  Use when you need full control over the import configuration beyond
  what presets offer.

  @param path   Path to the layout image
  @param desc   Custom import descriptor (texture_type will be set to kCube)
  @param policy Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportCubeMapFromLayoutImage(
  const std::filesystem::path& path, const TextureImportDesc& desc,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//===----------------------------------------------------------------------===//
// Texture Array Import API
//===----------------------------------------------------------------------===//

//! Import a texture array from multiple files.
/*!
  Loads multiple images and assembles them into a 2D texture array.
  All images must have identical dimensions.

  @param layer_paths Paths to layer images (layer 0, layer 1, ...)
  @param preset      Texture preset to apply
  @param policy      Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  std::vector<std::filesystem::path> layers = {
    "terrain_grass.png",
    "terrain_dirt.png",
    "terrain_rock.png",
  };
  auto array = ImportTextureArray(layers,
                                  TexturePreset::kAlbedo,
                                  D3D12PackingPolicy::Instance());
  ```
*/
OXGN_CNTT_NDAPI auto ImportTextureArray(
  std::span<const std::filesystem::path> layer_paths, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a texture array from multiple files with a custom descriptor.
/*!
  Loads multiple images and assembles them into a 2D texture array.
  The descriptor's texture_type and array_layers will be set appropriately.

  @param layer_paths Paths to layer images (layer 0, layer 1, ...)
  @param desc        Custom import descriptor
  @param policy      Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportTextureArray(
  std::span<const std::filesystem::path> layer_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//===----------------------------------------------------------------------===//
// 3D Texture Import API
//===----------------------------------------------------------------------===//

//! Import a 3D texture from depth slice files.
/*!
  Loads multiple images and assembles them into a 3D texture.
  All images must have identical dimensions.

  @param slice_paths Paths to slice images (slice 0, slice 1, ...)
  @param preset      Texture preset to apply
  @param policy      Packing policy for the target backend
  @return Import result on success, or error

  ### Usage Example

  ```cpp
  std::vector<std::filesystem::path> slices = {
    "volume_slice_000.png",
    "volume_slice_001.png",
    "volume_slice_002.png",
    \/\/ ...
  };
  auto volume = ImportTexture3D(slices,
                                TexturePreset::kData,
                                D3D12PackingPolicy::Instance());
  ```
*/
OXGN_CNTT_NDAPI auto ImportTexture3D(
  std::span<const std::filesystem::path> slice_paths, TexturePreset preset,
  const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//! Import a 3D texture from depth slice files with a custom descriptor.
/*!
  Loads multiple images and assembles them into a 3D texture.
  The descriptor's texture_type and depth will be set appropriately.

  @param slice_paths Paths to slice images (slice 0, slice 1, ...)
  @param desc        Custom import descriptor
  @param policy      Packing policy for the target backend
  @return Import result on success, or error
*/
OXGN_CNTT_NDAPI auto ImportTexture3D(
  std::span<const std::filesystem::path> slice_paths,
  const TextureImportDesc& desc, const ITexturePackingPolicy& policy)
  -> oxygen::Result<TextureImportResult, TextureImportError>;

//===----------------------------------------------------------------------===//
// Builder Pattern for Advanced Control
//===----------------------------------------------------------------------===//

//! Fluent builder for advanced texture import configuration.
/*!
  Use when you need fine-grained control beyond what presets offer.
  The builder collects configuration and produces a `TextureImportResult`
  via the `Build()` method.

  ### Key Features

  - **Fluent API**: Chain method calls for concise configuration
  - **Preset-first**: Start with a preset, then override specific options
  - **Multi-source**: Supports cube faces, array layers, and depth slices

  ### Usage Example

  ```cpp
  auto result = TextureImportBuilder()
    .FromFile("textures\/brick_normal.png")
    .WithPreset(TexturePreset::kNormal)
    .FlipNormalGreen(true)           \/\/ Override preset default
    .WithBc7Quality(Bc7Quality::kHigh)
    .WithMaxMipLevels(4)
    .Build(D3D12PackingPolicy::Instance());
  ```

  @see ImportTexture for simpler single-file imports
*/
class TextureImportBuilder {
public:
  //! Construct an empty builder.
  OXGN_CNTT_API TextureImportBuilder();

  //! Move constructor.
  OXGN_CNTT_API TextureImportBuilder(TextureImportBuilder&& other) noexcept;

  //! Move assignment.
  OXGN_CNTT_API auto operator=(TextureImportBuilder&& other) noexcept
    -> TextureImportBuilder&;

  //! Destructor.
  OXGN_CNTT_API ~TextureImportBuilder();

  //! Copy is disabled.
  TextureImportBuilder(const TextureImportBuilder&) = delete;

  //! Copy assignment is disabled.
  auto operator=(const TextureImportBuilder&) -> TextureImportBuilder& = delete;

  //=== Source Configuration ===----------------------------------------------//

  //! Set the source file path.
  /*!
    For single-source textures (2D, most common case).

    @param path Path to the source image file
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto FromFile(std::filesystem::path path)
    -> TextureImportBuilder&;

  //! Set the source from memory.
  /*!
    For single-source textures when data is already loaded.

    @param data      Raw image data
    @param source_id Identifier for diagnostics
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto FromMemory(std::vector<std::byte> data,
    std::string source_id) -> TextureImportBuilder&;

  //! Add a cube face source file.
  /*!
    For cube map assembly. Call once per face.

    @param face Face identifier (+X, -X, +Y, -Y, +Z, -Z)
    @param path Path to the face image file
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto AddCubeFace(CubeFace face, std::filesystem::path path)
    -> TextureImportBuilder&;

  //! Add an array layer source file.
  /*!
    For texture array assembly.

    @param layer Layer index (0-based)
    @param path  Path to the layer image file
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto AddArrayLayer(uint16_t layer, std::filesystem::path path)
    -> TextureImportBuilder&;

  //! Add a depth slice source file.
  /*!
    For 3D texture assembly.

    @param slice Slice index (0-based)
    @param path  Path to the slice image file
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto AddDepthSlice(uint16_t slice, std::filesystem::path path)
    -> TextureImportBuilder&;

  //=== Preset & Format Configuration ===-------------------------------------//

  //! Apply a preset (recommended starting point).
  /*!
    Sets sensible defaults for the specified preset. Apply first, then
    use other methods to override specific settings.

    @param preset Preset to apply
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithPreset(TexturePreset preset) -> TextureImportBuilder&;

  //! Apply a custom descriptor (alternative to preset).
  /*!
    Use when you need full control over the import configuration.
    This replaces any preset that was previously applied.

    @param desc Custom import descriptor
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithDescriptor(const TextureImportDesc& desc)
    -> TextureImportBuilder&;

  //! Set the texture type explicitly.
  /*!
    Usually inferred from source configuration (single file → 2D,
    cube faces → Cube, etc.). Use this to override.

    @param type Texture type
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithTextureType(TextureType type) -> TextureImportBuilder&;

  //! Set the output format explicitly.
  /*!
    Overrides the preset's default output format.

    @param format Output pixel format
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithOutputFormat(Format format) -> TextureImportBuilder&;

  //! Set the source color space.
  /*!
    Specifies how the pixel values in the source image should be interpreted.
    This is authoring intent, not metadata extracted from the file.

    @param space Color space of the source image (kSRGB or kLinear)
    @return Reference to this builder for chaining

    @see TextureImportDesc::source_color_space
  */
  OXGN_CNTT_API auto WithSourceColorSpace(ColorSpace space)
    -> TextureImportBuilder&;

  //=== Mip Configuration ===-------------------------------------------------//

  //! Set the mip generation policy.
  /*!
    @param policy Mip policy (full chain, limited, or none)
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithMipPolicy(MipPolicy policy) -> TextureImportBuilder&;

  //! Set the maximum number of mip levels.
  /*!
    Only applies when mip policy is `kMaxCount`.

    @param levels Maximum mip levels to generate
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithMaxMipLevels(uint8_t levels) -> TextureImportBuilder&;

  //! Set the mip filter.
  /*!
    @param filter Filter kernel for mip generation
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithMipFilter(MipFilter filter) -> TextureImportBuilder&;

  //=== Content-Specific Options ===------------------------------------------//

  //! Flip the green channel for normal maps.
  /*!
    Use when converting between DirectX and OpenGL normal map conventions.

    @param flip True to flip green channel
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto FlipNormalGreen(bool flip = true) -> TextureImportBuilder&;

  //! Renormalize normals in mip levels.
  /*!
    Ensures normals remain unit-length after mip downsampling.

    @param renormalize True to renormalize (default for normal maps)
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto RenormalizeNormalsInMips(bool renormalize = true)
    -> TextureImportBuilder&;

  //! Flip Y during decode.
  /*!
    Common for textures authored for OpenGL coordinate systems.

    @param flip True to flip vertically during decode
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto FlipYOnDecode(bool flip = true) -> TextureImportBuilder&;

  //=== Compression Options
  //===-------------------------------------------------//

  //! Set BC7 compression quality.
  /*!
    Higher quality increases compression time but may improve visual quality.
    Use `Bc7Quality::kNone` to disable BC7 compression.

    @param quality Compression quality tier
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithBc7Quality(Bc7Quality quality)
    -> TextureImportBuilder&;

  //=== HDR Options
  //===---------------------------------------------------------//

  //! Set HDR handling policy.
  /*!
    Controls behavior when HDR content is encountered with an LDR output format.

    @param handling HDR handling policy
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithHdrHandling(HdrHandling handling)
    -> TextureImportBuilder&;

  //! Set exposure adjustment for HDR content.
  /*!
    Applied before tonemapping when converting HDR to LDR.

    @param ev Exposure adjustment in EV (exposure value)
    @return Reference to this builder for chaining
  */
  OXGN_CNTT_API auto WithExposure(float ev) -> TextureImportBuilder&;

  //=== Build
  //===--------------------------------------------------------------//

  //! Build and cook the texture.
  /*!
    Loads sources, applies configuration, and produces cooked output.
    This method consumes the builder state.

    @param policy Packing policy for the target backend
    @return Import result on success, or error
  */
  OXGN_CNTT_NDAPI auto Build(const ITexturePackingPolicy& policy)
    -> oxygen::Result<TextureImportResult, TextureImportError>;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::content::import
