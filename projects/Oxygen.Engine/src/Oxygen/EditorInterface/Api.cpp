//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Direct3D12/Detail/CompositionSurface.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::scene::Scene;

namespace oxygen::engine::interop {

auto CreateScene(const char* name) -> bool
{
  // Scene name must be a non-null, null terminated string that is not be
  // empty
  CHECK_NOTNULL_F(name, "Scene name must not be null");
  auto scene_name = std::string_view(name);
  CHECK_F(!scene_name.empty(), "Scene name must not be empty");

  return true;
}

auto RemoveScene(const char* name) -> bool
{
  CHECK_NOTNULL_F(name, "Scene name must not be null");
  auto scene_name = std::string_view(name);

  return false;
}

auto CreateCompositionSurface(std::shared_ptr<EngineContext> ctx,
  void** swap_chain_out) -> std::shared_ptr<graphics::Surface>
{
  if (!ctx || ctx->gfx_weak.expired()) {
    return nullptr;
  }
  auto gfx = ctx->gfx_weak.lock();
  auto queue = gfx->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (!queue) {
    return nullptr;
  }

  auto surface = gfx->CreateSurfaceFromNative(nullptr, queue);
  if (surface && swap_chain_out) {
    // We know it's a CompositionSurface because we called CreateSurfaceFromNative
    // (which maps to that in D3D12 backend for now, or we assume D3D12 backend)
    // Ideally we should check the type, but for now we static_cast as we know the backend.
    auto composition_surface = std::static_pointer_cast<oxygen::graphics::d3d12::detail::CompositionSurface>(surface);
    *swap_chain_out = composition_surface->GetSwapChain();
  }
  return surface;
}

auto RequestCompositionSurfaceResize(
  const std::shared_ptr<graphics::Surface>& surface, const uint32_t width,
  const uint32_t height) -> void
{
  if (!surface) {
    return;
  }

  auto composition_surface
    = std::static_pointer_cast<graphics::d3d12::detail::CompositionSurface>(
      surface);
  if (!composition_surface) {
    LOG_F(2,
      "RequestCompositionSurfaceResize ignored: surface '{}' is not a"
      " CompositionSurface",
      surface->GetName());
    return;
  }

  composition_surface->RequestResize(width, height);
}

} // namespace oxygen::engine::interop
