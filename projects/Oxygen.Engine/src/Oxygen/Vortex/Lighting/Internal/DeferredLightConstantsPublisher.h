//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Vortex/Lighting/Types/DeferredLightConstants.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/InlineTransfersCoordinator.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>

namespace oxygen::vortex::lighting::internal {

//! Publishes immutable deferred draw CBVs in the renderer's fenced upload
//! arena.
class DeferredLightConstantsPublisher {
public:
  DeferredLightConstantsPublisher(std::weak_ptr<Graphics> graphics,
    upload::StagingProvider& staging,
    observer_ptr<upload::InlineTransfersCoordinator> transfers);
  ~DeferredLightConstantsPublisher();

  OXYGEN_MAKE_NON_COPYABLE(DeferredLightConstantsPublisher)
  OXYGEN_MAKE_NON_MOVABLE(DeferredLightConstantsPublisher)

  auto OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot) -> void;
  //! Indices survive additional Publish calls in this frame, until
  //! OnFrameStart or destruction. Consume this borrowed view while recording;
  //! it does not extend descriptor or GPU-resource lifetime.
  [[nodiscard]] auto Publish(std::span<const DeferredLightConstants> records)
    -> std::expected<std::span<const ShaderVisibleIndex>, upload::UploadError>;

private:
  struct Batch {
    upload::StagingProvider::Allocation allocation;
    std::vector<graphics::NativeView> views;
    std::vector<ShaderVisibleIndex> indices;
    std::uint64_t aligned_offset {};
  };
  struct Slot {
    std::optional<frame::SequenceNumber> sequence;
    std::vector<Batch> batches;
    std::size_t used_batches {};
  };
  auto ReleaseBatch(Batch& batch) noexcept -> void;
  auto ResetSlot(Slot& slot) noexcept -> void;

  std::weak_ptr<Graphics> graphics_;
  upload::StagingProvider& staging_;
  observer_ptr<upload::InlineTransfersCoordinator> transfers_;
  frame::Slot current_slot_ { frame::kInvalidSlot };
  std::array<Slot, frame::kFramesInFlight.get()> slots_;
};

} // namespace oxygen::vortex::lighting::internal
