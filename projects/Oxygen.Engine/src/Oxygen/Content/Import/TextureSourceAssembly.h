//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Content/Import/ScratchImage.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Content/api_export.h>

namespace oxygen::content::import {

//! Cube face identifiers matching D3D12/Vulkan face ordering convention.
/*!
  Face order corresponds to array layer indices for cube map textures.

  ### Oxygen Coordinate System

  Oxygen uses a **Z-up, right-handed** coordinate system:
  - +X = Right
  - +Y = Forward
  - +Z = Up

  The cube face directions correspond to axis-aligned viewing directions in this
  coordinate system.
*/
enum class CubeFace : uint8_t {
  // clang-format off
  kPositiveX = 0,  //!< +X face (right)
  kNegativeX = 1,  //!< -X face (left)
  kPositiveY = 2,  //!< +Y face (forward)
  kNegativeY = 3,  //!< -Y face (back)
  kPositiveZ = 4,  //!< +Z face (up)
  kNegativeZ = 5,  //!< -Z face (down)
  // clang-format on
};

//! Number of faces in a cube map.
inline constexpr uint8_t kCubeFaceCount = 6;

//! String representation of CubeFace enum values.
OXGN_CNTT_NDAPI auto to_string(CubeFace face) -> const char*;

//! Identifies a subresource within a multi-source texture.
/*!
  Used to map source image data to a specific location in the assembled texture.

  - For **2D arrays**: set `array_layer` to the layer index
  - For **cube maps**: set `array_layer` to the face index (0-5)
  - For **3D textures**: set `depth_slice` to the slice index
  - For **pre-authored mips**: set `mip_level` to the mip index
*/
struct SubresourceId {
  uint16_t array_layer = 0; //!< Array layer (or cube face index 0-5)
  uint16_t mip_level = 0; //!< Mip level (0 = highest resolution)
  uint16_t depth_slice = 0; //!< Depth slice for 3D textures

  //! Equality comparison.
  [[nodiscard]] auto operator==(const SubresourceId& other) const noexcept
    -> bool
    = default;
};

//! A single source file mapped to a subresource.
/*!
  Represents one input image (as raw bytes) that will be decoded and placed
  into a specific subresource location in the final assembled texture.
*/
struct TextureSource {
  std::vector<std::byte> bytes; //!< Source file contents (encoded image data)
  SubresourceId subresource; //!< Target subresource location
  std::string source_id; //!< Diagnostic identifier (filename, path)
};

//! Collection of source files for multi-source texture assembly.
/*!
  `TextureSourceSet` maps input files to subresources (array layers, mip levels,
  3D slices). Use this class to assemble cube maps, texture arrays, or 3D
  textures from multiple input images.

  ### Usage Pattern

  ```cpp
  TextureSourceSet sources;

  \/\/ For cube maps:
  sources.AddCubeFace(CubeFace::kPositiveX, LoadFile("sky_px.hdr"),
  "sky_px.hdr"); sources.AddCubeFace(CubeFace::kNegativeX,
  LoadFile("sky_nx.hdr"), "sky_nx.hdr");
  \/\/ ... add remaining faces

  \/\/ For texture arrays:
  sources.AddArrayLayer(0, LoadFile("layer0.png"), "layer0.png");
  sources.AddArrayLayer(1, LoadFile("layer1.png"), "layer1.png");

  \/\/ For 3D textures:
  sources.AddDepthSlice(0, LoadFile("slice0.png"), "slice0.png");
  sources.AddDepthSlice(1, LoadFile("slice1.png"), "slice1.png");
  ```

  @see CookTexture, TextureImportDesc
*/
class TextureSourceSet {
public:
  //! Add a source file with explicit subresource targeting.
  /*!
    @param source The texture source with bytes, subresource ID, and diagnostic
    name
  */
  OXGN_CNTT_API void Add(TextureSource source);

  //! Add a source file for a specific array layer.
  /*!
    Convenience overload for texture arrays. Sets `array_layer` in the
    subresource ID.

    @param array_layer Array layer index (0-based)
    @param bytes       Encoded image data
    @param source_id   Diagnostic identifier for error messages
  */
  OXGN_CNTT_API void AddArrayLayer(
    uint16_t array_layer, std::vector<std::byte> bytes, std::string source_id);

  //! Add a source file for a specific cube face.
  /*!
    Convenience overload for cube maps. Maps the face to the corresponding
    array layer index.

    @param face      Cube face identifier
    @param bytes     Encoded image data
    @param source_id Diagnostic identifier for error messages
  */
  OXGN_CNTT_API void AddCubeFace(
    CubeFace face, std::vector<std::byte> bytes, std::string source_id);

  //! Add a source file for a specific depth slice (3D textures).
  /*!
    Convenience overload for 3D textures. Sets `depth_slice` in the subresource
    ID.

    @param slice_index Depth slice index (0-based)
    @param bytes       Encoded image data
    @param source_id   Diagnostic identifier for error messages
  */
  OXGN_CNTT_API void AddDepthSlice(
    uint16_t slice_index, std::vector<std::byte> bytes, std::string source_id);

  //! Add a source file for a specific mip level.
  /*!
    Use this when providing pre-authored mip levels instead of generating them.

    @param array_layer Array layer index (0 for non-array textures)
    @param mip_level   Mip level index (0 = highest resolution)
    @param bytes       Encoded image data
    @param source_id   Diagnostic identifier for error messages
  */
  OXGN_CNTT_API void AddMipLevel(uint16_t array_layer, uint16_t mip_level,
    std::vector<std::byte> bytes, std::string source_id);

  //! Get all sources in the set.
  [[nodiscard]] auto Sources() const noexcept -> std::span<const TextureSource>
  {
    return sources_;
  }

  //! Get the number of sources in the set.
  [[nodiscard]] auto Count() const noexcept -> size_t
  {
    return sources_.size();
  }

  //! Check if the set is empty.
  [[nodiscard]] auto IsEmpty() const noexcept -> bool
  {
    return sources_.empty();
  }

  //! Clear all sources from the set.
  OXGN_CNTT_API void Clear() noexcept;

  //! Get a specific source by index.
  /*!
    @param index Source index (must be less than Count())
    @return Reference to the source at the given index
    @throws std::out_of_range if index is out of bounds
  */
  OXGN_CNTT_NDAPI auto GetSource(size_t index) const -> const TextureSource&;

private:
  std::vector<TextureSource> sources_;
};

//=== Cube Map Assembly Helpers
//===-----------------------------------------===//

//! 3D direction vector for cube face calculations.
struct CubeFaceDirection {
  float x; //!< X component
  float y; //!< Y component
  float z; //!< Z component
};

//! Cube face basis vectors for sampling calculations.
/*!
  Each face has a center direction (where the face points), and right/up vectors
  defining the face's 2D coordinate system for texel lookups.
*/
struct CubeFaceBasis {
  CubeFaceDirection center; //!< Direction at face center (normal)
  CubeFaceDirection right; //!< Right vector (positive U direction)
  CubeFaceDirection up; //!< Up vector (positive V direction)
};

//! Cube face basis vectors for Oxygen's Z-up, right-handed coordinate system.
/*!
  These vectors define the orientation of each cube face for sampling
  equirectangular panoramas or computing cubemap directions.

  ### Oxygen Coordinate System

  - **+X = Right**, **+Y = Forward**, **+Z = Up**
  - Right-handed coordinate system

  ### Face Definitions

  | Face | Center Direction | Right | Up |
  | ---- | ---------------- | ----- | -- |
  | +X   | (+1, 0, 0)       | (0, +1, 0) | (0, 0, +1) |
  | -X   | (-1, 0, 0)       | (0, -1, 0) | (0, 0, +1) |
  | +Y   | (0, +1, 0)       | (-1, 0, 0) | (0, 0, +1) |
  | -Y   | (0, -1, 0)       | (+1, 0, 0) | (0, 0, +1) |
  | +Z   | (0, 0, +1)       | (+1, 0, 0) | (0, -1, 0) |
  | -Z   | (0, 0, -1)       | (+1, 0, 0) | (0, +1, 0) |

  These match the GPU cubemap convention where sampling direction (x, y, z) maps
  to the face whose axis has the largest absolute component.
*/
// clang-format off
inline constexpr std::array<CubeFaceBasis, kCubeFaceCount> kCubeFaceBases = {{
  // +X: center (+1, 0, 0), right (0, +1, 0), up (0, 0, +1)
  { {+1.0F, 0.0F, 0.0F}, {0.0F, +1.0F, 0.0F}, {0.0F, 0.0F, +1.0F} },
  // -X: center (-1, 0, 0), right (0, -1, 0), up (0, 0, +1)
  { {-1.0F, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, +1.0F} },
  // +Y: center (0, +1, 0), right (-1, 0, 0), up (0, 0, +1)
  { {0.0F, +1.0F, 0.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, +1.0F} },
  // -Y: center (0, -1, 0), right (+1, 0, 0), up (0, 0, +1)
  { {0.0F, -1.0F, 0.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, +1.0F} },
  // +Z: center (0, 0, +1), right (+1, 0, 0), up (0, -1, 0)
  { {0.0F, 0.0F, +1.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F} },
  // -Z: center (0, 0, -1), right (+1, 0, 0), up (0, +1, 0)
  { {0.0F, 0.0F, -1.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, +1.0F, 0.0F} },
}};
// clang-format on

//! Cube face basis vectors for standard GPU cubemap convention.
/*!
  These vectors match the standard cubemap layout used by D3D12, OpenGL, and
  Vulkan. All major graphics APIs share the same cubemap face orientation
  convention.

  Use these when generating cubemaps from equirectangular panoramas that will
  be sampled using standard TextureCube sampling.

  ### Standard GPU Cubemap Convention (Y-up)

  - **+X = Right**, **+Y = Up**, **+Z = Forward**
  - Face array order: +X, -X, +Y, -Y, +Z, -Z
  - Shared by D3D12, OpenGL, and Vulkan

  ### Face Definitions

  | Face | Center Direction | Right       | Up          |
  | ---- | ---------------- | ----------- | ----------- |
  | +X   | (+1, 0, 0)       | (0, 0, -1)  | (0, +1, 0)  |
  | -X   | (-1, 0, 0)       | (0, 0, +1)  | (0, +1, 0)  |
  | +Y   | (0, +1, 0)       | (+1, 0, 0)  | (0, 0, -1)  |
  | -Y   | (0, -1, 0)       | (+1, 0, 0)  | (0, 0, +1)  |
  | +Z   | (0, 0, +1)       | (+1, 0, 0)  | (0, +1, 0)  |
  | -Z   | (0, 0, -1)       | (-1, 0, 0)  | (0, +1, 0)  |
*/
// clang-format off
inline constexpr std::array<CubeFaceBasis, kCubeFaceCount> kGpuCubeFaceBases = {{
  // +X: center (+1, 0, 0), right (0, 0, -1), up (0, +1, 0)
  { {+1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F}, {0.0F, +1.0F, 0.0F} },
  // -X: center (-1, 0, 0), right (0, 0, +1), up (0, +1, 0)
  { {-1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, +1.0F}, {0.0F, +1.0F, 0.0F} },
  // +Y (top): center (0, +1, 0), right (+1, 0, 0), up (0, 0, -1)
  { {0.0F, +1.0F, 0.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F} },
  // -Y (bottom): center (0, -1, 0), right (+1, 0, 0), up (0, 0, +1)
  { {0.0F, -1.0F, 0.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, 0.0F, +1.0F} },
  // +Z: center (0, 0, +1), right (+1, 0, 0), up (0, +1, 0)
  { {0.0F, 0.0F, +1.0F}, {+1.0F, 0.0F, 0.0F}, {0.0F, +1.0F, 0.0F} },
  // -Z: center (0, 0, -1), right (-1, 0, 0), up (0, +1, 0)
  { {0.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, 0.0F}, {0.0F, +1.0F, 0.0F} },
}};
// clang-format on

//! Get the basis vectors for a specific cube face.
/*!
  @param face The cube face to get basis vectors for
  @return Reference to the basis vectors for the face
*/
[[nodiscard]] inline auto GetCubeFaceBasis(CubeFace face) noexcept
  -> const CubeFaceBasis&
{
  return kCubeFaceBases[static_cast<size_t>(face)];
}

//! Compute a normalized 3D direction from face UV coordinates.
/*!
  Given a cube face and normalized UV coordinates (0-1 range), computes the
  corresponding world-space direction vector.

  @param face The cube face
  @param u    Horizontal coordinate (0 = left, 1 = right)
  @param v    Vertical coordinate (0 = bottom, 1 = top)
  @return Normalized direction vector (magnitude = 1)
*/
OXGN_CNTT_NDAPI auto ComputeCubeDirection(
  CubeFace face, float u, float v) noexcept -> CubeFaceDirection;

//! Assemble a cube map from 6 individual face images.
/*!
  Combines 6 separate face images into a single `ScratchImage` with
  `texture_type = kTextureCube` and `array_layers = 6`.

  All face images must have identical dimensions and format.

  @param faces Array of 6 face images in CubeFace order (+X, -X, +Y, -Y, +Z, -Z)
  @return Assembled cube map or error if faces are incompatible

  ### Example

  ```cpp
  std::array<ScratchImage, 6> faces = {
    DecodeToScratchImage(LoadFile("sky_px.hdr")),
    DecodeToScratchImage(LoadFile("sky_nx.hdr")),
    \/\/ ... remaining faces
  };
  auto cube = AssembleCubeFromFaces(faces);
  ```
*/
OXGN_CNTT_NDAPI auto AssembleCubeFromFaces(
  std::span<const ScratchImage, kCubeFaceCount> faces)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Options for equirectangular to cube map conversion.
/*!
  Controls the conversion process when generating cube faces from an
  equirectangular (lat-long) panorama image.
*/
struct EquirectToCubeOptions {
  uint32_t face_size = 512; //!< Output face resolution (square)
  MipFilter sample_filter = MipFilter::kKaiser; //!< Sampling filter for quality
};

//! Convert an equirectangular panorama to a cube map.
/*!
  Converts a 2:1 aspect ratio equirectangular (lat-long) panorama image into
  a cube map with 6 faces.

  The conversion samples the panorama for each texel of each cube face,
  computing the appropriate direction vector and mapping to spherical
  coordinates.

  @param equirect  The equirectangular source image (width should be 2x height)
  @param options   Conversion options controlling face size and sampling filter
  @return Assembled cube map with 6 faces or error

  ### Algorithm

  For each face and texel (u, v):
  1. Compute 3D direction from face basis and UV
  2. Convert direction to spherical (θ, φ)
  3. Map to equirect UV: u = (θ/π + 1)/2, v = (φ/(π/2) + 1)/2
  4. Sample equirect image with chosen filter

  ### Example

  ```cpp
  auto hdr = DecodeToScratchImage(LoadFile("environment.hdr"));
  EquirectToCubeOptions opts{ .face_size = 1024 };
  auto cube = ConvertEquirectangularToCube(hdr.value(), opts);
  ```
*/
OXGN_CNTT_NDAPI auto ConvertEquirectangularToCube(
  const ScratchImage& equirect, const EquirectToCubeOptions& options)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//===----------------------------------------------------------------------===//
// Cube Map Layout Detection and Extraction
//===----------------------------------------------------------------------===//

//! Layout of a cube map within a single image.
/*!
  Identifies how 6 cube faces are arranged within a single source image.

  ### Supported Layouts

  | Layout            | Aspect | Face Arrangement                          |
  | ----------------- | ------ | ----------------------------------------- |
  | kHorizontalStrip  | 6:1    | Left-to-right: +X, -X, +Y, -Y, +Z, -Z     |
  | kVerticalStrip    | 1:6    | Top-to-bottom: +X, -X, +Y, -Y, +Z, -Z     |
  | kHorizontalCross  | 4:3    | Standard cross (see diagram below)        |
  | kVerticalCross    | 3:4    | Vertical cross (see diagram below)        |

  ### Cross Layout Diagrams

  **Horizontal Cross (4:3):**
  ```
      [+Y]
  [-X][+Z][+X][-Z]
      [-Y]
  ```

  **Vertical Cross (3:4):**
  ```
      [+Y]
  [-X][+Z][+X]
      [-Y]
      [-Z]
  ```

  @see DetectCubeMapLayout, ExtractCubeFacesFromLayout
*/
enum class CubeMapImageLayout : uint8_t {
  // clang-format off
  kUnknown         = 0,  //!< Cannot detect or invalid layout
  kHorizontalStrip = 1,  //!< 6:1 aspect, faces in a row
  kVerticalStrip   = 2,  //!< 1:6 aspect, faces in a column
  kHorizontalCross = 3,  //!< 4:3 aspect, cross arrangement
  kVerticalCross   = 4,  //!< 3:4 aspect, vertical cross arrangement
  // clang-format on
};

//! String representation of CubeMapImageLayout enum values.
OXGN_CNTT_NDAPI auto to_string(CubeMapImageLayout layout) -> const char*;

//! Result of cube map layout detection.
/*!
  Contains the detected layout type and computed face size. The face size
  is derived from the image dimensions and is always square.
*/
struct CubeMapLayoutDetection {
  CubeMapImageLayout layout = CubeMapImageLayout::kUnknown; //!< Detected layout
  uint32_t face_size = 0; //!< Size of each square face in pixels
};

//! Detect cube map layout from image dimensions.
/*!
  Analyzes image width and height to determine if they match a known
  cube map layout. All layouts require square faces.

  @param width  Image width in pixels
  @param height Image height in pixels
  @return Detection result, or nullopt if no valid layout detected

  ### Detection Rules

  | Condition                          | Detected Layout     | Face Size |
  | ---------------------------------- | ------------------- | --------- |
  | width == height * 6                | kHorizontalStrip    | height    |
  | height == width * 6                | kVerticalStrip      | width     |
  | width % 4 == 0, height % 3 == 0,   |                     |           |
  |   width/4 == height/3              | kHorizontalCross    | width/4   |
  | width % 3 == 0, height % 4 == 0,   |                     |           |
  |   width/3 == height/4              | kVerticalCross      | width/3   |

  @see ExtractCubeFacesFromLayout
*/
OXGN_CNTT_NDAPI auto DetectCubeMapLayout(uint32_t width,
  uint32_t height) noexcept -> std::optional<CubeMapLayoutDetection>;

//! Detect cube map layout from a ScratchImage.
/*!
  Convenience overload that extracts dimensions from the image metadata.

  @param image Source image to analyze
  @return Detection result, or nullopt if no valid layout detected

  @see DetectCubeMapLayout(uint32_t, uint32_t)
*/
OXGN_CNTT_NDAPI auto DetectCubeMapLayout(const ScratchImage& image) noexcept
  -> std::optional<CubeMapLayoutDetection>;

//! Extract 6 cube faces from a layout image.
/*!
  Extracts the 6 cube faces from a single image containing all faces arranged
  in one of the supported layouts. The output is a single ScratchImage with
  `texture_type = kTextureCube` and `array_layers = 6`.

  ### Face Mapping

  All layouts use GPU-standard face ordering: +X, -X, +Y, -Y, +Z, -Z.

  **Strip layouts** assume faces are in this order from left-to-right
  (horizontal) or top-to-bottom (vertical).

  **Cross layouts** use standard cube map cross conventions:
  - Center face is +Z (front)
  - Adjacent faces are -X (left), +X (right), +Y (top), -Y (bottom)
  - Opposite face is -Z (back)

  @param layout_image Source image containing all 6 faces
  @param layout       Layout type (must not be kUnknown)
  @return Assembled cube map ScratchImage on success, or error

  @note This function handles format-independent extraction. The pixel format
        of the output matches the input.

  @see DetectCubeMapLayout, AssembleCubeFromFaces
*/
OXGN_CNTT_NDAPI auto ExtractCubeFacesFromLayout(
  const ScratchImage& layout_image, CubeMapImageLayout layout)
  -> oxygen::Result<ScratchImage, TextureImportError>;

//! Extract cube faces with automatic layout detection.
/*!
  Convenience function that detects the layout and extracts faces in one call.

  @param layout_image Source image containing all 6 faces
  @return Assembled cube map ScratchImage on success, or error if layout
          cannot be detected or extraction fails

  @see DetectCubeMapLayout, ExtractCubeFacesFromLayout
*/
OXGN_CNTT_NDAPI auto ExtractCubeFacesFromLayout(
  const ScratchImage& layout_image)
  -> oxygen::Result<ScratchImage, TextureImportError>;

} // namespace oxygen::content::import
