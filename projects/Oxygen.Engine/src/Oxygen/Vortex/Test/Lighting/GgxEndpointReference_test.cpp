//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>

namespace oxygen::vortex::testing::reference {
namespace {

  auto LoadAnchors() -> nlohmann::json
  {
    auto stream = std::ifstream(OXYGEN_GGX_ENDPOINT_REFERENCE_FILE);
    if (!stream) {
      throw std::runtime_error("Cannot read generated GGX endpoint reference");
    }
    return nlohmann::json::parse(stream);
  }

  NOLINT_TEST(
    GgxEndpointReferenceTest, MatchesIndependentHighPrecisionEndpoints)
  {
    const auto data = LoadAnchors();
    ASSERT_EQ(data.at("model_revision"), 1);
    ASSERT_EQ(data.at("first_precision_digits"), 60);
    ASSERT_EQ(data.at("second_precision_digits"), 90);
    ASSERT_EQ(data.at("cases").size(), 12U);
    double maximum_error = 0.0;
    for (const auto& anchor : data.at("cases")) {
      const auto roughness = anchor.at("roughness").get<double>();
      const auto mu = anchor.at("view_cosine").get<double>();
      SCOPED_TRACE(roughness);
      SCOPED_TRACE(mu);
      const auto result = IntegrateGgxMoments(
        PerceptualRoughness { roughness }, ViewCosine { mu });
      ASSERT_TRUE(result.has_value());
      const auto expected_energy
        = std::stod(anchor.at("directional_albedo").get<std::string>());
      const auto expected_bias
        = std::stod(anchor.at("schlick_moment").get<std::string>());
      const auto error
        = std::max(std::abs(result->directional_albedo - expected_energy),
          std::abs(result->schlick_moment - expected_bias));
      maximum_error = std::max(maximum_error, error);
      EXPECT_LE(error, 1.0e-8);
      EXPECT_LE(
        std::stod(anchor.at("precision_disagreement").get<std::string>()),
        1.0e-40);
    }
    RecordProperty(
      "maximum_endpoint_absolute_error", nlohmann::json(maximum_error).dump());
  }

  NOLINT_TEST(GgxEndpointReferenceTest, AnchorsRejectAlphaFloorAfterSquaring)
  {
    const auto data = LoadAnchors();
    const auto& anchor = data.at("cases").at(0);
    ASSERT_EQ(anchor.at("roughness"), 0.045);
    ASSERT_EQ(anchor.at("view_cosine"), 1);
    const auto expected
      = std::stod(anchor.at("directional_albedo").get<std::string>());
    // A floor of 0.045 on alpha incorrectly evaluates r=sqrt(0.045).
    const auto wrong_model = IntegrateGgxMoments(
      PerceptualRoughness { std::sqrt(0.045) }, ViewCosine { 1.0 });
    ASSERT_TRUE(wrong_model.has_value());
    EXPECT_GT(std::abs(wrong_model->directional_albedo - expected), 1.0e-4);
  }

} // namespace
} // namespace oxygen::vortex::testing::reference
