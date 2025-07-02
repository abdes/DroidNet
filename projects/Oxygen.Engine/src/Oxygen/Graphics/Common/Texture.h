//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string_view>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics {

class DescriptorHandle;

OXYGEN_GFX_API auto to_string(TextureType value) -> const char*;

struct TextureDesc {
  uint32_t width = 1;
  uint32_t height = 1;
  uint32_t depth = 1;
  uint32_t array_size = 1;
  uint32_t mip_levels = 1;
  uint32_t sample_count = 1;
  uint32_t sample_quality = 0;
  Format format = Format::kUnknown;
  TextureType texture_type = TextureType::kTexture2D;

  std::string debug_name { "Texture" };

  bool is_shader_resource = false;
  bool is_render_target = false;
  bool is_uav = false;
  bool is_typeless = false;
  bool is_shading_rate_surface = false;

  // TODO: consider supporting shared textures
  // SharedResourceFlags shared_resource_flags = SharedResourceFlags::None;

  // TODO: consider supporting tiled and virtual resources
  // Indicates that the texture is created with no backing memory,
  // and memory is bound to the texture later using bindTextureMemory.
  // On DX12, the texture resource is created at the time of memory binding.
  // bool is_virtual = false;
  // bool is_tiled = false;

  Color clear_value;
  bool use_clear_value = false;

  ResourceStates initial_state = ResourceStates::kUndefined;
  ResourceAccessMode cpu_access = ResourceAccessMode::kImmutable;
};

using MipLevel = uint32_t;
using ArraySlice = uint32_t;

//! Represents a specific section of texture data, defined by coordinates,
//! dimensions, mip level and array slice.
/*!
 TextureSlice allows accessing a specific region within a texture resource. It
 defines both the position (x, y, z coordinates) and dimensions (width, height,
 depth) of the region, as well as which mip level and array slice to target.

 Mipmaps are progressively smaller versions of the original texture that:
 - Reduce texture aliasing artifacts by providing pre-filtered versions
 - Improve performance through better texture caching
 - Are organized as a hierarchy where each level is half the size of the
   previous level

 In graphics API terminology:
 - D3D12: "Mip Slice" refers to all mips at the same level across array
   elements, and "Array Slice" refers to all mips belonging to the same texture
   element
 - Vulkan: Uses "mip level" to identify the mip level and "array layer" for the
   array index. For cube maps, each face is treated as a separate array layer,
   similar to D3D12's array slices.

 Both terminologies are particularly relevant for texture arrays and cube maps
 where each face represents a distinct slice or layer.
*/
struct TextureSlice {
  uint32_t x = 0; //!< X offset into the texture (in texels).
  uint32_t y = 0; //!< Y offset into the texture (in texels).
  uint32_t z = 0; //!< Z offset into the texture (in texels, for 3D textures).

  //! Width of the region in texels.
  //! Value of uint32_t(-1) means the entire width.
  uint32_t width = static_cast<uint32_t>(-1);

  //! Height of the region in texels.
  //! Value of uint32_t(-1) means the entire height.
  uint32_t height = static_cast<uint32_t>(-1);

  //! Depth of the region in texels.
  //! Value of uint32_t(-1) means the entire depth.
  uint32_t depth = static_cast<uint32_t>(-1);

  //! Mip level to access (0 is the largest/original texture).
  MipLevel mip_level = 0;

  //! Array slice to access (relevant for texture arrays and cube maps).
  ArraySlice array_slice = 0;

  //! Resolves special dimension values into actual texture dimensions for the
  //! specified mip level.
  /*!
   When width, height, or depth is set to uint32_t(-1), this method calculates
   the actual dimensions based on the texture description and mip level. This
   is particularly useful when:
   - You want to refer to the entire width/height/depth of a texture at a
     specific mip level.
   - You need to account for mip level scaling (each mip level reduces
     dimensions by half).

   The method ensures proper dimension calculations with mip chain reduction
   (>> mipLevel) while guaranteeing dimensions are at least 1 texel.

   \param desc The base texture description.
   \return A new TextureSlice with all dimensions fully resolved to
   actual values.
   */
  [[nodiscard]] OXYGEN_GFX_API auto Resolve(const TextureDesc& desc) const
    -> TextureSlice;
};

//! Defines a set of texture sub-resources across multiple mip levels and array
//! slices.
/*!
 Provides a way to reference ranges of sub-resources within a texture. This is
 useful for operations that need to target specific mip levels or array slices,
 such as resource transitions, copies, and barriers.

 Special values can be used to reference all mip levels or array slices, which
 will be resolved to appropriate values when needed based on the texture
 description.
 */
struct TextureSubResourceSet {
  //! Special value indicating all mip levels of a texture.
  static constexpr MipLevel kAllMipLevels = static_cast<MipLevel>(-1);

  //! Special value indicating all array slices of a texture.
  static constexpr ArraySlice kAllArraySlices = static_cast<ArraySlice>(-1);

  //! Base mip level (0 is the highest resolution level).
  MipLevel base_mip_level = 0;

  //! Number of mip levels to include (1 means just the base level).
  MipLevel num_mip_levels = 1;

  //! Base array slice (0 is the first array element).
  ArraySlice base_array_slice = 0;

  //! Number of array slices to include (1 means just the base slice).
  ArraySlice num_array_slices = 1;

  //! Returns a TextureSubResourceSet that represents the entire texture.
  static constexpr auto EntireTexture() -> TextureSubResourceSet
  {
    return { .base_mip_level = 0,
      .num_mip_levels = TextureSubResourceSet::kAllMipLevels,
      .base_array_slice = 0,
      .num_array_slices = TextureSubResourceSet::kAllArraySlices };
  }

  //! Resolves any special values to concrete ranges based on the texture
  //! description.
  /*!
   Converts kAllMipLevels and kAllArraySlices to actual ranges based on the
   texture. Also handles dimension-specific array slice resolution for
   different texture types.

   \param desc The base texture description.
   \param single_mip_level If true, forces the result to target only a single
   mip level.
   \return A new TextureSubResourceSet with all values resolved to concrete
   ranges.
   */
  [[nodiscard]] OXYGEN_GFX_API auto Resolve(const TextureDesc& desc,
    bool single_mip_level) const -> TextureSubResourceSet;

  //! Checks if this set references the entire texture (all mips and slices).
  [[nodiscard]] OXYGEN_GFX_API auto IsEntireTexture(
    const TextureDesc& desc) const -> bool;

  auto operator==(const TextureSubResourceSet& other) const -> bool
  {
    return base_mip_level == other.base_mip_level
      && num_mip_levels == other.num_mip_levels
      && base_array_slice == other.base_array_slice
      && num_array_slices == other.num_array_slices;
  }

  auto operator!=(const TextureSubResourceSet& other) const -> bool
  {
    return !(*this == other);
  }
};

//! Describes a texture view for bindless rendering.
/*!
 TextureViewDescription contains all the necessary information to create a
 native view for a texture, including view type, visibility, format, dimension,
 and sub-resource set.
*/
struct TextureViewDescription {
  //! The type of view to create (SRV, UAV, RTV, DSV).
  ResourceViewType view_type { ResourceViewType::kTexture_SRV };

  //! The visibility of the view (shader visible, etc.).
  DescriptorVisibility visibility { DescriptorVisibility::kShaderVisible };

  //! The format of the texture view (e.g., RGBA8, D24S8).
  //! This may differ from the texture format in some cases (e.g., typeless
  //! textures).
  Format format { Format::kUnknown };

  //! The dimension of the texture (1D, 2D, 3D, etc.).
  //! This may differ from the texture dimension in some cases (e.g., typeless
  //! textures).
  TextureType dimension { TextureType::kUnknown };

  //! The sub-resource set to use for the view.
  //! This defines which mip levels and array slices to include in the view.
  TextureSubResourceSet sub_resources {
    TextureSubResourceSet::EntireTexture()
  };

  //! Indicates if the view is read-only (for DSVs).
  bool is_read_only_dsv { false };

  auto operator==(const TextureViewDescription&) const -> bool = default;
};

} // namespace oxygen::graphics

//! Hash specialization for TextureSubResourceSet.
/*!
 Enables TextureSubResourceSet to be used as key in hash-based containers like
 std::unordered_map or std::unordered_set.

 Combines hashes of all the TextureSubResourceSet members using the HashCombine
 function to generate a consistent, well-distributed hash value.
*/
template <> struct std::hash<oxygen::graphics::TextureSubResourceSet> {
  auto operator()(
    oxygen::graphics::TextureSubResourceSet const& s) const noexcept
    -> std::size_t
  {
    size_t hash = 0;
    oxygen::HashCombine(hash, s.base_mip_level);
    oxygen::HashCombine(hash, s.num_mip_levels);
    oxygen::HashCombine(hash, s.base_array_slice);
    oxygen::HashCombine(hash, s.num_array_slices);
    return hash;
  }
};

//! Hash specialization for TextureViewDescription.
/*!
 Enables TextureViewDescription to be used as key in hash-based containers like
 std::unordered_map or std::unordered_set.

 Combines hashes of all the TextureViewDescription members using the HashCombine
 function to generate a consistent, well-distributed hash value.
*/
template <> struct std::hash<oxygen::graphics::TextureViewDescription> {
  auto operator()(
    oxygen::graphics::TextureViewDescription const& s) const noexcept
    -> std::size_t
  {
    size_t hash
      = std::hash<oxygen::graphics::TextureSubResourceSet>()(s.sub_resources);
    oxygen::HashCombine(hash, s.view_type);
    oxygen::HashCombine(hash, s.visibility);
    oxygen::HashCombine(hash, s.format);
    oxygen::HashCombine(hash, s.dimension);
    oxygen::HashCombine(hash, s.is_read_only_dsv);
    return hash;
  }
};

namespace oxygen::graphics {

class Texture : public Composition,
                public Named,
                public std::enable_shared_from_this<Texture> {
public:
  using ViewDescriptionT = TextureViewDescription;

  explicit Texture(std::string_view name)
  {
    AddComponent<ObjectMetaData>(name);
  }

  OXYGEN_GFX_API ~Texture() override;

  OXYGEN_MAKE_NON_COPYABLE(Texture)
  OXYGEN_DEFAULT_MOVABLE(Texture)

  //! Gets the descriptor for this texture.
  [[nodiscard]] virtual auto GetDescriptor() const -> const TextureDesc& = 0;

  //! Gets the native resource handle for the texture.
  [[nodiscard]] virtual auto GetNativeResource() const -> NativeObject = 0;
  [[nodiscard]] OXYGEN_GFX_API auto GetNativeView(
    const DescriptorHandle& view_handle,
    const TextureViewDescription& view_desc) const -> NativeObject;

  //! Gets the name of the texture.
  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return GetComponent<ObjectMetaData>().GetName();
  }

  //! Sets the name of the texture.
  void SetName(const std::string_view name) noexcept override
  {
    GetComponent<ObjectMetaData>().SetName(name);
  }

protected:
  //! Gets a shader resource view for the texture.
  [[nodiscard]] OXYGEN_GFX_API virtual auto CreateShaderResourceView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeObject
    = 0;

  //! Gets an unordered access view for the texture.
  [[nodiscard]] OXYGEN_GFX_API virtual auto CreateUnorderedAccessView(
    const DescriptorHandle& view_handle, Format format, TextureType dimension,
    TextureSubResourceSet sub_resources) const -> NativeObject
    = 0;

  //! Gets a render target view for the texture.
  [[nodiscard]] OXYGEN_GFX_API virtual auto CreateRenderTargetView(
    const DescriptorHandle& view_handle, Format format,
    TextureSubResourceSet sub_resources) const -> NativeObject
    = 0;

  //! Gets a depth stencil view for the texture.
  [[nodiscard]] OXYGEN_GFX_API virtual auto CreateDepthStencilView(
    const DescriptorHandle& view_handle, Format format,
    TextureSubResourceSet sub_resources, bool is_read_only) const
    -> NativeObject
    = 0;
};

// Ensure Texture satisfies ResourceWithViews
static_assert(oxygen::graphics::ResourceWithViews<oxygen::graphics::Texture>,
  "Texture must satisfy ResourceWithViews");

} // namespace oxygen::graphics
