//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class ResourceRegistry;
class Texture;
}

namespace oxygen::vortex::environment {
namespace internal {
  struct IblGpuProducts;
  struct IblCaptureLeaseState;
}

enum class IblCaptureError : std::uint8_t {
  kUnavailable,
  kBusy,
  kClosed,
  kAllocationFailed,
  kRecordingFailed,
};

//! Immutable generation retained within its scene's capture allowance.
//! Copies share admission. Attach before GPU reads; the recorder retains the
//! generation through discard/completion. Restore shader-resource state after
//! copies.
class IblCaptureLease final {
public:
  IblCaptureLease() = default;
  [[nodiscard]] explicit operator bool() const noexcept
  {
    return state_ != nullptr;
  }
  [[nodiscard]] OXGN_VRTX_API auto Attach(graphics::CommandRecorder& recorder,
    graphics::ResourceRegistry& registry) const
    -> Result<void, IblCaptureError>;
  [[nodiscard]] OXGN_VRTX_API auto Revision() const noexcept -> std::uint32_t;
  [[nodiscard]] OXGN_VRTX_API auto ProcessedCube() const
    -> std::shared_ptr<const graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto SpecularCube() const
    -> std::shared_ptr<const graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto ProcessedHalfCube() const
    -> std::shared_ptr<const graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto SpecularHalfCube() const
    -> std::shared_ptr<const graphics::Texture>;
  [[nodiscard]] OXGN_VRTX_API auto DiffuseSh() const
    -> std::shared_ptr<const graphics::Buffer>;
  [[nodiscard]] OXGN_VRTX_API auto Metadata() const
    -> std::shared_ptr<const graphics::Buffer>;

private:
  friend struct internal::IblGpuProducts;
  explicit IblCaptureLease(
    std::shared_ptr<internal::IblCaptureLeaseState> state) noexcept;
  std::shared_ptr<internal::IblCaptureLeaseState> state_;
};
} // namespace oxygen::vortex::environment
