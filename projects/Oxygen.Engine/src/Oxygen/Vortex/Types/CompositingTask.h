//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/CompositionView.h>

namespace oxygen::graphics {
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

//! Compositing task type for the kCompositing phase.
enum class CompositingTaskType {
  kCopy,
  kBlend,
  kBlendTexture,
  kTaa,
};

//! Copy a view output into the target framebuffer.
struct CopyTask {
  ViewId source_view_id {};
  ViewPort viewport {};
};

//! Alpha-blended composition of a view output into the target framebuffer.
struct BlendTask {
  ViewId source_view_id {};
  ViewPort viewport {};
  float alpha { 1.0F };
};

//! Alpha-blended composition of a texture into the target framebuffer.
struct TextureBlendTask {
  std::shared_ptr<graphics::Texture> source_texture {};
  ViewPort viewport {};
  float alpha { 1.0F };
};

//! Placeholder for future temporal AA tasks.
struct TaaTask {
  float jitter_scale { 1.0F };
};

//! A compositing task with a stable enum tag and payload slots.
struct CompositingTask {
  CompositingTaskType type { CompositingTaskType::kCopy };
  std::string debug_name {};
  CopyTask copy {};
  BlendTask blend {};
  TextureBlendTask texture_blend {};
  TaaTask taa {};

  [[nodiscard]] static auto MakeCopy(ViewId view_id, ViewPort viewport,
    std::string debug_name = {}) -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kCopy,
      .debug_name = std::move(debug_name),
      .copy = { .source_view_id = view_id, .viewport = viewport },
    };
  }

  [[nodiscard]] static auto MakeBlend(
    ViewId view_id, ViewPort viewport, float alpha, std::string debug_name = {})
    -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kBlend,
      .debug_name = std::move(debug_name),
      .blend = {
        .source_view_id = view_id,
        .viewport = viewport,
        .alpha = alpha,
      },
    };
  }

  [[nodiscard]] static auto MakeTextureBlend(
    std::shared_ptr<graphics::Texture> texture, ViewPort viewport, float alpha,
    std::string debug_name = {}) -> CompositingTask
  {
    return CompositingTask {
      .type = CompositingTaskType::kBlendTexture,
      .debug_name = std::move(debug_name),
      .texture_blend = {
        .source_texture = std::move(texture),
        .viewport = viewport,
        .alpha = alpha,
      },
    };
  }
};

using CompositingTaskList = std::vector<CompositingTask>;
using SurfaceOverlayBatchList = std::vector<CompositionView::OverlayBatch>;

//! Composition submission for the kCompositing phase.
struct CompositionSubmission {
  CompositionView::SurfaceRouteId surface_id {
    CompositionView::kDefaultSurfaceRoute
  };
  std::string debug_name {};
  std::shared_ptr<graphics::Framebuffer> composite_target {};
  CompositingTaskList tasks {};
  SurfaceOverlayBatchList surface_overlays {};
};

} // namespace oxygen::vortex
