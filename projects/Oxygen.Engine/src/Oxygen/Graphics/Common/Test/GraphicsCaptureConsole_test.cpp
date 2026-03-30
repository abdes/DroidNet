//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Console/Console.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Test/Mocks/MockGraphics.h>

namespace {

class TestFrameCaptureController final
  : public oxygen::graphics::FrameCaptureController {
public:
  explicit TestFrameCaptureController(
    const oxygen::graphics::FrameCaptureFeature features)
    : features_(features)
  {
  }

  [[nodiscard]] auto GetProviderName() const noexcept
    -> std::string_view override
  {
    return "PIX";
  }

  [[nodiscard]] auto IsAvailable() const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto IsCapturing() const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    return "provider=PIX available=false capturing=false status=\"waiting for "
           "gpu capturer\"";
  }

  [[nodiscard]] auto GetSupportedFeatures() const noexcept
    -> oxygen::graphics::FrameCaptureFeature override
  {
    return features_;
  }

  auto OnBeginFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
  }

  auto OnEndFrame(oxygen::frame::SequenceNumber /*frame_number*/,
    oxygen::frame::Slot /*frame_slot*/) -> void override
  {
  }

  auto OnPresentSurface(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> void override
  {
  }

private:
  auto DoTriggerNextFrame() -> bool override { return false; }

  auto DoStartCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoEndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoDiscardCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    return false;
  }

  auto DoSetCaptureFileTemplate(std::string_view /*path_template*/)
    -> bool override
  {
    return false;
  }

  auto DoLaunchReplayUI(bool /*connect_target_control*/) -> bool override
  {
    return false;
  }

  oxygen::graphics::FrameCaptureFeature features_;
};

class CaptureConsoleGraphics final
  : public oxygen::graphics::testing::MockGraphics {
public:
  CaptureConsoleGraphics(
    oxygen::observer_ptr<oxygen::graphics::FrameCaptureController>
      frame_capture)
    : MockGraphics("CaptureConsoleGraphics")
    , frame_capture_(frame_capture)
  {
  }

  [[nodiscard]] auto GetFrameCaptureController() const
    -> oxygen::observer_ptr<oxygen::graphics::FrameCaptureController> override
  {
    return frame_capture_;
  }

private:
  oxygen::observer_ptr<oxygen::graphics::FrameCaptureController> frame_capture_;
};

NOLINT_TEST(GraphicsCaptureConsoleTest, StatusCommandPrintsSummaryAndRawState)
{
  const auto features = oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame
    | oxygen::graphics::FrameCaptureFeature::kManualCapture
    | oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate;
  TestFrameCaptureController controller(features);
  CaptureConsoleGraphics graphics {
    oxygen::observer_ptr<oxygen::graphics::FrameCaptureController>(&controller)
  };
  oxygen::console::Console console;

  graphics.RegisterConsoleBindings(
    oxygen::observer_ptr<oxygen::console::Console>(&console));
  const auto result = console.Execute("gfx.capture.status");

  EXPECT_EQ(result.status, oxygen::console::ExecutionStatus::kOk);
  EXPECT_THAT(result.output, ::testing::HasSubstr("provider: PIX"));
  EXPECT_THAT(result.output, ::testing::HasSubstr("available: no"));
  EXPECT_THAT(result.output, ::testing::HasSubstr("capturing: no"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr("supported features: next_frame_capture, "
                         "manual_capture, capture_file_template"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr(
      "unsupported features: discard_capture, launch_replay_ui"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr(
      "meaningful commands: gfx.capture.status, gfx.capture.frame, "
      "gfx.capture.begin, gfx.capture.end"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr(
      "capture file template: use frame_capture.capture_file_template or "
      "--capture-output"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr(
      "availability note: provider is configured but not currently ready"));
  EXPECT_THAT(result.output,
    ::testing::HasSubstr(
      "raw state:\nprovider=PIX available=false capturing=false"));
}

} // namespace
