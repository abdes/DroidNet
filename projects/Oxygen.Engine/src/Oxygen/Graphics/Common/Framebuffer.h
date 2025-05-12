//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/StaticVector.h>
#include <Oxygen/Graphics/Common/Constants.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

struct FramebufferAttachment {
    std::shared_ptr<Texture> texture = nullptr;
    TextureSubResourceSet sub_resources = TextureSubResourceSet(0, 1, 0, 1);
    Format format = Format::kUnknown;
    bool is_read_only { false };

    [[nodiscard]] auto IsValid() const { return texture != nullptr; }
};

struct FramebufferDesc {
    StaticVector<FramebufferAttachment, kMaxRenderTargets> color_attachments;
    FramebufferAttachment depth_attachment;
    FramebufferAttachment shading_rate_attachment;

    auto AddColorAttachment(const FramebufferAttachment& a)
    {
        color_attachments.push_back(a);
        return *this;
    }

    auto AddColorAttachment(std::shared_ptr<Texture> texture)
    {
        color_attachments.push_back(FramebufferAttachment {
            .texture = std::move(texture) });
        return *this;
    }

    auto AddColorAttachment(std::shared_ptr<Texture> texture, const TextureSubResourceSet sub_resources)
    {
        color_attachments.push_back(FramebufferAttachment {
            .texture = std::move(texture),
            .sub_resources = sub_resources });
        return *this;
    }

    auto SetDepthAttachment(const FramebufferAttachment& d)
    {
        depth_attachment = d;
        return *this;
    }

    auto SetDepthAttachment(std::shared_ptr<Texture> texture)
    {
        depth_attachment = FramebufferAttachment {
            .texture = std::move(texture)
        };
        return *this;
    }

    auto SetDepthAttachment(std::shared_ptr<Texture> texture, const TextureSubResourceSet sub_resources)
    {
        depth_attachment = FramebufferAttachment {
            .texture = std::move(texture),
            .sub_resources = sub_resources
        };
        return *this;
    }

    auto SetShadingRateAttachment(const FramebufferAttachment& d)
    {
        shading_rate_attachment = d;
        return *this;
    }

    auto SetShadingRateAttachment(std::shared_ptr<Texture> texture)
    {
        shading_rate_attachment = FramebufferAttachment {
            .texture = std::move(texture)
        };
        return *this;
    }

    auto SetShadingRateAttachment(
        std::shared_ptr<Texture> texture,
        const TextureSubResourceSet sub_resources)
    {
        shading_rate_attachment = FramebufferAttachment {
            .texture = std::move(texture),
            .sub_resources = sub_resources
        };
        return *this;
    }
};

// Describes the parameters of a framebuffer that can be used to determine if a
// given framebuffer is compatible with a certain graphics or meshlet pipeline
// object. All fields of FramebufferInfo must match between the framebuffer and
// the pipeline for them to be compatible.
class FramebufferInfo {
public:
    OXYGEN_GFX_API explicit FramebufferInfo(const FramebufferDesc& desc);

    auto operator==(const FramebufferInfo& other) const
    {
        return FormatsEqual(color_formats_, other.color_formats_)
            && depth_format_ == other.depth_format_
            && sample_count_ == other.sample_count_
            && sample_quality_ == other.sample_quality_;
    }
    auto operator!=(const FramebufferInfo& other) const { return !(*this == other); }

private:
    static auto FormatsEqual(
        const StaticVector<Format, kMaxRenderTargets>& a,
        const StaticVector<Format, kMaxRenderTargets>& b) -> bool
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }

    StaticVector<Format, kMaxRenderTargets> color_formats_;
    Format depth_format_ = Format::kUnknown;
    uint32_t sample_count_ = 1;
    uint32_t sample_quality_ = 0;
};

class Framebuffer {
public:
    Framebuffer() = default;
    virtual ~Framebuffer() = default;

    OXYGEN_MAKE_NON_COPYABLE(Framebuffer)
    OXYGEN_DEFAULT_MOVABLE(Framebuffer)

    [[nodiscard]] virtual auto GetDescriptor() const -> const FramebufferDesc& = 0;
    [[nodiscard]] virtual auto GetFramebufferInfo() const -> const FramebufferInfo& = 0;
};

} // namespace oxygen::graphics
