//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

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

    bool is_shader_resource = true; // Note: is_shader_resource is initialized to 'true' for backward compatibility
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

class Texture : public Composition, public Named {
public:
    Texture(std::string_view name = "Texture")
    {
        AddComponent<ObjectMetaData>(name);
    }

    ~Texture() override = default;

    OXYGEN_MAKE_NON_COPYABLE(Texture);
    OXYGEN_DEFAULT_MOVABLE(Texture);

    virtual auto GetNativeResource() const -> NativeObject = 0;

    [[nodiscard]] auto GetName() const noexcept -> std::string_view override
    {
        return GetComponent<ObjectMetaData>().GetName();
    }

    void SetName(std::string_view name) noexcept override
    {
        GetComponent<ObjectMetaData>().SetName(name);
    }
};

} // namespace oxygen::graphics
