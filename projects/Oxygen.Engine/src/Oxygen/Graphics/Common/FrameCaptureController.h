//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class Surface;

//! Backend-owned GPU frame-capture control surface.
class OXGN_GFX_API FrameCaptureController {
public:
  virtual ~FrameCaptureController() = default;

  [[nodiscard]] virtual auto GetProviderName() const noexcept
    -> std::string_view
    = 0;
  [[nodiscard]] virtual auto IsAvailable() const noexcept -> bool = 0;
  [[nodiscard]] virtual auto IsCapturing() const noexcept -> bool = 0;
  [[nodiscard]] virtual auto DescribeState() const -> std::string = 0;

  virtual auto TriggerNextFrame() -> bool = 0;
  virtual auto StartCapture(observer_ptr<Surface> surface = {}) -> bool = 0;
  virtual auto EndCapture(observer_ptr<Surface> surface = {}) -> bool = 0;
  virtual auto DiscardCapture(observer_ptr<Surface> surface = {}) -> bool = 0;
  virtual auto SetCaptureFileTemplate(std::string_view path_template) -> bool
    = 0;
  virtual auto LaunchReplayUI(bool connect_target_control = true) -> bool = 0;

  virtual auto OnBeginFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
    = 0;
  virtual auto OnEndFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
    = 0;
};

} // namespace oxygen::graphics
