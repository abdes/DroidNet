//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Nexus/IndexReuse.h>
#include <Oxygen/Vortex/Environment/Types/IblProductMetadata.h>
#include <Oxygen/Vortex/Types/SkyLightRuntimeState.h>

namespace oxygen {
class Graphics;
}
namespace oxygen::graphics {
class GpuBufferReadback;
}
namespace oxygen::vortex {
class DiagnosticsService;
}

namespace oxygen::vortex::environment::internal {

struct IblGpuProducts;

//! Bounded, nonblocking metadata observations; never controls shading.
class IblGpuValidation {
public:
  struct Observation {
    nexus::VersionedIndex<std::uint32_t> slot {};
    std::uint32_t revision {};
    std::uint64_t order {};
    SkyLightGpuValidation status { SkyLightGpuValidation::kUnavailable };
    IblProductMetadata metadata {};
  };
  using Observations
    = std::array<std::optional<Observation>, frame::kFramesInFlight.get()>;

  explicit IblGpuValidation(std::shared_ptr<Graphics> graphics);
  ~IblGpuValidation();
  IblGpuValidation(const IblGpuValidation&) = delete;
  auto operator=(const IblGpuValidation&) -> IblGpuValidation& = delete;
  IblGpuValidation(IblGpuValidation&&) = delete;
  auto operator=(IblGpuValidation&&) -> IblGpuValidation& = delete;
  auto Poll() noexcept -> Observations;
  auto Request(const IblGpuProducts& products,
    DiagnosticsService& diagnostics) noexcept -> void;
  auto Inspect(const IblGpuProducts& products, bool enabled) const
    -> SkyLightRuntimeState;

private:
  struct Pending {
    std::shared_ptr<graphics::GpuBufferReadback> readback;
    std::optional<Observation> request;
  };
  auto Remember(const Observation& observation) noexcept -> void;
  // Readback objects contain a manager reference. The CPU owner keeps Graphics
  // alive until they are destroyed; submitted batches never retain this owner.
  std::shared_ptr<Graphics> graphics_;
  std::array<Pending, frame::kFramesInFlight.get()> pending_;
  std::optional<Observation> latest_;
  std::optional<Observation> last_submitted_;
  std::uint64_t next_order_ {};
  std::uint64_t last_failed_order_ {};
  std::uint32_t last_failed_revision_ {};
};

} // namespace oxygen::vortex::environment::internal
