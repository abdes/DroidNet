//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/FrameContext.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/EnvironmentSettingsService.h"
#include "DemoShell/Services/PostProcessSettingsService.h"
#include "DemoShell/Services/SettingsService.h"
#include "DemoShell/UI/EnvironmentVm.h"

namespace oxygen::examples::testing {

namespace {

  class MockPostProcessSettingsService : public ui::PostProcessSettingsService {
  public:
    MOCK_METHOD(void, SetManualExposureEv, (float), (override));
    MOCK_METHOD(void, ResetAutoExposure, (float), (override));
    MOCK_METHOD(void, SetExposureMode, (engine::ExposureMode), (override));
    MOCK_METHOD(void, SetExposureEnabled, (bool), (override));
  };

  class EnvironmentVmTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      saved_settings_bytes_ = ReadSettingsBytes();
      settings->SetBool("env.settings.custom_state_present", false);
      service_.SetSunAzimuthDeg(123.0F);
      service_.SetSunElevationDeg(17.0F);
      service_.SetSunIlluminanceLx(4321.0F);
      service_.SetSkyAtmosphereEnabled(false);
      service_.SetSkySphereEnabled(true);
      service_.SetSkyIntensity(2.75F);
      service_.SetFogEnabled(true);
      service_.SetFogExtinctionSigmaTPerMeter(0.012F);
      service_.SetPresetIndex(3);
    }

    auto TearDown() -> void override
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      if (saved_settings_bytes_.has_value()) {
        std::ofstream output(settings->GetStoragePath(), std::ios::binary);
        ASSERT_TRUE(output.is_open());
        output << *saved_settings_bytes_;
        ASSERT_TRUE(output.good());
      } else {
        std::error_code error;
        std::filesystem::remove(settings->GetStoragePath(), error);
        ASSERT_FALSE(error);
      }
      settings->Load();
    }

    static auto ReadSettingsBytes() -> std::optional<std::string>
    {
      const auto settings = SettingsService::ForDemoApp();
      std::ifstream input(settings->GetStoragePath(), std::ios::binary);
      if (!input.is_open()) {
        return std::nullopt;
      }
      return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    }

    auto ExpectEnvironmentUnchanged() const -> void
    {
      EXPECT_FLOAT_EQ(service_.GetSunAzimuthDeg(), 123.0F);
      EXPECT_FLOAT_EQ(service_.GetSunElevationDeg(), 17.0F);
      EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), 4321.0F);
      EXPECT_FALSE(service_.GetSkyAtmosphereEnabled());
      EXPECT_TRUE(service_.GetSkySphereEnabled());
      EXPECT_FLOAT_EQ(service_.GetSkyIntensity(), 2.75F);
      EXPECT_TRUE(service_.GetFogEnabled());
      EXPECT_FLOAT_EQ(service_.GetFogExtinctionSigmaTPerMeter(), 0.012F);
    }

    EnvironmentSettingsService service_;
    // Any exposure write, including resetting adaptation, fails the test.
    ::testing::StrictMock<MockPostProcessSettingsService> post_process_;
    ui::EnvironmentVm vm_ { observer_ptr { &service_ },
      observer_ptr { &post_process_ }, nullptr };
    std::optional<std::string> saved_settings_bytes_;
  };

} // namespace

NOLINT_TEST_F(
  EnvironmentVmTest, SelectingCustomRetainsEnvironmentWithoutChangingExposure)
{
  vm_.ApplyPreset(1);

  EXPECT_EQ(service_.GetPresetIndex(), -1);
  EXPECT_EQ(vm_.GetPresetLabel(), "Custom");
  ExpectEnvironmentUnchanged();
}

NOLINT_TEST_F(EnvironmentVmTest,
  RejectsInvalidSelectionWithoutChangingEnvironmentOrExposure)
{
  const auto previous_epoch = service_.GetEpoch();
  for (const int index : std::array { -1, vm_.GetPresetCount(),
         std::numeric_limits<int>::min(), std::numeric_limits<int>::max() }) {
    SCOPED_TRACE(index);
    vm_.ApplyPreset(index);

    EXPECT_EQ(service_.GetPresetIndex(), 3);
    EXPECT_EQ(service_.GetEpoch(), previous_epoch);
    ExpectEnvironmentUnchanged();
  }
}

NOLINT_TEST_F(EnvironmentVmTest,
  ManualAtmosphereEditAfterBuiltInPresetBecomesCustomWithoutChangingExposure)
{
  ui::EnvironmentVm preset_vm { observer_ptr { &service_ }, nullptr, nullptr };
  preset_vm.ApplyPreset(2, false);
  ASSERT_EQ(service_.GetPresetIndex(), 0);
  ASSERT_TRUE(service_.GetSkyAtmosphereEnabled());
  const auto sun_illuminance = service_.GetSunIlluminanceLx();

  vm_.SetMieAnisotropy(0.37F);

  EXPECT_EQ(vm_.GetPresetLabel(), "Custom");
  EXPECT_FLOAT_EQ(service_.GetMieAnisotropy(), 0.37F);
  EXPECT_TRUE(service_.GetSkyAtmosphereEnabled());
  EXPECT_FLOAT_EQ(service_.GetSunIlluminanceLx(), sun_illuminance);
}

NOLINT_TEST_F(EnvironmentVmTest, PreviewToggleDoesNotChangeEnvironmentProfile)
{
  service_.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .force_environment_override = false,
    .initial_preview_sun_enabled = false,
  });
  service_.SetPresetIndex(3);

  vm_.SetPreviewSunEnabled(true);

  EXPECT_TRUE(vm_.GetPreviewSunEnabled());
  EXPECT_EQ(service_.GetPresetIndex(), 3);
  service_.ActivateCustomMode();

  vm_.SetPreviewSunEnabled(false);

  EXPECT_FALSE(vm_.GetPreviewSunEnabled());
  EXPECT_EQ(vm_.GetPresetLabel(), "Custom");
}

NOLINT_TEST_F(EnvironmentVmTest, ProfileKeysRoundTripInUiOrder)
{
  constexpr std::array<std::string_view, 7> keys { "scene", "custom",
    "outdoor-sunny", "outdoor-cloudy", "foggy-daylight", "outdoor-dawn",
    "outdoor-dusk" };
  ASSERT_EQ(vm_.GetPresetCount(), keys.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    SCOPED_TRACE(keys[i]);
    EXPECT_EQ(ui::EnvironmentVm::GetPresetKey(static_cast<int>(i)), keys[i]);
    EXPECT_EQ(ui::EnvironmentVm::FindPresetIndex(keys[i]), static_cast<int>(i));
  }
}

NOLINT_TEST_F(EnvironmentVmTest, ProfileKeysRejectAliasesAndInvalidIndices)
{
  for (const auto key : { "", "Scene", "use-scene", "Outdoor Sunny",
         "outdoor-sunny ", " outdoor-sunny", "unknown" }) {
    SCOPED_TRACE(key);
    EXPECT_FALSE(ui::EnvironmentVm::FindPresetIndex(key));
  }
  for (const int index : std::array { -1, vm_.GetPresetCount(),
         std::numeric_limits<int>::min(), std::numeric_limits<int>::max() }) {
    SCOPED_TRACE(index);
    EXPECT_TRUE(ui::EnvironmentVm::GetPresetKey(index).empty());
  }
}

NOLINT_TEST_F(EnvironmentVmTest,
  ExplicitTransientPresetSuppressesPendingWritesUntilManualEdit)
{
  const auto settings = SettingsService::ForDemoApp();
  settings->Save();
  const auto saved_bytes = ReadSettingsBytes();
  ASSERT_TRUE(saved_bytes.has_value());
  ui::EnvironmentVm vm { observer_ptr { &service_ }, nullptr, nullptr };

  vm.ApplyPreset(2, false);
  service_.OnFrameStart(engine::FrameContext {});
  settings->Save();

  EXPECT_EQ(service_.GetPresetIndex(), 0);
  EXPECT_EQ(ReadSettingsBytes(), saved_bytes);

  vm.SetMieAnisotropy(0.37F);
  service_.OnFrameStart(engine::FrameContext {});
  settings->Save();

  EXPECT_EQ(service_.GetPresetIndex(), -1);
  EXPECT_EQ(settings->GetFloat("environment_preset_index"), -1.0F);
  EXPECT_EQ(settings->GetFloat("env.atmo.mie_anisotropy"), 0.37F);
}

NOLINT_TEST_F(EnvironmentVmTest,
  TransientPresetAppliesExposureWithoutChangingSettingsBytesOnSaveOrRebind)
{
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("post_process.exposure.mode", 0.0F);
  settings->SetFloat("post_process.exposure.manual_ev", 4.5F);
  settings->SetBool("post_process.exposure.enabled", false);
  settings->Save();
  const auto saved_bytes = ReadSettingsBytes();
  ASSERT_TRUE(saved_bytes.has_value());
  auto scene = std::make_shared<scene::Scene>("Transient Profile", 16);
  EnvironmentSettingsService environment;
  ui::PostProcessSettingsService post_process;
  post_process.BindScene(observer_ptr { scene.get() });
  ui::EnvironmentVm vm { observer_ptr { &environment },
    observer_ptr { &post_process }, nullptr };

  // CLI selection initializes the service's per-run policy before the VM
  // applies its startup preset. Exercise that complete production path.
  vm.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_environment_profile = 0,
  });
  environment.OnFrameStart(engine::FrameContext {});

  EXPECT_EQ(post_process.GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_TRUE(post_process.GetExposureEnabled());
  EXPECT_FLOAT_EQ(post_process.GetManualExposureEv(), 14.0F);
  const auto volume = scene->GetEnvironment()
                        ->TryGetSystem<scene::environment::PostProcessVolume>();
  ASSERT_TRUE(volume);
  EXPECT_EQ(volume->GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_TRUE(volume->GetExposureEnabled());
  EXPECT_FLOAT_EQ(volume->GetManualExposureEv(), 14.0F);
  settings->Save();
  EXPECT_EQ(ReadSettingsBytes(), saved_bytes);

  auto next_scene = std::make_shared<scene::Scene>("Next Scene", 16);
  post_process.BindScene(observer_ptr { next_scene.get() });
  const auto next_volume
    = next_scene->GetEnvironment()
        ->TryGetSystem<scene::environment::PostProcessVolume>();
  ASSERT_TRUE(next_volume);
  EXPECT_EQ(next_volume->GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_FLOAT_EQ(next_volume->GetManualExposureEv(), 14.0F);
  settings->Save();
  EXPECT_EQ(ReadSettingsBytes(), saved_bytes);
  EXPECT_EQ(settings->GetFloat("post_process.exposure.mode"), 0.0F);
  EXPECT_EQ(settings->GetFloat("post_process.exposure.manual_ev"), 4.5F);
  EXPECT_EQ(settings->GetBool("post_process.exposure.enabled"), false);
}

NOLINT_TEST_F(EnvironmentVmTest,
  LaterEditsPersistOnlyTheirFieldsAndUiPresetReplacesTransientExposure)
{
  const auto settings = SettingsService::ForDemoApp();
  settings->SetFloat("post_process.exposure.mode", 0.0F);
  settings->SetFloat("post_process.exposure.manual_ev", 4.5F);
  settings->SetBool("post_process.exposure.enabled", false);
  auto scene = std::make_shared<scene::Scene>("Transient Then Edited", 16);
  EnvironmentSettingsService environment;
  ui::PostProcessSettingsService post_process;
  post_process.BindScene(observer_ptr { scene.get() });
  ui::EnvironmentVm vm { observer_ptr { &environment },
    observer_ptr { &post_process }, nullptr };
  vm.SetRuntimeConfig(EnvironmentRuntimeConfig {
    .scene = observer_ptr { scene.get() },
    .force_environment_override = false,
    .restore_environment_profile = true,
    .initial_environment_profile = 0,
  });

  post_process.SetGamma(2.4F);
  post_process.SetManualExposureEv(7.0F);
  settings->Save();

  EXPECT_EQ(settings->GetFloat("post_process.tonemapping.gamma"), 2.4F);
  EXPECT_EQ(settings->GetFloat("post_process.exposure.manual_ev"), 7.0F);
  EXPECT_EQ(settings->GetFloat("post_process.exposure.mode"), 0.0F);
  EXPECT_EQ(settings->GetBool("post_process.exposure.enabled"), false);
  EXPECT_FLOAT_EQ(post_process.GetManualExposureEv(), 7.0F);
  EXPECT_EQ(post_process.GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_TRUE(post_process.GetExposureEnabled());

  vm.ApplyPreset(3);

  EXPECT_FLOAT_EQ(post_process.GetManualExposureEv(), 12.0F);
  EXPECT_EQ(post_process.GetExposureMode(), engine::ExposureMode::kAuto);
  EXPECT_TRUE(post_process.GetExposureEnabled());
  EXPECT_EQ(settings->GetFloat("post_process.exposure.manual_ev"), 12.0F);
  EXPECT_EQ(settings->GetFloat("post_process.exposure.mode"),
    static_cast<float>(engine::ExposureMode::kAuto));
  EXPECT_EQ(settings->GetBool("post_process.exposure.enabled"), true);

  post_process.ApplyExposurePreset(
    engine::ExposureMode::kManualCamera, 22.0F, false, false);
  post_process.ResetToDefaults();

  EXPECT_EQ(post_process.GetExposureMode(), engine::ExposureMode::kManual);
  EXPECT_FLOAT_EQ(post_process.GetManualExposureEv(), 9.7F);
  EXPECT_TRUE(post_process.GetExposureEnabled());
}

} // namespace oxygen::examples::testing
