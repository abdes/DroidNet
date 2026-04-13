//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

namespace oxygen::vortex::upload::internal {
auto InlineCoordinatorTagFactory::Get() noexcept -> InlineCoordinatorTag
{
  return InlineCoordinatorTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace oxygen::vortex::upload {

namespace {
  [[nodiscard]] auto MakeUploaderTag() noexcept -> UploaderTag
  {
    return internal::UploaderTagFactory::Get();
  }
} // namespace

InlineTransfersCoordinator::InlineTransfersCoordinator(
  observer_ptr<Graphics> gfx)
  : gfx_(gfx)
{
  DCHECK_NOTNULL_F(gfx_, "InlineTransfersCoordinator requires valid Graphics");
}

InlineTransfersCoordinator::~InlineTransfersCoordinator() = default;

auto InlineTransfersCoordinator::RegisterProvider(
  const std::shared_ptr<StagingProvider>& provider) -> void
{
  if (!provider) {
    return;
  }
  providers_.push_back(provider);
  DLOG_F(2, "InlineTransfersCoordinator registered provider {}",
    static_cast<const void*>(provider.get()));
}

auto InlineTransfersCoordinator::NotifyInlineWrite(SizeBytes size,
  [[maybe_unused]] std::string_view source_label) noexcept -> void
{
  pending_inline_bytes_.fetch_add(size.get(), std::memory_order_relaxed);
  has_pending_inline_writes_.store(true, std::memory_order_release);
  DLOG_F(3, "InlineTransfersCoordinator tracking {} bytes from {}", size.get(),
    source_label);
}

auto InlineTransfersCoordinator::OnFrameStart(
  vortex::RendererTag /*tag*/, frame::Slot slot) -> void
{
  // Always clear the flag and advance the fence/retire cycle.
  // Even if no writes occurred, we must notify providers to rotate partitions
  // and update retirement counters to avoid false-positive warnings.
  has_pending_inline_writes_.exchange(false, std::memory_order_acq_rel);
  RetireCompleted();

  static auto tag = internal::InlineCoordinatorTagFactory::Get();

  for (auto it = providers_.begin(); it != providers_.end();) {
    if (auto provider = it->lock()) {
      provider->OnFrameStart(tag, slot);
      ++it;
    } else {
      it = providers_.erase(it);
    }
  }
}

auto InlineTransfersCoordinator::RetireCompleted() -> void
{
  const auto fence_raw
    = synthetic_fence_counter_.fetch_add(1, std::memory_order_acq_rel) + 1;
  [[maybe_unused]] const auto retired_bytes
    = pending_inline_bytes_.exchange(0, std::memory_order_acq_rel);
  const auto fence = graphics::FenceValue { fence_raw };
  auto tag = MakeUploaderTag();

  size_t notified_providers = 0;
  for (auto it = providers_.begin(); it != providers_.end();) {
    if (auto provider = it->lock()) {
      provider->RetireCompleted(tag, fence);
      ++it;
      ++notified_providers;
    } else {
      it = providers_.erase(it);
    }
  }

  DLOG_F(2, "InlineTransfersCoordinator retired {} bytes fence={} providers={}",
    retired_bytes, fence.get(), notified_providers);
}

} // namespace oxygen::vortex::upload
