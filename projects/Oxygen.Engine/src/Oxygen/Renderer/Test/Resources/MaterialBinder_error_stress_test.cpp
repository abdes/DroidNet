//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Renderer/RendererTag.h>
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
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  // No materials allocated; EnsureFrameResources should be safe to call.
  MaterialBinderRef().EnsureFrameResources();
}

//! Allocate a large number of materials/textures to detect descriptor
//! exhaustion / stability.
NOLINT_TEST_F(MaterialBinderErrorStressTest, DescriptorExhaustionStress)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  const int N = 512;
  for (int i = 0; i < N; ++i) {
    const oxygen::content::ResourceKey base { static_cast<uint32_t>(
      100000 + i * 2) };
    const oxygen::content::ResourceKey normal { static_cast<uint32_t>(
      100001 + i * 2) };

    auto desc = std::make_shared<oxygen::data::MaterialAsset>(
      oxygen::data::pak::MaterialAssetDesc {},
      std::vector<oxygen::data::ShaderReference> {},
      std::vector<oxygen::content::ResourceKey> { base, normal });

    oxygen::engine::sceneprep::MaterialRef ref;
    ref.asset = desc;
    MaterialBinderRef().GetOrAllocate(ref);

    // allocate texture SRVs via texture binder
    const auto tmpBase = TextureBinderRef().GetOrAllocate(base);
    const auto tmpNormal = TextureBinderRef().GetOrAllocate(normal);
  }

  // At least some descriptors must have been allocated.
  EXPECT_GT(AllocatedTextureSrvCount(), 0u);
}

//! EnsureFrameResources uploads can be invoked after marking materials dirty
//! (smoke).
NOLINT_TEST_F(MaterialBinderErrorStressTest, EnsureFrameResourcesUploads)
{
  const oxygen::content::ResourceKey base { 120001U };
  const oxygen::content::ResourceKey normal { 120002U };

  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  auto mat = std::make_shared<oxygen::data::MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {},
    std::vector<oxygen::content::ResourceKey> { base, normal });
  oxygen::engine::sceneprep::MaterialRef ref;
  ref.asset = mat;

  const auto h = MaterialBinderRef().GetOrAllocate(ref);
  ASSERT_TRUE(MaterialBinderRef().IsValidHandle(h));

  // Mark dirty by updating the material in place
  MaterialBinderRef().Update(h, mat);
  MaterialBinderRef().EnsureFrameResources();

  SUCCEED(); // if we reached here no crash occurred
}

//! Stress allocation loop to detect leaks or catastrophic failures.
NOLINT_TEST_F(MaterialBinderErrorStressTest, StressAllocation)
{
  Uploader().OnFrameStart(oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });
  MaterialBinderRef().OnFrameStart(
    oxygen::renderer::internal::RendererTagFactory::Get(),
    oxygen::frame::Slot { 1 });

  for (int i = 0; i < 200; ++i) {
    const oxygen::content::ResourceKey base { static_cast<uint32_t>(
      200000 + i) };
    const oxygen::content::ResourceKey normal { static_cast<uint32_t>(
      300000 + i) };

    auto m = std::make_shared<oxygen::data::MaterialAsset>(
      oxygen::data::pak::MaterialAssetDesc {},
      std::vector<oxygen::data::ShaderReference> {},
      std::vector<oxygen::content::ResourceKey> { base, normal });

    oxygen::engine::sceneprep::MaterialRef ref;
    ref.asset = m;
    MaterialBinderRef().GetOrAllocate(ref);
    const auto tmpBase = TextureBinderRef().GetOrAllocate(base);
    const auto tmpNormal = TextureBinderRef().GetOrAllocate(normal);
  }

  EXPECT_GT(AllocatedTextureSrvCount(), 0u);
}

} // namespace
