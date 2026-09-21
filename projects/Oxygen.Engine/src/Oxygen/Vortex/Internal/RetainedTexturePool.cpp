//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>

namespace oxygen::vortex::internal {
namespace {

  auto SameAllocation(
    const graphics::TextureDesc& a, const graphics::TextureDesc& b) -> bool
  {
    // Debug names do not affect allocation compatibility. Compare clear-value
    // bits explicitly: Color equality intentionally uses a numeric tolerance.
    return a.width == b.width && a.height == b.height && a.depth == b.depth
      && a.array_size == b.array_size && a.mip_levels == b.mip_levels
      && a.sample_count == b.sample_count
      && a.sample_quality == b.sample_quality && a.format == b.format
      && a.texture_type == b.texture_type
      && a.is_shader_resource == b.is_shader_resource
      && a.is_render_target == b.is_render_target && a.is_uav == b.is_uav
      && a.is_typeless == b.is_typeless
      && a.is_shading_rate_surface == b.is_shading_rate_surface
      && a.use_clear_value == b.use_clear_value
      && std::bit_cast<std::uint32_t>(a.clear_value.r)
      == std::bit_cast<std::uint32_t>(b.clear_value.r)
      && std::bit_cast<std::uint32_t>(a.clear_value.g)
      == std::bit_cast<std::uint32_t>(b.clear_value.g)
      && std::bit_cast<std::uint32_t>(a.clear_value.b)
      == std::bit_cast<std::uint32_t>(b.clear_value.b)
      && std::bit_cast<std::uint32_t>(a.clear_value.a)
      == std::bit_cast<std::uint32_t>(b.clear_value.a)
      && a.initial_state == b.initial_state && a.cpu_access == b.cpu_access;
  }

  struct IdleTexture {
    std::shared_ptr<graphics::Texture> texture;
    graphics::ResourceStates state { graphics::ResourceStates::kUnknown };
  };

  struct ViewEntry {
    graphics::TextureDesc desc;
    frame::SequenceNumber last_seen { 0U };
    IdleTexture idle;
  };

} // namespace

struct RetainedTexturePool::Impl {
  std::shared_ptr<Graphics> graphics;
  frame::SequenceNumber sequence { 0U };
  std::unordered_map<ViewId, std::shared_ptr<ViewEntry>> views;
};

RetainedTexturePool::RetainedTexturePool(std::shared_ptr<Graphics> graphics)
  : impl_(std::make_unique<Impl>())
{
  impl_->graphics = std::move(graphics);
}

RetainedTexturePool::~RetainedTexturePool() = default;

auto RetainedTexturePool::Acquire(const ViewId view_id,
  const graphics::TextureDesc& desc, const bool recyclable)
  -> std::shared_ptr<graphics::Texture>
{
  auto& graphics = impl_->graphics;
  if (!graphics) {
    return {};
  }

  std::shared_ptr<ViewEntry> entry;
  if (recyclable && view_id != kInvalidViewId) {
    auto& current = impl_->views[view_id];
    if (!current || !SameAllocation(current->desc, desc)) {
      // Replacing the token invalidates returns from every older descriptor.
      current = std::make_shared<ViewEntry>();
      current->desc = desc;
    }
    current->last_seen = impl_->sequence;
    entry = current;
  } else {
    RemoveView(view_id);
  }

  auto idle = IdleTexture {};
  if (entry) {
    idle = std::exchange(entry->idle, {});
  }
  const auto queue = graphics->GetCommandQueue(graphics::QueueRole::kGraphics);
  if (idle.texture && (!queue || idle.texture.use_count() != 1)) {
    idle = {};
  }
  auto texture = std::move(idle.texture);
  if (!texture) {
    texture = graphics->CreateTexture(desc);
  }
  if (!texture) {
    return {};
  }
  texture->SetName(desc.debug_name);

  // Only the underlying resource enters the registry. Registering this wrapper
  // would create a cycle through the Graphics owner captured by its deleter.
  auto retained = std::shared_ptr<graphics::Texture>(texture.get(),
    [graphics, texture, eligibility = std::weak_ptr<ViewEntry>(entry)](
      graphics::Texture*) mutable -> void {
      // Weak wrapper observers may retain the deleter's control block after
      // this call. Keep Graphics alive only through scheduling retirement.
      auto retained_graphics = std::move(graphics);
      auto* owner = retained_graphics.get();
      owner->GetDeferredReclaimer().RegisterDeferredAction(
        [owner, texture = std::move(texture), eligibility] mutable -> void {
          const auto state
            = owner->TryGetKnownResourceState(texture->GetNativeResource());
          auto& registry = owner->GetResourceRegistry();
          if (registry.Contains(*texture)) {
            registry.UnRegisterResource(*texture);
          }
          // This action belongs to Graphics; capturing its shared owner here
          // would prevent Graphics destruction from draining the reclaimer.
          if (texture.use_count() != 1 || !state.has_value()) {
            return;
          }
          if (const auto destination = eligibility.lock();
            destination && !destination->idle.texture) {
            destination->idle = { std::move(texture), *state };
          }
        });
    });
  graphics->GetResourceRegistry().Register(texture);
  if (idle.state != graphics::ResourceStates::kUnknown) {
    const std::array states {
      graphics::CommandQueue::KnownResourceState {
        .resource = texture->GetNativeResource(),
        .state = idle.state,
      },
    };
    queue->AdoptKnownResourceStates(states);
  }
  return retained;
}

auto RetainedTexturePool::OnFrameStart(const frame::SequenceNumber sequence)
  -> void
{
  impl_->sequence = sequence;
  std::erase_if(impl_->views, [sequence](const auto& item) -> auto {
    const auto last_seen = item.second->last_seen.get();
    return last_seen > sequence.get() || sequence.get() - last_seen > 1U;
  });
}

auto RetainedTexturePool::RemoveView(const ViewId view_id) -> void
{
  impl_->views.erase(view_id);
}

auto RetainedTexturePool::Clear() -> void { impl_->views.clear(); }

} // namespace oxygen::vortex::internal
