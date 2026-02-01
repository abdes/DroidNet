//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <vector>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>

namespace oxygen::graphics {
class Framebuffer;
class Surface;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {

//! Compositing task type for the kCompositing phase.
enum class CompositingTaskType {
  kCopy,
  kBlend,
  kBlendTexture,
  kTonemap,
  kTaa,
};

//! Copy a view output into the target framebuffer.
struct CopyTask {
  ViewId source_view {};
  ViewPort viewport {};
};

//! Alpha-blended composition of a view output into the target framebuffer.
struct BlendTask {
  ViewId source_view {};
  ViewPort viewport {};
  float alpha { 1.0F };
};

//! Alpha-blended composition of a texture into the target framebuffer.
struct TextureBlendTask {
  std::shared_ptr<graphics::Texture> source_texture {};
  ViewPort viewport {};
  float alpha { 1.0F };
};

//! Placeholder for future tonemapping tasks.
struct TonemapTask {
  float exposure { 1.0F };
};

//! Placeholder for future temporal AA tasks.
struct TaaTask {
  float jitter_scale { 1.0F };
};

//! A compositing task with a stable enum tag and payload slots.
struct CompositingTask {
  CompositingTaskType type { CompositingTaskType::kCopy };
  CopyTask copy {};
  BlendTask blend {};
  TextureBlendTask texture_blend {};
  TonemapTask tonemap {};
  TaaTask taa {};

  [[nodiscard]] static auto MakeCopy(ViewId view_id, ViewPort viewport)
    -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kCopy,
      .copy = { .source_view = view_id, .viewport = viewport },
    };
  }

  [[nodiscard]] static auto MakeBlend(
    ViewId view_id, ViewPort viewport, float alpha) -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kBlend,
      .blend = {
        .source_view = view_id,
        .viewport = viewport,
        .alpha = alpha,
      },
    };
  }

  [[nodiscard]] static auto MakeTextureBlend(
    std::shared_ptr<graphics::Texture> texture, ViewPort viewport, float alpha)
    -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kBlendTexture,
      .texture_blend = {
        .source_texture = std::move(texture),
        .viewport = viewport,
        .alpha = alpha,
      },
    };
  }
};

using CompositingTaskList = std::vector<CompositingTask>;

//! Composition submission for the kCompositing phase.
struct CompositionSubmission {
  std::shared_ptr<graphics::Framebuffer> target_framebuffer {};
  std::shared_ptr<graphics::Surface> target_surface {};
  CompositingTaskList tasks {};
};

} // namespace oxygen::engine
