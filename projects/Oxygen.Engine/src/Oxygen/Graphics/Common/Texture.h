//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Composition/ObjectMetaData.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/Format.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

namespace oxygen::graphics {

enum class TextureDimension : uint8_t {
    kUnknown,
    kTexture1D,
    kTexture1DArray,
    kTexture2D,
    kTexture2DArray,
    kTextureCube,
    kTextureCubeArray,
    kTexture2DMS,
    kTexture2DMSArray,
    kTexture3D
};

OXYGEN_GFX_API auto to_string(TextureDimension value) -> const char*;

struct TextureDesc {
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t array_size = 1;
    uint32_t mip_levels = 1;
    uint32_t sample_count = 1;
    uint32_t sample_quality = 0;
    Format format = Format::kUnknown;
    TextureDimension dimension = TextureDimension::kTexture2D;

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
    [[nodiscard]] OXYGEN_GFX_API auto Resolve(const TextureDesc& desc) const -> TextureSlice;
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
    [[nodiscard]] OXYGEN_GFX_API auto Resolve(
        const TextureDesc& desc, bool single_mip_level) const -> TextureSubResourceSet;

    //! Checks if this set references the entire texture (all mips and slices).
    [[nodiscard]] OXYGEN_GFX_API auto IsEntireTexture(const TextureDesc& desc) const -> bool;

    auto operator==(const TextureSubResourceSet& other) const -> bool
    {
        return base_mip_level == other.base_mip_level
            && num_mip_levels == other.num_mip_levels
            && base_array_slice == other.base_array_slice
            && num_array_slices == other.num_array_slices;
    }

    auto operator!=(const TextureSubResourceSet& other) const -> bool { return !(*this == other); }
};

class Texture : public Composition, public Named {
public:
    //! Predefined constant representing all sub-resources in a texture
    static constexpr TextureSubResourceSet kAllSubResources = {
        .base_mip_level = 0,
        .num_mip_levels = TextureSubResourceSet::kAllMipLevels,
        .base_array_slice = 0,
        .num_array_slices = TextureSubResourceSet::kAllArraySlices
    };

    explicit Texture(std::string_view name)
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Texture() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Texture)
    OXYGEN_DEFAULT_MOVABLE(Texture)

    [[nodiscard]] virtual auto GetNativeResource() const -> NativeObject = 0;
    [[nodiscard]] virtual auto GetDescriptor() const -> const TextureDesc& = 0;

    [[nodiscard]] virtual auto GetShaderResourceView(
        Format format,
        TextureSubResourceSet sub_resources,
        TextureDimension dimension) -> NativeObject
        = 0;

    [[nodiscard]] virtual auto GetUnorderedAccessView(
        Format format,
        TextureSubResourceSet sub_resources,
        TextureDimension dimension) -> NativeObject
        = 0;

    [[nodiscard]] virtual auto GetRenderTargetView(
        Format format,
        TextureSubResourceSet sub_resources) -> NativeObject
        = 0;

    [[nodiscard]] virtual auto GetDepthStencilView(
        Format format,
        TextureSubResourceSet sub_resources,
        bool is_read_only) -> NativeObject
        = 0;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(const std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }
};

//! Describes a texture binding used to manage SRV/VkImageView per texture.
/*!
 TextureBindingKey extends TextureSubResourceSet with additional information
 needed to create appropriate texture view objects in different graphics APIs.

 This struct acts as a key for caching texture views, allowing the engine to
 reuse existing views when the same texture is bound with identical parameters
 multiple times, improving performance and reducing resource overhead.
*/
struct TextureBindingKey : TextureSubResourceSet {
    //! Format to use when creating the view (can differ from the texture's
    //! native format).
    Format format { Format::kUnknown };

    //! Indicates if this is a read-only depth-stencil view.
    //! Used in APIs like D3D12 that have separate read-only DSV states.
    bool is_read_only_dsv { false };

    TextureBindingKey() = default;

    TextureBindingKey(
        const TextureSubResourceSet& b,
        const Format _format,
        const bool _is_read_only_dsv = false)
        : TextureSubResourceSet(b)
        , format(_format)
        , is_read_only_dsv(_is_read_only_dsv)
    {
    }

    auto operator==(const TextureBindingKey& other) const -> bool
    {
        return format == other.format
            && static_cast<const TextureSubResourceSet&>(*this) == other
            && is_read_only_dsv == other.is_read_only_dsv;
    }
};

} // namespace oxygen::graphics

//! Hash specialization for TextureSubResourceSet.
/*!
 Enables TextureSubResourceSet to be used as key in hash-based containers like
 std::unordered_map or std::unordered_set.

 Combines hashes of all the TextureSubResourceSet members using the HashCombine
 function to generate a consistent, well-distributed hash value.
*/
template <>
struct std::hash<oxygen::graphics::TextureSubResourceSet> {
    auto operator()(oxygen::graphics::TextureSubResourceSet const& s) const noexcept -> std::size_t
    {
        size_t hash = 0;
        oxygen::HashCombine(hash, s.base_mip_level);
        oxygen::HashCombine(hash, s.num_mip_levels);
        oxygen::HashCombine(hash, s.base_array_slice);
        oxygen::HashCombine(hash, s.num_array_slices);
        return hash;
    }
};

//! Hash specialization for TextureBindingKey.
/*!
 Enables TextureBindingKey to be used as key in hash-based containers like
 std::unordered_map or std::unordered_set.

 Combines hashes of the TextureBindingKey format, its base TextureSubResourceSet
 (using its hash specialization), and the read-only DSV flag using XOR
 operations to produce a well-distributed hash value.
*/
template <>
struct std::hash<oxygen::graphics::TextureBindingKey> {
    auto operator()(oxygen::graphics::TextureBindingKey const& s) const noexcept -> std::size_t
    {
        return std::hash<oxygen::graphics::Format>()(s.format)
            ^ std::hash<oxygen::graphics::TextureSubResourceSet>()(s)
            ^ std::hash<bool>()(s.is_read_only_dsv);
    }
};
