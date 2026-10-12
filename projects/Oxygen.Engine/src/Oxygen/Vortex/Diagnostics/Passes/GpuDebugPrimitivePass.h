//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Diagnostics/DiagnosticsTypes.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;

//! Bounded GPU debug lines and invalid-access counters for camera views.
/*!
 While `DiagnosticsFeature::kGpuDebugPrimitives` is enabled, each view owns a
 line buffer of the configured capacity and a counter buffer whose first words
 are the indirect arguments of the line draw. `Prepare` publishes the view's
 `DebugFrameBindings` record and resets the counters before any producer runs;
 shaders then append lines and count invalid bindless and material loads.
 `Record` draws the stored lines over the view output, always on top, and
 reads the counters back. A later frame collects them as stats, and reports
 overflow and invalid accesses as diagnostics issues.

 While the feature is disabled, views own no buffers and publish no record.
*/
class GpuDebugPrimitivePass {
public:
  OXGN_VRTX_API explicit GpuDebugPrimitivePass(Renderer& renderer);
  OXGN_VRTX_API ~GpuDebugPrimitivePass();

  OXYGEN_MAKE_NON_COPYABLE(GpuDebugPrimitivePass)
  OXYGEN_MAKE_NON_MOVABLE(GpuDebugPrimitivePass)

  //! Publishes the current view's record and records its counter reset.
  /*!
   Returns the record for `ViewFrameBindings::debug_frame_slot`; invalid, and
   the view's buffers released, while the feature is disabled. A view whose
   buffers or record cannot be created renders without them.
  */
  OXGN_VRTX_API auto Prepare(RenderContext& ctx,
    graphics::CommandRecorder& recorder) -> ShaderVisibleIndex;

  //! Draws the lines the current view stored this frame into `target`, then
  //! reads its counters back. False when the view was not prepared.
  OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const graphics::Framebuffer& target)
    -> bool;

  //! The latest counters read back for `view_id`.
  OXGN_VRTX_NDAPI auto GetStats(ViewId view_id) const
    -> std::optional<GpuDebugPrimitiveStats>;

  OXGN_VRTX_API auto RemoveView(ViewId view_id) -> void;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace oxygen::vortex
