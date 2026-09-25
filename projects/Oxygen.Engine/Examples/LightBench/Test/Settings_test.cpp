//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

#include "LightBench/LightBenchSettings.h"
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::light_bench::testing {

NOLINT_TEST(LightBenchSettings, ModifiedSettingsRoundTripThroughScene)
{
  auto bench = LightScene {};
  std::shared_ptr<scene::Scene> scene = bench.CreateScene();
  bench.SetScene(observer_ptr { scene.get() });
  auto camera = LightScene::CreateReferenceCamera(*scene);
  auto modified = ReferenceSettings();
  modified.objects[0].scale.x = 1.5F;
  modified.objects[1].enabled = false;
  modified.directional.illuminance_lux = 725;
  modified.point.enabled = true;
  modified.spot.color_rgb = { .5F, .75F, 1.0F };
  modified.camera_position.x = 3;
  modified.camera_exposure.iso = 400;
  modified.camera_extents.at(4) = .25F;
  modified.exposure.manual_ev = 5;
  modified.exposure.compensation_curve = {
    { .metered_ev = -2, .compensation_ev = 1 },
    { .metered_ev = 4, .compensation_ev = -1 },
  };
  modified.tone_mapper = engine::ToneMapper::kReinhard;
  modified.gamma = 2.4F;
  const auto encoded = EncodeSettings(modified);
  ASSERT_TRUE(encoded) << encoded.error().message;
  const auto decoded = DecodeSettings(*encoded);
  ASSERT_TRUE(decoded);
  ApplySettings(*decoded, bench, camera);
  const auto captured = CaptureSettings(bench, camera);
  EXPECT_FALSE(IsReferenceSettings(captured));
  const auto saved = EncodeSettings(captured);
  ASSERT_TRUE(saved);
  EXPECT_EQ(*saved, *encoded);
  // Complete application reset is exercised through the real Reset button in
  // save_load_and_panel_return. This test owns scene/codec fidelity only.
}

NOLINT_TEST(LightBenchSettings, RejectsMalformedAndCoupledInvalidSettings)
{
  const auto encoded = EncodeSettings(ReferenceSettings());
  ASSERT_TRUE(encoded);
  const auto valid = nlohmann::json::parse(*encoded);
  const auto reject = [&](const char* label, auto mutate) -> auto {
    SCOPED_TRACE(label);
    auto candidate = valid;
    mutate(candidate);
    EXPECT_FALSE(DecodeSettings(candidate.dump()));
  };
  reject("unknown field", [](auto& j) -> auto { j["unknown"] = 1; });
  reject("obsolete version", [](auto& j) -> auto { j["version"] = 0; });
  reject("missing exposure", [](auto& j) -> auto { j.erase("exposure"); });
  reject("unknown enum", [](auto& j) -> auto { j["exposure"]["mode"] = 100; });
  reject("unordered EV", [](auto& j) -> auto { j["exposure"]["min_ev"] = 20; });
  reject("unordered percentiles",
    [](auto& j) -> auto { j["exposure"]["low_percentile"] = .95; });
  reject("duplicate curve coordinates",
    [](auto& j) -> auto { j["exposure"]["curve"] = { { 0, 0 }, { 0, 1 } }; });
  reject("bad scale", [](auto& j) -> auto { j["objects"][0]["scale"][0] = 0; });
  reject("bad quaternion",
    [](auto& j) -> auto { j["camera"]["rotation"] = { 0, 0, 0, 0 }; });
  reject(
    "bad clipping planes", [](auto& j) { j["camera"]["extents"][4] = 200; });
  reject("bad camera", [](auto& j) -> auto { j["camera"]["iso"] = 0; });
  reject("bad cone", [](auto& j) -> auto { j["spot"]["inner_degrees"] = 85; });
  reject("bad direction",
    [](auto& j) -> auto { j["spot"]["direction"] = { 0, 0, 0 }; });
  EXPECT_FALSE(DecodeSettings("{"));
  EXPECT_FALSE(
    DecodeSettings(R"({"post_process":{"exposure":{"manual_ev":6}}})"));
  auto nonfinite = ReferenceSettings();
  nonfinite.directional.illuminance_lux
    = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(EncodeSettings(nonfinite));
}

NOLINT_TEST(LightBenchSettings, ShippedIndoorSettingsMatchCompletePreset)
{
  std::ifstream input(OXYGEN_LIGHTBENCH_INDOOR_SETTINGS);
  ASSERT_TRUE(input);
  const auto text = std::string(std::istreambuf_iterator<char> { input }, {});
  const auto decoded = DecodeSettings(text);
  ASSERT_TRUE(decoded) << decoded.error().message;
  EXPECT_TRUE(IsPresetSettings(*decoded));
  EXPECT_EQ(decoded->preset, LightBenchPreset::kIndoor);
  EXPECT_TRUE(decoded->point.enabled);
  EXPECT_TRUE(decoded->spot.enabled);
  EXPECT_FALSE(decoded->directional.enabled);
  EXPECT_EQ(decoded->exposure.mode, engine::ExposureMode::kAuto);
  EXPECT_EQ(decoded->tone_mapper, engine::ToneMapper::kAcesFitted);
}

NOLINT_TEST(LightBenchSettings, CompletePresetsValidateAndRoundTrip)
{
  for (const auto& info : GetPresets()) {
    SCOPED_TRACE(info.id);
    const auto settings = PresetSettings(info.preset);
    ASSERT_TRUE(IsPresetSettings(settings));
    const auto encoded = EncodeSettings(settings);
    ASSERT_TRUE(encoded) << encoded.error().message;
    const auto decoded = DecodeSettings(*encoded);
    ASSERT_TRUE(decoded) << decoded.error().message;
    EXPECT_EQ(decoded->preset, info.preset);
    EXPECT_TRUE(IsPresetSettings(*decoded));
    auto modified = *decoded;
    modified.exposure.compensation_ev += 1.0F;
    EXPECT_FALSE(IsPresetSettings(modified));
    if (info.preset == LightBenchPreset::kIndoor)
      RecordProperty("indoor_settings_json", *encoded);
  }
}

NOLINT_TEST(
  LightBenchSettings, ViewingPresetsLightGroundAndUseExplicitDisplaySettings)
{
  const auto indoor = PresetSettings(LightBenchPreset::kIndoor);
  EXPECT_FALSE(indoor.directional.enabled);
  EXPECT_TRUE(indoor.point.enabled && indoor.spot.enabled);
  EXPECT_TRUE(indoor.point.casts_shadows && indoor.spot.casts_shadows);
  EXPECT_EQ(indoor.exposure.mode, engine::ExposureMode::kAuto);
  EXPECT_EQ(indoor.tone_mapper, engine::ToneMapper::kAcesFitted);
  for (const auto preset :
    { LightBenchPreset::kAutoAdaptation, LightBenchPreset::kOutdoorDaylight }) {
    const auto settings = PresetSettings(preset);
    EXPECT_TRUE(settings.directional.enabled);
    EXPECT_LT(settings.directional.direction_ws.y, 0.0F);
    EXPECT_LT(settings.directional.direction_ws.z, 0.0F);
    EXPECT_EQ(settings.exposure.mode, engine::ExposureMode::kAuto);
    EXPECT_EQ(settings.tone_mapper, engine::ToneMapper::kAcesFitted);
    EXPECT_TRUE(std::isfinite(settings.initial_auto_ev));
    EXPECT_NE(std::abs(settings.directional.direction_ws.y),
      std::abs(settings.directional.direction_ws.z));
    for (std::size_t i = 0; i < 5; ++i)
      EXPECT_FLOAT_EQ(settings.objects.at(i).position.z, .5F);
  }
  EXPECT_FLOAT_EQ(PresetSettings(LightBenchPreset::kOutdoorDaylight)
                    .directional.illuminance_lux,
    100000.0F);
}

} // namespace oxygen::examples::light_bench::testing
