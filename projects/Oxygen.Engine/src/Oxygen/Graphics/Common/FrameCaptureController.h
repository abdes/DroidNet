//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

class Surface;

enum class FrameCaptureFeature : uint32_t { // NOLINT(*-enum-size)
  kNone = 0,
  kTriggerNextFrame = OXYGEN_FLAG(0),
  kManualCapture = OXYGEN_FLAG(1),
  kDiscardCapture = OXYGEN_FLAG(2),
  kCaptureFileTemplate = OXYGEN_FLAG(3),
  kReplayUI = OXYGEN_FLAG(4),
};
OXYGEN_DEFINE_FLAGS_OPERATORS(FrameCaptureFeature)

OXGN_GFX_API constexpr auto to_string(
  const FrameCaptureFeature feature) noexcept -> std::string_view
{
  switch (feature) {
  case FrameCaptureFeature::kNone:
    return "none";
  case FrameCaptureFeature::kTriggerNextFrame:
    return "next_frame_capture";
  case FrameCaptureFeature::kManualCapture:
    return "manual_capture";
  case FrameCaptureFeature::kDiscardCapture:
    return "discard_capture";
  case FrameCaptureFeature::kCaptureFileTemplate:
    return "capture_file_template";
  case FrameCaptureFeature::kReplayUI:
    return "launch_replay_ui";
  }
  return "unknown";
}

OXGN_GFX_API constexpr auto HasAllFeatures(const FrameCaptureFeature features,
  const FrameCaptureFeature required) noexcept -> bool
{
  return (features & required) == required;
}

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
  [[nodiscard]] virtual auto GetSupportedFeatures() const noexcept
    -> FrameCaptureFeature
    = 0;

  [[nodiscard]] auto SupportsFeature(
    const FrameCaptureFeature feature) const noexcept -> bool
  {
    return HasAllFeatures(GetSupportedFeatures(), feature);
  }

  [[nodiscard]] auto DescribeSupportedFeatures() const -> std::string
  {
    const auto features = GetSupportedFeatures();
    if (features == FrameCaptureFeature::kNone) {
      return "none";
    }

    std::string out;
    const auto append_feature
      = [this, &out](const FrameCaptureFeature feature) {
          if (!SupportsFeature(feature)) {
            return;
          }
          if (!out.empty()) {
            out += ',';
          }
          out += to_string(feature);
        };

    append_feature(FrameCaptureFeature::kTriggerNextFrame);
    append_feature(FrameCaptureFeature::kManualCapture);
    append_feature(FrameCaptureFeature::kDiscardCapture);
    append_feature(FrameCaptureFeature::kCaptureFileTemplate);
    append_feature(FrameCaptureFeature::kReplayUI);
    return out;
  }

  auto TriggerNextFrame() -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kTriggerNextFrame);
    return DoTriggerNextFrame();
  }

  auto StartCapture(observer_ptr<Surface> surface = {}) -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kManualCapture);
    return DoStartCapture(surface);
  }

  auto EndCapture(observer_ptr<Surface> surface = {}) -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kManualCapture);
    return DoEndCapture(surface);
  }

  auto DiscardCapture(observer_ptr<Surface> surface = {}) -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kDiscardCapture);
    return DoDiscardCapture(surface);
  }

  auto SetCaptureFileTemplate(std::string_view path_template) -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kCaptureFileTemplate);
    return DoSetCaptureFileTemplate(path_template);
  }

  auto LaunchReplayUI(bool connect_target_control = true) -> bool
  {
    EnsureFeatureSupported(FrameCaptureFeature::kReplayUI);
    return DoLaunchReplayUI(connect_target_control);
  }

  virtual auto OnBeginFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
    = 0;
  virtual auto OnEndFrame(
    frame::SequenceNumber frame_number, frame::Slot frame_slot) -> void
    = 0;
  virtual auto OnPresentSurface(observer_ptr<Surface> surface) -> void = 0;

protected:
  [[noreturn]] auto ThrowUnsupportedFeature(
    const FrameCaptureFeature feature) const -> void
  {
    throw std::runtime_error(std::string(GetProviderName())
      + " frame capture does not support " + std::string(to_string(feature)));
  }

private:
  auto EnsureFeatureSupported(const FrameCaptureFeature feature) const -> void
  {
    if (!SupportsFeature(feature)) {
      ThrowUnsupportedFeature(feature);
    }
  }

  virtual auto DoTriggerNextFrame() -> bool = 0;
  virtual auto DoStartCapture(observer_ptr<Surface> surface) -> bool = 0;
  virtual auto DoEndCapture(observer_ptr<Surface> surface) -> bool = 0;
  virtual auto DoDiscardCapture(observer_ptr<Surface> surface) -> bool = 0;
  virtual auto DoSetCaptureFileTemplate(std::string_view path_template) -> bool
    = 0;
  virtual auto DoLaunchReplayUI(bool connect_target_control) -> bool = 0;
};

} // namespace oxygen::graphics
