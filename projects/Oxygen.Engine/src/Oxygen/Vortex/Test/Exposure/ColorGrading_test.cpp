//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr std::uint32_t kGradingProbe = 1048576U;
  constexpr float kNone = 0.0F;
  constexpr float kTolerance = 1.0e-5F;
  constexpr float kGammaTolerance = 1.0e-4F;

  //! One grading evaluation: (rgb, exposure), (saturation, contrast,
  //! vignette intensity, gamma), (content uv, tone mapper, unused).
  using GradingCase = std::array<float, 12>;

  struct Expected {
    std::array<float, 3> rgb;
    float vignette;
  };

  auto MakeCase(const std::array<float, 3> rgb, const float exposure,
    const float saturation, const float contrast, const float vignette,
    const std::array<float, 2> content_uv = { 0.5F, 0.5F },
    const float gamma = 1.0F) -> GradingCase
  {
    return GradingCase { rgb[0], rgb[1], rgb[2], exposure, saturation, contrast,
      vignette, gamma, content_uv[0], content_uv[1], kNone, 0.0F };
  }
} // namespace

//! The fixed foreground order and the golden values of the grading contract:
//! exposure once -> Rec.709 saturation -> linear contrast about 0.18 -> tone
//! curve (None clips) -> content-ellipse vignette -> DisplayGamma. Expected
//! values are written by hand, not produced by the shader helpers under test.
NOLINT_TEST_F(ExposureGpuTest, ColorGradingFollowsTheFixedForegroundOrder)
{
  const auto cases = std::vector<GradingCase> {
    // Saturation 0 leaves the Rec.709 luminance 0.2126*0.2 + 0.7152*0.4 +
    // 0.0722*0.6 = 0.37192 in every channel.
    MakeCase({ 0.2F, 0.4F, 0.6F }, 1.0F, 0.0F, 1.0F, 0.0F),
    // Contrast 0.5 about 0.18: 0.18 + 0.5 * (x - 0.18).
    MakeCase({ 0.2F, 0.4F, 0.6F }, 1.0F, 1.0F, 0.5F, 0.0F),
    // Exposure 2 is applied once; None clips 1.2 to 1.
    MakeCase({ 0.2F, 0.4F, 0.6F }, 2.0F, 1.0F, 1.0F, 0.0F),
    // Contrast precedes the tone curve: 1.2 becomes 0.69 before clipping.
    MakeCase({ 0.2F, 0.4F, 0.6F }, 2.0F, 1.0F, 0.5F, 0.0F),
    // Vignette 0.5 is 1 at the content centre.
    MakeCase({ 0.2F, 0.4F, 0.6F }, 1.0F, 1.0F, 1.0F, 0.5F, { 0.5F, 0.5F }),
    // ... and 0.5 at contentUV (1, 0.5), applied after the clip of 1.2.
    MakeCase({ 0.2F, 0.4F, 0.6F }, 2.0F, 1.0F, 1.0F, 0.5F, { 1.0F, 0.5F }),
    // Identity grading keeps the DisplayGamma 2.2 power: x^(1/2.2).
    MakeCase(
      { 0.25F, 0.5F, 0.75F }, 1.0F, 1.0F, 1.0F, 0.0F, { 0.5F, 0.5F }, 2.2F),
  };
  const auto expected = std::array<Expected, 7> {
    Expected { { 0.37192F, 0.37192F, 0.37192F }, 1.0F },
    Expected { { 0.19F, 0.29F, 0.39F }, 1.0F },
    Expected { { 0.4F, 0.8F, 1.0F }, 1.0F },
    Expected { { 0.29F, 0.49F, 0.69F }, 1.0F },
    Expected { { 0.2F, 0.4F, 0.6F }, 1.0F },
    Expected { { 0.2F, 0.4F, 0.5F }, 0.5F },
    Expected { { 0.532521F, 0.729740F, 0.877423F }, 1.0F },
  };

  const auto results = RunToneProbe(std::as_bytes(std::span(cases)),
    static_cast<std::uint32_t>(cases.size()), kGradingProbe, false);

  ASSERT_EQ(results.size(), expected.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    SCOPED_TRACE(index);
    const auto& result = results.at(index);
    const auto& want = expected.at(index);
    // The gamma case compares powers computed by hand to six digits.
    const auto tolerance = index == 6U ? kGammaTolerance : kTolerance;
    EXPECT_NEAR(result.at(0), want.rgb[0], tolerance);
    EXPECT_NEAR(result.at(1), want.rgb[1], tolerance);
    EXPECT_NEAR(result.at(2), want.rgb[2], tolerance);
    EXPECT_NEAR(result.at(3), want.vignette, kTolerance);
  }
}

} // namespace oxygen::vortex::testing::exposure
