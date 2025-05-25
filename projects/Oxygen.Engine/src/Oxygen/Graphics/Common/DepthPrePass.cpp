//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DepthPrepass.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/Scissors.h>
#include <Oxygen/Graphics/Common/Types/ViewPort.h>

using oxygen::graphics::Color;
using oxygen::graphics::DepthPrePass;
using oxygen::graphics::Scissors;
using oxygen::graphics::ViewPort;
// No using for CommandRecorder or ResourceStates, will use fully qualified names.

DepthPrePass::DepthPrePass(std::string_view name, const Config& config)
    : RenderPass(name) // Call the base class constructor to set the name
    , config_(config)
{
    ValidateConfig();
}

void DepthPrePass::SetViewport(const ViewPort& viewport)
{
    if (!viewport.IsValid()) {
        throw std::invalid_argument(
            fmt::format("viewport {} is invalid",
                nostd::to_string(viewport)));
    }
    DCHECK_NOTNULL_F(config_.depth_texture, "expecting a non-null depth texture");

    const auto& tex_desc = config_.depth_texture->GetDescriptor();

    auto viewport_width = viewport.top_left_x + viewport.width;
    auto viewport_height = viewport.top_left_y + viewport.height;
    if (viewport_width > static_cast<float>(tex_desc.width)
        || viewport_height > static_cast<float>(tex_desc.height)) {
        throw std::out_of_range(
            fmt::format(
                "viewport dimensions ({}, {}) exceed depth_texture bounds: ({}, {})",
                viewport_width, viewport_height, tex_desc.width, tex_desc.height));
    }
    viewport_.emplace(viewport);
}

void DepthPrePass::SetScissors(const Scissors& scissors)
{
    if (!scissors.IsValid()) {
        throw std::invalid_argument(
            fmt::format("scissors {} are invalid.",
                nostd::to_string(scissors)));
    }
    DCHECK_NOTNULL_F(config_.depth_texture,
        "expecting depth texture to be valid when setting scissors");

    const auto& tex_desc = config_.depth_texture->GetDescriptor();

    // Assuming scissors coordinates are relative to the texture origin (0,0)
    if (scissors.left < 0 || scissors.top < 0) {
        throw std::out_of_range("scissors left and top must be non-negative.");
    }
    if (static_cast<uint32_t>(scissors.right) > tex_desc.width
        || static_cast<uint32_t>(scissors.bottom) > tex_desc.height) {
        throw std::out_of_range(
            fmt::format("scissors dimensions ({}, {}) exceed depth_texture bounds ({}, {})",
                scissors.right, scissors.bottom, tex_desc.width, tex_desc.height));
    }

    scissors_.emplace(scissors);
}

void DepthPrePass::SetClearColor(const Color& color)
{
    clear_color_.emplace(color);
}

void DepthPrePass::SetEnabled(bool enabled)
{
    enabled_ = enabled;
}

auto DepthPrePass::IsEnabled() const -> bool
{
    return enabled_;
}

oxygen::co::Co<> DepthPrePass::PrepareResources(oxygen::graphics::CommandRecorder& recorder)
{
    DCHECK_NOTNULL_F(config_.depth_texture,
        "Depth texture must be valid for PrepareResources");

    if (config_.depth_texture) {
        recorder.RequireResourceState(*config_.depth_texture, ResourceStates::kDepthWrite);
        // Flush barriers to ensure the depth_texture is in kDepthWrite state
        // before derived classes might perform operations like clears.
        recorder.FlushBarriers();
    }
    co_return;
}

void DepthPrePass::ValidateConfig()
{
    if (!config_.depth_texture) {
        throw std::runtime_error(
            "invalid DepthPrePassConfig: depth_texture cannot be null.");
    }
    if (config_.framebuffer) {
        const auto& fb_desc = config_.framebuffer->GetDescriptor();
        if (fb_desc.depth_attachment.texture
            && fb_desc.depth_attachment.texture != config_.depth_texture) {
            throw std::runtime_error(
                "invalid DepthPrePassConfig: framebuffer's depth attachment "
                "texture must match depth_texture when both are provided and "
                "framebuffer has a depth attachment.");
        }
    }
    // Backend-specific derived classes can override this to add more checks.
}
