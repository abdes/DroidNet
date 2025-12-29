//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/ScenePrep/MaterialRef.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

namespace {

using oxygen::content::ResourceKey;
using oxygen::renderer::testing::MaterialBinderTest;

class MaterialBinderErrorStressTest : public MaterialBinderTest { };

//! EnsureFrameResources can be called without crashing (smoke)
NOLINT_TEST_F(MaterialBinderErrorStressTest, EnsureFrameResourcesSmoke)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // No materials allocated; EnsureFrameResources should be safe to call.
  MatBinder().EnsureFrameResources();
}

//! Allocate a large number of materials/textures to detect descriptor
//! exhaustion / stability.
NOLINT_TEST_F(MaterialBinderErrorStressTest, DescriptorExhaustionStress)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  constexpr int N = 512;
  for (int i = 0; i < N; ++i) {
    const ResourceKey base { static_cast<uint32_t>(100000 + (i * 2)) };
    const ResourceKey normal { static_cast<uint32_t>(100001 + (i * 2)) };

    auto desc = std::make_shared<oxygen::data::MaterialAsset>(
      oxygen::data::pak::MaterialAssetDesc {},
      std::vector<oxygen::data::ShaderReference> {},
      std::vector { base, normal });

    oxygen::engine::sceneprep::MaterialRef ref;
    ref.asset = desc;
    MatBinder().GetOrAllocate(ref);

    // allocate texture SRVs via texture binder
    [[maybe_unused]] const auto tmpBase = TexBinder().GetOrAllocate(base);
    [[maybe_unused]] const auto tmpNormal = TexBinder().GetOrAllocate(normal);
  }

  // At least some descriptors must have been allocated.
  EXPECT_GT(AllocatedTextureSrvCount(), 0U);
}

//! EnsureFrameResources uploads can be invoked after marking materials dirty
//! (smoke).
NOLINT_TEST_F(MaterialBinderErrorStressTest, EnsureFrameResourcesUploads)
{
  constexpr ResourceKey base { 120001U };
  constexpr ResourceKey normal { 120002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const auto mat = std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {},
    std::vector { base, normal });
  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = mat;

  const auto h = MatBinder().GetOrAllocate(ref);
  ASSERT_TRUE(MatBinder().IsHandleValid(h));

  // Mark dirty by updating the material in place
  MatBinder().Update(h, mat);
  MatBinder().EnsureFrameResources();

  SUCCEED(); // if we reached here no crash occurred
}

//! Stress allocation loop to detect leaks or catastrophic failures.
NOLINT_TEST_F(MaterialBinderErrorStressTest, StressAllocation)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MatBinder().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  for (int i = 0; i < 200; ++i) {
    const ResourceKey base { static_cast<uint32_t>(200000 + i) };
    const ResourceKey normal { static_cast<uint32_t>(300000 + i) };

    auto m = std::make_shared<oxygen::data::MaterialAsset>(
      oxygen::data::pak::MaterialAssetDesc {},
      std::vector<oxygen::data::ShaderReference> {},
      std::vector { base, normal });

    oxygen::engine::sceneprep::MaterialRef ref;
    ref.asset = m;
    MatBinder().GetOrAllocate(ref);
    [[maybe_unused]] const auto tmpBase = TexBinder().GetOrAllocate(base);
    [[maybe_unused]] const auto tmpNormal = TexBinder().GetOrAllocate(normal);
  }

  EXPECT_GT(AllocatedTextureSrvCount(), 0U);
}

} // namespace
