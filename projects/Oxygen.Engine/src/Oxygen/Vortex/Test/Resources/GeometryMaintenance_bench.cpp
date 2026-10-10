//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <ratio>
#include <string>
#include <vector>

#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/GeometryUploader.h>
#include <Oxygen/Vortex/ScenePrep/GeometryRef.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fixtures/GeometryUploaderTest.h>

namespace {
using oxygen::frame::Slot;
using oxygen::vortex::internal::RendererTagFactory;
using oxygen::vortex::testing::GeometryUploaderTest;
using oxygen::vortex::testing::MakeGeometryAssetKey;

// CPU ownership/queue baseline with FakeGraphics. Native driver release cost is
// measured separately in application traces; this test has no timing assertion.
class GeometryMaintenanceBenchmark : public GeometryUploaderTest { };

NOLINT_TEST_F(GeometryMaintenanceBenchmark, DISABLED_LargeUnloadCpuBaseline)
{
  constexpr auto kAssetCount = std::uint32_t { 4096U };
  BeginFrame(Slot { 0 });
  auto& geometry = GeoUploader();
  const auto mesh = MakeValidTriangleMesh("Maintenance baseline", false);
  std::vector<oxygen::data::AssetKey> keys;
  keys.reserve(kAssetCount);
  for (std::uint32_t index = 0U; index < kAssetCount; ++index) {
    keys.push_back(
      MakeGeometryAssetKey("maintenance-" + std::to_string(index)));
    (void)geometry.GetOrAllocate(
      oxygen::vortex::sceneprep::GeometryRef { .asset_key = keys.back(),
        .lod_index = oxygen::data::LodIndex {},
        .mesh = mesh });
  }
  geometry.EnsureFrameResources();
  for (const auto key : keys) {
    Loader().EmitGeometryAssetEviction(
      key, oxygen::content::EvictionReason::kClear);
  }

  const auto limits = GeometryLimits();
  const auto drain_frames
    = (kAssetCount + limits.max_reclaimed_lods_per_frame - 1U)
    / limits.max_reclaimed_lods_per_frame;
  std::vector<double> samples;
  samples.reserve(drain_frames + 2U);
  auto& reclaimer = GfxPtr()->GetDeferredReclaimer();
  for (std::size_t frame = 0U; frame < drain_frames + 2U; ++frame) {
    const auto slot = Slot { static_cast<std::uint32_t>(frame % 2U) };
    const auto start = std::chrono::steady_clock::now();
    reclaimer.OnBeginFrame(slot);
    geometry.OnFrameStart(RendererTagFactory::Get(), slot);
    samples.push_back(std::chrono::duration<double, std::micro>(
      std::chrono::steady_clock::now() - start)
        .count());
  }
  ASSERT_EQ(geometry.GetPendingUploadCount(), 0U);
  const auto first = samples.front();
  std::ranges::sort(samples);
  const auto p95 = samples.at((samples.size() * 95U + 99U) / 100U - 1U);
  std::cout << "maintenance_cpu assets=" << kAssetCount
            << " lod_budget=" << limits.max_reclaimed_lods_per_frame
            << " first_us=" << first << " p95_us=" << p95
            << " max_us=" << samples.back() << '\n';
}
} // namespace
