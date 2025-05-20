//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file Sampler.h
 * @brief Backend-agnostic sampler implementation for 3D rendering.
 *
 * This file provides a unified interface for texture sampling operations across different
 * graphics backends (DirectX, Vulkan, etc).
 */

#pragma once

#include <cstdint>

#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/Format.h>

namespace oxygen::graphics {

/**
 * @brief Sampler descriptor defining all sampling parameters
 */
struct SamplerDesc {
    enum class Filter : uint8_t {
        kPoint,
        kBilinear,
        kTrilinear,
        kAniso
    };

    enum class AddressMode : uint8_t {
        kWrap,
        kMirror,
        kClamp,
        kBorder
    };

    enum class CompareFunc : uint8_t {
        kDisabled,
        kLess,
        kGreater,
        kEqual
    };

    Filter filter = Filter::kBilinear;
    AddressMode address_u = AddressMode::kWrap;
    AddressMode address_v = AddressMode::kWrap;
    AddressMode address_w = AddressMode::kWrap;
    float mip_lod_bias = 0.0f;
    uint32_t max_anisotropy = 16;
    CompareFunc compare_func = CompareFunc::kDisabled;
    float border_color[4] = { 0, 0, 0, 0 };
    float min_lod = 0.0f;
    float max_lod = 1000.0f;

    bool operator==(const SamplerDesc&) const = default;
    [[nodiscard]] size_t GetHash() const noexcept;
};

/**
 * @brief Backend-agnostic sampler class for texture sampling operations
 *
 * This class wraps the backend-specific sampler implementation and provides
 * a consistent interface across different graphics APIs.
 */
class Sampler {
public:
    /**
     * @brief Create a sampler with the specified parameters
     * @param desc The sampler description
     */
    explicit Sampler(const SamplerDesc& desc);

    /**
     * @brief Create a sampler from native handle
     * @param native_handle Backend-specific native handle
     * @param desc The sampler description
     */
    Sampler(NativeObject native_handle, const SamplerDesc& desc);

    /**
     * @brief Destructor
     */
    ~Sampler();

    /**
     * @brief Get the sampler description
     * @return The sampler description
     */
    [[nodiscard]] const SamplerDesc& GetDesc() const noexcept { return desc_; }

    /**
     * @brief Get the native resource handle
     * @return The native resource handle
     */
    [[nodiscard]] const NativeObject& GetNativeResource() const noexcept { return native_; }

    /**
     * @brief Check if the sampler is valid
     * @return True if valid, false otherwise
     */
    [[nodiscard]] bool IsValid() const noexcept { return native_.IsValid(); }

private:
    NativeObject native_ {};
    SamplerDesc desc_;
};

// Common predefined samplers
namespace Samplers {
    // Creates a point/nearest sampler with clamp address mode
    [[nodiscard]] Sampler PointClamp();

    // Creates a bilinear sampler with clamp address mode
    [[nodiscard]] Sampler BilinearClamp();

    // Creates a trilinear sampler with wrap address mode
    [[nodiscard]] Sampler TrilinearWrap();

    // Creates an anisotropic sampler with wrap address mode
    [[nodiscard]] Sampler AnisotropicWrap(uint32_t max_anisotropy = 16);

    // Creates a shadow map comparison sampler
    [[nodiscard]] Sampler ShadowComparison();
}

} // namespace oxygen::graphics
