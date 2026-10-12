//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/Shadows/Types/Generated.ShadowContracts.h>
#include <Oxygen/Vortex/Shadows/Types/ShadowReceiverView.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {

class Renderer;
struct PreparedSceneFrame;

namespace shadows {

  //! A view's directional cascades fitted to its receivers, on the GPU.
  struct FittedCascades {
    //! The view's cascade records with fitted matrices and texel sizes, in the
    //! order of the loose records; read by lighting, shadow depth and culling.
    ShaderVisibleIndex records_srv { kInvalidShaderVisibleIndex };
    //! One receiver mask block per cascade (`ShadowReceiverFit.hlsli`).
    ShaderVisibleIndex receiver_mask_srv { kInvalidShaderVisibleIndex };
  };

  //! Fits a view's directional cascades to the receivers it sees, and marks
  //! where they lie, without CPU readback.
  /*!
   The CPU's cascades cover each split's whole frustum slice. This pass
   reduces the view's scene depth, and the boxes of its translucent draws, to
   the light-space rect each cascade's receivers occupy, narrows the cascade
   to it, and writes the fitted records to a GPU buffer. It then builds a
   receiver mask per cascade; caster culling rejects casters whose shadow
   lands on no receiver. A cascade without receivers keeps its loose record
   and an empty mask, so it draws no casters.

   Records on the view's command list after its depth prepass, before the
   shadow depths that read the fitted records.
  */
  class ShadowReceiverFitPass {
  public:
    //! The GPU buffers of a view's latest fit.
    struct Inspection {
      //! `cascade_count` `ShadowCascadeBinding` records.
      observer_ptr<const graphics::Buffer> records;
      //! One receiver mask block of `uint` tiles per cascade.
      observer_ptr<const graphics::Buffer> receiver_mask;
      std::uint32_t cascade_count { 0U };
    };

    OXGN_VRTX_API explicit ShadowReceiverFitPass(Renderer& renderer);
    OXGN_VRTX_API ~ShadowReceiverFitPass();

    OXYGEN_MAKE_NON_COPYABLE(ShadowReceiverFitPass)
    OXYGEN_MAKE_NON_MOVABLE(ShadowReceiverFitPass)

    OXGN_VRTX_API auto OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot) -> void;

    //! Fits `loose` for `view_id`; nothing when the work cannot be recorded,
    //! and the view then renders its loose cascades unmasked.
    [[nodiscard]] OXGN_VRTX_API auto Record(graphics::CommandRecorder& recorder,
      ViewId view_id, const PreparedSceneFrame& prepared_frame,
      const ShadowReceiverView& receivers,
      std::span<const ShadowCascadeBinding> loose)
      -> std::optional<FittedCascades>;

    //! The buffers of the latest fit of `view_id`; none when its cascades
    //! were not fitted.
    [[nodiscard]] OXGN_VRTX_API auto Inspect(ViewId view_id) const
      -> std::optional<Inspection>;

    OXGN_VRTX_API auto RemoveView(ViewId view_id) -> void;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace shadows
} // namespace oxygen::vortex
