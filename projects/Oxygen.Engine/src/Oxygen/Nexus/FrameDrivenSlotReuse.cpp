//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Nexus/FrameDrivenSlotReuse.h>

using oxygen::nexus::DomainKey;
using oxygen::nexus::FrameDrivenSlotReuse;

FrameDrivenSlotReuse::FrameDrivenSlotReuse(AllocateFn allocate, FreeFn free,
  graphics::detail::DeferredReclaimer& per_frame)
  : allocate_(std::move(allocate))
  , free_(std::move(free))
  , impl_(per_frame, [this](bindless::HeapIndex idx, DomainKey key) {
    if (free_) {
      free_(key, idx);
    }
  })
{
}

auto FrameDrivenSlotReuse::Allocate(DomainKey domain) -> VersionedBindlessHandle
{
  const auto idx = allocate_(domain);
  const auto versioned_idx = impl_.ActivateSlot(idx);
  return VersionedBindlessHandle { versioned_idx.index,
    versioned_idx.generation };
}

auto FrameDrivenSlotReuse::Release(DomainKey domain, VersionedBindlessHandle h)
  -> void
{
  if (!h.IsValid()) {
    return;
  }
  impl_.Release(
    {
      .index = h.ToBindlessHandle(),
      .generation = h.GenerationValue(),
    },
    domain);
}

auto FrameDrivenSlotReuse::IsHandleCurrent(
  VersionedBindlessHandle h) const noexcept -> bool
{
  if (!h.IsValid()) {
    return false;
  }
  return impl_.IsHandleCurrent({
    .index = h.ToBindlessHandle(),
    .generation = h.GenerationValue(),
  });
}

auto FrameDrivenSlotReuse::OnBeginFrame(frame::Slot fi) -> void
{
  impl_.OnBeginFrame(fi);
}
