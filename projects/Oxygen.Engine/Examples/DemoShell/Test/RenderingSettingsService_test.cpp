//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>

#include <Oxygen/Testing/GTest.h>

#include "DemoShell/Services/RenderingSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

namespace oxygen::examples::testing {

namespace {

  class RenderingSettingsServiceTest : public ::testing::Test {
  protected:
    void SetUp() override { ResetDemoSettings(); }
    void TearDown() override { ResetDemoSettings(); }

    static auto ResetDemoSettings() -> void
    {
      const auto settings = SettingsService::ForDemoApp();
      ASSERT_NE(settings, nullptr);
      std::error_code ec;
      std::filesystem::remove(settings->GetStoragePath(), ec);
      settings->Load();
    }

    RenderingSettingsService service_ {};
  };

} // namespace

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_DefaultsToUltraForBackwardCompatibility)
{
  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kUltra);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_PersistsReadableStringValues)
{
  service_.SetShadowQualityTier(ShadowQualityTier::kMedium);

  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  ASSERT_TRUE(settings->GetString("rendering.shadow_quality_tier").has_value());
  EXPECT_EQ(*settings->GetString("rendering.shadow_quality_tier"), "medium");
  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kMedium);
}

NOLINT_TEST_F(RenderingSettingsServiceTest,
  ShadowQualityTier_ReadsLegacyNumericPersistence)
{
  const auto settings = SettingsService::ForDemoApp();
  ASSERT_NE(settings, nullptr);
  settings->SetFloat("rendering.shadow_quality_tier", 1.0F);

  EXPECT_EQ(service_.GetShadowQualityTier(), ShadowQualityTier::kMedium);
}

} // namespace oxygen::examples::testing
