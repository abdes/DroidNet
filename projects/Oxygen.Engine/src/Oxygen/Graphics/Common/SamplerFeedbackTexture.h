//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file SamplerFeedbackTexture.h
 * @brief Backend-agnostic sampler feedback texture implementation for virtual texturing.
 *
 * This file provides a unified interface for sampler feedback textures used in
 * virtual texturing systems across different graphics backends.
 */

#pragma once

#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Format.h>

namespace oxygen::graphics {

/**
 * @brief Descriptor for sampler feedback texture
 */
struct SamplerFeedbackTextureDesc {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 1;
    TextureDimension dimension = TextureDimension::kTexture2D;

    bool operator==(const SamplerFeedbackTextureDesc&) const = default;
};

/**
 * @brief Backend-agnostic sampler feedback texture for virtual texturing
 *
 * Sampler feedback textures are used in virtual texturing systems to track
 * texture usage and prioritize texture streaming. This class provides a
 * consistent interface across different graphics APIs.
 */
class SamplerFeedbackTexture {
public:
    /**
     * @brief Create a sampler feedback texture from a target texture
     * @param target_texture The texture this feedback is tracking
     * @param desc Additional descriptor parameters (optional)
     */
    explicit SamplerFeedbackTexture(const Texture& target_texture, const SamplerFeedbackTextureDesc& desc = {});

    /**
     * @brief Create a sampler feedback texture from a native handle
     * @param native_handle Backend-specific native handle
     * @param target_texture The texture this feedback is tracking
     * @param desc The sampler feedback texture description
     */
    SamplerFeedbackTexture(
        NativeObject native_handle,
        const Texture& target_texture,
        const SamplerFeedbackTextureDesc& desc);

    /**
     * @brief Destructor
     */
    ~SamplerFeedbackTexture();

    /**
     * @brief Get the feedback texture description
     * @return The sampler feedback texture description
     */
    [[nodiscard]] const SamplerFeedbackTextureDesc& GetDesc() const noexcept { return desc_; }

    /**
     * @brief Get the native resource handle
     * @return The native resource handle
     */
    [[nodiscard]] const NativeObject& GetNativeResource() const noexcept { return native_; }

    /**
     * @brief Get the unordered access view for this feedback texture
     * @return The native UAV object
     */
    [[nodiscard]] NativeObject GetUnorderedAccessView() const;

    /**
     * @brief Get the target texture this feedback is tracking
     * @return Reference to the target texture
     */
    [[nodiscard]] const Texture& GetTargetTexture() const noexcept { return target_texture_; }

    /**
     * @brief Check if the sampler feedback texture is valid
     * @return True if valid, false otherwise
     */
    [[nodiscard]] bool IsValid() const noexcept { return native_.IsValid(); }

private:
    NativeObject native_ {};
    NativeObject uav_ {};
    SamplerFeedbackTextureDesc desc_;
    const Texture& target_texture_;
};

} // namespace oxygen::graphics
