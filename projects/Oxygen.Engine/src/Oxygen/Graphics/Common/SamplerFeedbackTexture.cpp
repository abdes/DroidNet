//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/SamplerFeedbackTexture.h>

namespace oxygen::graphics {

SamplerFeedbackTexture::SamplerFeedbackTexture(const Texture& target_texture, const SamplerFeedbackTextureDesc& desc)
    : target_texture_(target_texture)
{
    // If no dimensions provided, derive from the target texture
    auto target_desc = target_texture.GetDescriptor();

    desc_ = desc;
    if (desc_.width == 0) {
        desc_.width = target_desc.width;
    }
    if (desc_.height == 0) {
        desc_.height = target_desc.height;
    }
    if (desc_.dimension == TextureDimension::kUnknown) {
        desc_.dimension = target_desc.dimension;
    }

    // Implementation will be provided by backend-specific implementations
    // that create the appropriate native resource and UAV
}

SamplerFeedbackTexture::SamplerFeedbackTexture(
    NativeObject native_handle,
    const Texture& target_texture,
    const SamplerFeedbackTextureDesc& desc)
    : native_(native_handle)
    , desc_(desc)
    , target_texture_(target_texture)
{
    // Backend-specific implementations will need to create UAV
}

SamplerFeedbackTexture::~SamplerFeedbackTexture()
{
    // Backend-specific cleanup will be implemented in derived classes
}

NativeObject SamplerFeedbackTexture::GetUnorderedAccessView() const
{
    return uav_;
}

} // namespace oxygen::graphics
