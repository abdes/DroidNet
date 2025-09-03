//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Oxygen/Graphics/Direct3D12/Buffer.h>
#include <Oxygen/Graphics/Direct3D12/CommandRecorder.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/WindowSurface.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

using oxygen::graphics::TextureDesc;
using oxygen::graphics::d3d12::RenderController;

RenderController::RenderController(const std::string_view name,
  const std::weak_ptr<oxygen::Graphics>& gfx_weak,
  std::weak_ptr<Surface> surface_weak, const frame::SlotCount frames_in_flight)
  : graphics::RenderController(
      name, gfx_weak, std::move(surface_weak), frames_in_flight)
{
  DCHECK_F(!gfx_weak.expired(), "Graphics object is expired");

  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  auto* gfx = static_cast<Graphics*>(gfx_weak.lock().get());
  AddComponent<detail::PipelineStateCache>(gfx);
}

auto RenderController::GetGraphics() -> Graphics&
{
  // Hides base GetGraphics(), returns d3d12::Graphics&
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  return static_cast<Graphics&>(Base::GetGraphics());
}

auto RenderController::GetGraphics() const -> const Graphics&
{
  // Hides base GetGraphics(), returns d3d12::Graphics&
  // NOLINTNEXTLINE(*-pro-type-static-cast-downcast)
  return static_cast<const Graphics&>(Base::GetGraphics());
}

auto RenderController::CreateCommandRecorder(
  std::shared_ptr<graphics::CommandList> command_list,
  observer_ptr<graphics::CommandQueue> target_queue)
  -> std::unique_ptr<graphics::CommandRecorder>
{
  // TODO: ASYNC_ENGINE_MIGRATION - RenderController is deprecated and will be
  // deleted Use Graphics directly instead of going through RenderController
  throw std::runtime_error("RenderController::CreateCommandRecorder is "
                           "deprecated. Use Graphics directly.");
}

auto RenderController::GetOrCreateGraphicsPipeline(GraphicsPipelineDesc desc,
  const size_t hash) -> detail::PipelineStateCache::Entry
{
  auto& cache = GetComponent<detail::PipelineStateCache>();
  return cache.GetOrCreatePipeline<GraphicsPipelineDesc>(std::move(desc), hash);
}

auto RenderController::GetOrCreateComputePipeline(ComputePipelineDesc desc,
  const size_t hash) -> detail::PipelineStateCache::Entry
{
  auto& cache = GetComponent<detail::PipelineStateCache>();
  return cache.GetOrCreatePipeline<ComputePipelineDesc>(std::move(desc), hash);
}
