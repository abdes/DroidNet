//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <utility>

#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/RenderController.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/api_export.h>

// ReSharper disable CppRedundantQualifier

namespace oxygen::graphics::d3d12 {

class Graphics;

namespace detail {
  class DescriptorHeap;
  class PerFrameResourceManager;
} // namespace detail

class RenderController final : public graphics::RenderController {
  using Base = graphics::RenderController;

public:
  OXYGEN_D3D12_API RenderController(
    const std::weak_ptr<oxygen::Graphics>& gfx_weak,
    std::weak_ptr<oxygen::graphics::Surface> surface,
    const frame::SlotCount frames_in_flight = frame::kFramesInFlight)
    : RenderController("D3D12 RenderController", gfx_weak, std::move(surface),
        frames_in_flight)
  {
  }

  //! Default constructor, sets the object name.
  OXYGEN_D3D12_API RenderController(std::string_view name,
    const std::weak_ptr<oxygen::Graphics>& gfx_weak,
    std::weak_ptr<oxygen::graphics::Surface> surface_weak,
    frame::SlotCount frames_in_flight = frame::kFramesInFlight);

  OXYGEN_D3D12_API ~RenderController() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderController)
  OXYGEN_MAKE_NON_MOVABLE(RenderController)

  // Hides base GetGraphics(), returns d3d12::Graphics&
  // ReSharper disable once CppHidingFunction
  [[nodiscard]] auto GetGraphics() -> d3d12::Graphics&;

  // Hides base GetGraphics(), returns d3d12::Graphics&
  // ReSharper disable once CppHidingFunction
  [[nodiscard]] auto GetGraphics() const -> const d3d12::Graphics&;

  [[nodiscard]] auto GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc,
    size_t hash) -> detail::PipelineStateCache::Entry;
  [[nodiscard]] auto GetOrCreateComputePipeline(
    ComputePipelineDesc desc, size_t hash) -> detail::PipelineStateCache::Entry;

protected:
  [[nodiscard]] OXYGEN_D3D12_API auto CreateCommandRecorder(
    std::shared_ptr<graphics::CommandList> command_list,
    observer_ptr<graphics::CommandQueue> target_queue)
    -> std::unique_ptr<graphics::CommandRecorder> override;
};

} // namespace oxygen::graphics::d3d12
