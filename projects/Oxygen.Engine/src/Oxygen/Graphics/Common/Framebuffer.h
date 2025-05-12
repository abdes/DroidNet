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

//! Describes a single attachment (color, depth, or shading rate) for a
//! framebuffer.
/*!
 Contains a texture, subresource set, format, and read-only flag.
*/
struct FramebufferAttachment {
    std::shared_ptr<Texture> texture = nullptr;
    TextureSubResourceSet sub_resources = TextureSubResourceSet(0, 1, 0, 1);
    Format format = Format::kUnknown;
    bool is_read_only { false };

    [[nodiscard]] auto IsValid() const { return texture != nullptr; }
};

//! Describes the set of attachments for a framebuffer.
/*!
 Includes color, depth, and shading rate attachments and provides methods to add
 or set attachments.
*/
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

    auto AddColorAttachment(
        std::shared_ptr<Texture> texture,
        const TextureSubResourceSet sub_resources)
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

    auto SetDepthAttachment(
        std::shared_ptr<Texture> texture,
        const TextureSubResourceSet sub_resources)
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

//! Describes framebuffer parameters for pipeline compatibility checks.
/*
 Includes color formats, depth format, sample count, and sample quality.
 Primarily used to determine compatibility between a framebuffer and a graphics
 or meshlet pipeline; all fields of FramebufferInfo must match between the
 framebuffer and the pipeline for them to be compatible. This ensures that
 pipelines are only used with framebuffers that have matching configurations,
 which is critical for correct rendering and efficient resource management.
*/
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

//! Graphics backend agnostic framebuffer, which defines the set of attachments
//! used as rendering targets during a render pass.
/*
 A framebuffer encapsulates a set of attachments (color, depth, and optionally
 shading rate) that serve as the rendering targets for the GPU during a render
 pass. The construction of a framebuffer is performed by providing a
 FramebufferDesc, which specifies the textures and subresources to be used as
 attachments. Concrete implementations of this class are responsible for
 allocating and managing the underlying GPU resources and ensuring that all
 attachments are compatible in terms of size and format.

 In the engine, a framebuffer is used as the destination for all rendering
 commands within a render pass. It is bound at the start of rendering, and all
 draw calls output their results to its attachments. After rendering, the
 contents of the framebuffer can be presented to the screen, used as input for
 further rendering passes, or read back for post-processing. The Framebuffer
 interface provides methods to query its descriptor and compatibility
 information, enabling the engine to validate pipeline compatibility and manage
 resource lifetimes efficiently.

 When creating framebuffers for a surface with a swapchain, it's important to
 note that a key aspect of the engine's design is the two-stage initialization
 of the surface. Swapchain backbuffers and their associated render target
 views are not created immediately upon swapchain creation. Instead, these
 resources are allocated only when a renderer is attached to the surface. This
 deferred allocation is essential for linking resource management to the frame
 lifecycle, which is owned by the renderer instance. It ensures that all GPU
 resources are created, managed, and destroyed in sync with the renderer,
 allowing for correct synchronization, efficient resource reuse, and proper
 cleanup.
*/
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
