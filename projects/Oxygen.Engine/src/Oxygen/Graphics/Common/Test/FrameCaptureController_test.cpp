//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>

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
    return "TestCapture";
  }

  [[nodiscard]] auto IsAvailable() const noexcept -> bool override
  {
    return true;
  }

  [[nodiscard]] auto IsCapturing() const noexcept -> bool override
  {
    return false;
  }

  [[nodiscard]] auto DescribeState() const -> std::string override
  {
    return "provider=TestCapture";
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

  int trigger_calls { 0 };
  int start_calls { 0 };
  int end_calls { 0 };
  int discard_calls { 0 };
  int file_template_calls { 0 };
  int replay_ui_calls { 0 };

private:
  auto DoTriggerNextFrame() -> bool override
  {
    ++trigger_calls;
    return true;
  }

  auto DoStartCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    ++start_calls;
    return true;
  }

  auto DoEndCapture(oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    ++end_calls;
    return true;
  }

  auto DoDiscardCapture(
    oxygen::observer_ptr<oxygen::graphics::Surface> /*surface*/)
    -> bool override
  {
    ++discard_calls;
    return true;
  }

  auto DoSetCaptureFileTemplate(std::string_view /*path_template*/)
    -> bool override
  {
    ++file_template_calls;
    return true;
  }

  auto DoLaunchReplayUI(bool /*connect_target_control*/) -> bool override
  {
    ++replay_ui_calls;
    return true;
  }

  oxygen::graphics::FrameCaptureFeature features_;
};

NOLINT_TEST(
  FrameCaptureControllerTest, SupportsFeatureReportsPerProviderCapabilities)
{
  const auto features = oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame
    | oxygen::graphics::FrameCaptureFeature::kManualCapture;
  TestFrameCaptureController controller(features);

  EXPECT_TRUE(controller.SupportsFeature(
    oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame));
  EXPECT_TRUE(controller.SupportsFeature(
    oxygen::graphics::FrameCaptureFeature::kManualCapture));
  EXPECT_FALSE(controller.SupportsFeature(
    oxygen::graphics::FrameCaptureFeature::kReplayUI));
  EXPECT_EQ(controller.DescribeSupportedFeatures(),
    "next_frame_capture,manual_capture");
}

NOLINT_TEST(FrameCaptureControllerTest, UnsupportedFeaturesThrowRuntimeError)
{
  TestFrameCaptureController controller(
    oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame);

  try {
    (void)controller.LaunchReplayUI();
    FAIL() << "expected LaunchReplayUI to throw for unsupported feature";
  } catch (const std::runtime_error& ex) {
    EXPECT_THAT(std::string(ex.what()), ::testing::HasSubstr("TestCapture"));
    EXPECT_THAT(
      std::string(ex.what()), ::testing::HasSubstr("launch_replay_ui"));
  }
}

NOLINT_TEST(
  FrameCaptureControllerTest, SupportedOperationsForwardToImplementation)
{
  const auto features = oxygen::graphics::FrameCaptureFeature::kTriggerNextFrame
    | oxygen::graphics::FrameCaptureFeature::kManualCapture
    | oxygen::graphics::FrameCaptureFeature::kDiscardCapture
    | oxygen::graphics::FrameCaptureFeature::kCaptureFileTemplate
    | oxygen::graphics::FrameCaptureFeature::kReplayUI;
  TestFrameCaptureController controller(features);

  EXPECT_TRUE(controller.TriggerNextFrame());
  EXPECT_TRUE(controller.StartCapture());
  EXPECT_TRUE(controller.EndCapture());
  EXPECT_TRUE(controller.DiscardCapture());
  EXPECT_TRUE(controller.SetCaptureFileTemplate("capture.rdc"));
  EXPECT_TRUE(controller.LaunchReplayUI(false));

  EXPECT_EQ(controller.trigger_calls, 1);
  EXPECT_EQ(controller.start_calls, 1);
  EXPECT_EQ(controller.end_calls, 1);
  EXPECT_EQ(controller.discard_calls, 1);
  EXPECT_EQ(controller.file_template_calls, 1);
  EXPECT_EQ(controller.replay_ui_calls, 1);
}

} // namespace
