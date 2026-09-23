//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {
  struct CertifiedInterval {
    double midpoint { 0.0 };
    double radius { 0.0 };
  };
  auto DistanceBound(const double estimate, const CertifiedInterval interval)
    -> double
  {
    const auto infinity = std::numeric_limits<double>::infinity();
    const auto difference
      = std::nextafter(std::abs(estimate - interval.midpoint), infinity);
    return std::nextafter(difference + interval.radius, infinity);
  }

  NOLINT_TEST(
    GgxMeanMomentCertificateTest, CpuMeansStayWithinCertifiedReferenceBudget)
  {
    auto stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    ASSERT_TRUE(stream.good());
    const auto data = nlohmann::json::parse(stream);
    ASSERT_EQ(data.at("model_revision"), 1);
    constexpr auto roughnesses
      = std::array { 0.045, 0.1, 0.25, 0.5, 0.75, 1.0 };
    ASSERT_EQ(data.at("cases").size(), roughnesses.size());
    ASSERT_EQ(data.at("radius_limit"), 1.0e-8);
    double maximum_energy_bound = 0.0;
    double maximum_bias_bound = 0.0;
    std::size_t query_index = 0U;
    for (const auto& sample : data.at("cases")) {
      const auto roughness = sample.at("roughness").get<double>();
      ASSERT_EQ(roughness, roughnesses.at(query_index++));
      SCOPED_TRACE(roughness);
      const auto estimate
        = IntegrateGgxMeanMoments(PerceptualRoughness { roughness });
      ASSERT_TRUE(estimate.has_value())
        << "order=" << estimate.error().last_estimate.order;
      const auto e_midpoint = sample.at("E_avg_midpoint").get<double>();
      const auto b_midpoint = sample.at("B_avg_midpoint").get<double>();
      const auto e_radius = sample.at("E_avg_radius").get<double>();
      const auto b_radius = sample.at("B_avg_radius").get<double>();
      ASSERT_GE(e_radius, 0.0);
      ASSERT_GE(b_radius, 0.0);
      ASSERT_LE(e_radius, 1.0e-8);
      ASSERT_LE(b_radius, 1.0e-8);
      const auto e_bound = DistanceBound(estimate->hemispherical_albedo,
        { .midpoint = e_midpoint, .radius = e_radius });
      const auto b_bound = DistanceBound(estimate->schlick_moment,
        { .midpoint = b_midpoint, .radius = b_radius });
      EXPECT_LE(e_bound, 1.0e-5);
      EXPECT_LE(b_bound, 1.0e-5);
      maximum_energy_bound = std::max(maximum_energy_bound, e_bound);
      maximum_bias_bound = std::max(maximum_bias_bound, b_bound);
    }
    RecordProperty("maximum_mean_energy_distance_bound",
      nlohmann::json(maximum_energy_bound).dump());
    RecordProperty("maximum_mean_bias_distance_bound",
      nlohmann::json(maximum_bias_bound).dump());
  }

  NOLINT_TEST(
    GgxMeanMomentCertificateTest, CertificateRejectsUnweightedRoughMean)
  {
    auto stream = std::ifstream(OXYGEN_GGX_MEAN_CERTIFICATE_FILE);
    ASSERT_TRUE(stream.good());
    const auto data = nlohmann::json::parse(stream);
    bool checked = false;
    for (const auto& sample : data.at("cases")) {
      if (sample.at("roughness") != 1.0) {
        continue;
      }
      const auto midpoint = sample.at("E_avg_midpoint").get<double>();
      const auto radius = sample.at("E_avg_radius").get<double>();
      // Integral E(mu) dmu is 1/2 at roughness one; the required measure is
      // 2*mu.
      EXPECT_GT(std::abs(0.5 - midpoint) - radius, 1.0e-5);
      checked = true;
    }
    EXPECT_TRUE(checked);
  }
} // namespace
} // namespace oxygen::vortex::testing::reference
