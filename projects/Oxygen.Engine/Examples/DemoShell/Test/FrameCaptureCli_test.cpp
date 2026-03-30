//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>
#include <string>

#include <Oxygen/Testing/GTest.h>

#include "Common/FrameCaptureCli.h"

namespace oxygen::examples::testing {

NOLINT_TEST(FrameCaptureCli, RejectsPixStartupCaptureFromFrameZero)
{
  const cli::FrameCaptureCliState state {
    .provider = "pix",
    .load = "search",
    .from_frame = 0,
    .frame_count = 1,
  };

  try {
    static_cast<void>(cli::BuildFrameCaptureConfig(state));
    FAIL() << "Expected PIX startup capture from frame 0 to be rejected";
  } catch (const cli::FrameCaptureCliError& ex) {
    EXPECT_THAT(std::string(ex.what()),
      ::testing::HasSubstr("--capture-from-frame must be greater than 0 when "
                           "--capture-provider=pix"));
  }
}

NOLINT_TEST(FrameCaptureCli, AcceptsPixStartupCaptureFromPositiveFrame)
{
  const cli::FrameCaptureCliState state {
    .provider = "pix",
    .load = "search",
    .output = "captures/pix/render_scene",
    .from_frame = 1,
    .frame_count = 1,
  };

  const auto config = cli::BuildFrameCaptureConfig(state);

  EXPECT_EQ(config.provider, FrameCaptureProvider::kPix);
  EXPECT_EQ(config.init_mode, FrameCaptureInitMode::kSearchPath);
  EXPECT_EQ(config.from_frame, 1U);
  EXPECT_EQ(config.frame_count, 1U);
  EXPECT_EQ(config.capture_file_template, "captures/pix/render_scene");
}

} // namespace oxygen::examples::testing
