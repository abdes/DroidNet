//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {

  NOLINT_TEST(
    GgxMomentCertificateTest, CpuMomentsStayWithinCertifiedReferenceBudget)
  {
    auto stream = std::ifstream(OXYGEN_GGX_MOMENT_CERTIFICATE_FILE);
    ASSERT_TRUE(stream.good());
    const auto data = nlohmann::json::parse(stream);
    ASSERT_EQ(data.at("model_revision"), 1);
    ASSERT_EQ(data.at("cases").size(), 55U);
    ASSERT_EQ(data.at("radius_limit"), 1.0e-8);
    double maximum_energy_bound = 0.0;
    double maximum_bias_bound = 0.0;
    for (const auto& sample : data.at("cases")) {
      const auto roughness = sample.at("roughness").get<double>();
      const auto mu = sample.at("view_cosine").get<double>();
      SCOPED_TRACE(roughness);
      SCOPED_TRACE(mu);
      // Stopping changes are not error bounds. Compare the returned values
      // directly to the independent enclosure under the frozen 1e-5 budget.
      const auto result = IntegrateGgxMoments(PerceptualRoughness { roughness },
        ViewCosine { mu }, { .refinement_tolerance = 1.0e-6 });
      ASSERT_TRUE(result.has_value())
        << "order=" << result.error().last_estimate.order;
      const auto energy_midpoint = sample.at("E_midpoint").get<double>();
      const auto bias_midpoint = sample.at("B_midpoint").get<double>();
      const auto energy_radius = sample.at("E_radius").get<double>();
      const auto bias_radius = sample.at("B_radius").get<double>();
      ASSERT_GE(energy_radius, 0.0);
      ASSERT_GE(bias_radius, 0.0);
      ASSERT_LE(energy_radius, 1.0e-8);
      ASSERT_LE(bias_radius, 1.0e-8);
      const auto infinity = std::numeric_limits<double>::infinity();
      const auto energy_difference = std::nextafter(
        std::abs(result->directional_albedo - energy_midpoint), infinity);
      const auto bias_difference = std::nextafter(
        std::abs(result->schlick_moment - bias_midpoint), infinity);
      const auto energy_bound
        = std::nextafter(energy_difference + energy_radius, infinity);
      const auto bias_bound
        = std::nextafter(bias_difference + bias_radius, infinity);
      EXPECT_LE(energy_bound, 1.0e-5);
      EXPECT_LE(bias_bound, 1.0e-5);
      maximum_energy_bound = std::max(maximum_energy_bound, energy_bound);
      maximum_bias_bound = std::max(maximum_bias_bound, bias_bound);
    }
    RecordProperty("maximum_energy_distance_bound",
      nlohmann::json(maximum_energy_bound).dump());
    RecordProperty(
      "maximum_bias_distance_bound", nlohmann::json(maximum_bias_bound).dump());
  }

  NOLINT_TEST(GgxMomentCertificateTest,
    CertifiedInteriorRejectsDoubleAzimuthNormalization)
  {
    auto stream = std::ifstream(OXYGEN_GGX_MOMENT_CERTIFICATE_FILE);
    ASSERT_TRUE(stream.good());
    const auto data = nlohmann::json::parse(stream);
    bool checked = false;
    for (const auto& sample : data.at("cases")) {
      if (sample.at("roughness") != 1.0 || sample.at("view_cosine") != 0.5) {
        continue;
      }
      const auto midpoint = sample.at("B_midpoint").get<double>();
      const auto radius = sample.at("B_radius").get<double>();
      const auto incorrect = 2.0 * midpoint;
      EXPECT_GT(std::abs(incorrect - midpoint) - radius, 1.0e-5);
      checked = true;
    }
    EXPECT_TRUE(checked);
  }

} // namespace
} // namespace oxygen::vortex::testing::reference
