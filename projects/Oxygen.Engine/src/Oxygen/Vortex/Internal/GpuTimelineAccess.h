//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsService.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen::vortex::internal {

class GpuTimelineSink;

//! The renderer's GPU timeline as production scheduling (IBL budgeting)
//! uses it: no capability gate, and recorders it attaches always collect
//! while a lease is held.
class GpuTimelineAccess {
public:
  explicit GpuTimelineAccess(DiagnosticsService& service)
    : service_(&service)
  {
  }

  //! Keeps collection on; never capability-gated.
  [[nodiscard]] OXGN_VRTX_API auto AcquireLease() const -> GpuTimelineLease;
  OXGN_VRTX_API auto AddSink(std::shared_ptr<GpuTimelineSink> sink) const
    -> void;
  //! Attaches the collector to a graphics-queue recorder.
  OXGN_VRTX_API auto AttachCollector(graphics::CommandRecorder& recorder) const
    -> void;
  //! A failed recording or submission: the frame's timing is discarded.
  OXGN_VRTX_API auto InvalidateCurrentFrame() const noexcept -> void;

private:
  observer_ptr<DiagnosticsService> service_;
};

} // namespace oxygen::vortex::internal
