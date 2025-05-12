//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/FrameBuffer.h>

using oxygen::graphics::FramebufferInfo;

FramebufferInfo::FramebufferInfo(const FramebufferDesc& desc)
{
    for (const auto& attachment : desc.color_attachments) {
        color_formats_.push_back(
            attachment.format == Format::kUnknown && attachment.texture
                ? attachment.texture->GetDescriptor().format
                : attachment.format);
    }

    if (desc.depth_attachment.IsValid()) {
        const TextureDesc& texture_desc = desc.depth_attachment.texture->GetDescriptor();
        depth_format_ = texture_desc.format;
        sample_count_ = texture_desc.sample_count;
        sample_quality_ = texture_desc.sample_quality;
    } else if (!desc.color_attachments.empty() && desc.color_attachments[0].IsValid()) {
        const TextureDesc& texture_desc = desc.color_attachments[0].texture->GetDescriptor();
        sample_count_ = texture_desc.sample_count;
        sample_quality_ = texture_desc.sample_quality;
    }
}
