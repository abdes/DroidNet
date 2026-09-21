//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/RendererTag.h>
#include <Oxygen/Vortex/Resources/IResourceBinder.h>
#include <Oxygen/Vortex/Resources/MaterialBinder.h>
#include <Oxygen/Vortex/ScenePrep/Handles.h>
#include <Oxygen/Vortex/Test/Fakes/AssetLoader.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Test/Fixtures/MaterialBinderTest.h>
#include <Oxygen/Vortex/Types/MaterialShadingConstants.h>
#include <Oxygen/Vortex/Types/ProceduralGridMaterialConstants.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>
#include <Oxygen/Vortex/Upload/UploaderTag.h>

namespace oxygen::vortex::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::vortex::upload::internal

namespace oxygen::vortex::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::vortex::internal

namespace oxygen::vortex::testing {

auto MaterialBinderTest::MakeMaterial(const MaterialRecipe& recipe)
  -> std::shared_ptr<const data::MaterialAsset>
{
  data::pak::render::MaterialAssetDesc desc {};
  desc.base_color_texture = data::pak::core::ResourceIndexT {
    recipe.raw_base_color_index,
  };
  desc.normal_texture = data::pak::core::ResourceIndexT {
    recipe.raw_normal_index,
  };
  std::ranges::copy(recipe.base_color, std::begin(desc.base_color));
  std::ranges::copy(recipe.uv_scale, std::begin(desc.uv_scale));
  std::ranges::copy(recipe.uv_offset, std::begin(desc.uv_offset));
  desc.uv_rotation_radians = recipe.uv_rotation_radians;
  desc.uv_set = recipe.uv_set;
  return std::make_shared<data::MaterialAsset>(data::AssetKey {}, desc,
    std::vector<data::ShaderReference> {},
    std::vector {
      recipe.base_color_key,
      recipe.normal_key,
    });
}

auto MaterialBinderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<vortex::upload::UploadCoordinator>(
    observer_ptr {
      gfx_.get(),
    },
    vortex::upload::DefaultUploadPolicy());

  staging_provider_ = uploader_->CreateRingBufferStaging(
    frame::SlotCount {
      1,
    },
    4);

  texture_binder_ = std::make_unique<FakeTextureBinder>();

  // Create a dedicated descriptor allocator for texture bindings so tests
  // can observe texture-binder allocations independently of the graphics
  // backend allocator (material atlas SRV creation etc.).
  texture_descriptor_allocator_ = std::make_unique<MiniDescriptorAllocator>();
  texture_binder_->SetDescriptorAllocator(texture_descriptor_allocator_.get());
  asset_loader_ = std::make_unique<FakeAssetLoader>();

  material_binder_ = std::make_unique<resources::MaterialBinder>(
    observer_ptr {
      gfx_.get(),
    },
    observer_ptr {
      uploader_.get(),
    },
    observer_ptr {
      staging_provider_.get(),
    },
    observer_ptr {
      texture_binder_.get(),
    },
    observer_ptr {
      asset_loader_.get(),
    });
}

auto MaterialBinderTest::TearDown() -> void
{
  material_binder_.reset();
  texture_binder_.reset();
  texture_descriptor_allocator_.reset();
  asset_loader_.reset();
  staging_provider_.reset();
  uploader_.reset();
  gfx_.reset();
}

auto MaterialBinderTest::GfxPtr() const -> observer_ptr<Graphics>
{
  return observer_ptr<Graphics>(gfx_.get());
}

auto MaterialBinderTest::Uploader() const -> vortex::upload::UploadCoordinator&
{
  return *uploader_;
}

void MaterialBinderTest::EmitMaterialAssetEviction(
  const data::AssetKey& key, const content::EvictionReason reason) const
{
  ASSERT_NE(asset_loader_, nullptr);
  asset_loader_->EmitMaterialAssetEviction(key, reason);
}

auto MaterialBinderTest::TexBinder() const -> resources::IResourceBinder&
{
  // When tests explicitly obtain a reference to the texture binder we
  // assume they intend to request concrete allocations; enable allocation on
  // request so subsequent `GetOrAllocate` calls will create shader-visible
  // descriptors.
  texture_binder_->SetAllocateOnRequest(true);
  return *texture_binder_;
}

auto MaterialBinderTest::MatBinder() const -> resources::MaterialBinder&
{
  return *material_binder_;
}

auto MaterialBinderTest::MaterialConstants(
  sceneprep::MaterialHandle handle) const -> const MaterialShadingConstants&
{
  const auto constants = MatBinder().GetMaterialShadingConstants();
  const auto index = static_cast<std::size_t>(handle.get());
  if (!MatBinder().IsHandleValid(handle) || index >= constants.size()) {
    throw std::out_of_range("Material handle has no shading constants");
  }
  return *std::next(constants.begin(), static_cast<std::ptrdiff_t>(index));
}

auto MaterialBinderTest::GridConstants(sceneprep::MaterialHandle handle) const
  -> const ProceduralGridMaterialConstants&
{
  const auto constants = MatBinder().GetProceduralGridMaterialConstants();
  const auto index = static_cast<std::size_t>(handle.get());
  if (!MatBinder().IsHandleValid(handle) || index >= constants.size()) {
    throw std::out_of_range("Material handle has no grid constants");
  }
  return *std::next(constants.begin(), static_cast<std::ptrdiff_t>(index));
}

auto MaterialBinderTest::AllocatedTextureSrvCount() const -> uint32_t
{
  // Prefer allocator used by the FakeTextureBinder (if configured) so tests
  // measure texture-binder allocations independently of other descriptor
  // activity (e.g. material atlas SRV creation). Fall back to the graphics
  // allocator when no texture-specific allocator is set.
  if (texture_binder_) {
    if (auto* ta = texture_binder_->GetDescriptorAllocator()) {
      return ta
        ->GetAllocatedDescriptorsCount(graphics::ResourceViewType::kTexture_SRV,
          graphics::DescriptorVisibility::kShaderVisible)
        .get();
    }
  }

  const auto& allocator = gfx_->GetDescriptorAllocator();
  return allocator
    .GetAllocatedDescriptorsCount(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible)
    .get();
}

auto MaterialBinderTest::TexBinderGetOrAllocateTotalCalls() const -> uint32_t
{
  return texture_binder_ ? texture_binder_->GetOrAllocateTotalCalls() : 0U;
}

auto MaterialBinderTest::TexBinderGetOrAllocateCallsForKey(
  const content::ResourceKey& key) const -> uint32_t
{
  return texture_binder_ ? texture_binder_->GetOrAllocateCallsForKey(key) : 0U;
}

auto MaterialBinderTest::GetPlaceholderIndexForKey(
  const content::ResourceKey& key) const -> ShaderVisibleIndex
{
  // Return the index currently associated with `key` from the fake binder.
  // The fake binder mimics the production binder by allocating a
  // shader-visible descriptor for per-entry placeholders immediately,
  // so this will return a valid, stable index for non-reserved keys.
  if (texture_binder_) {
    return texture_binder_->GetOrAllocate(key);
  }
  return ShaderVisibleIndex {
    0U,
  };
}

void MaterialBinderTest::SetTextureBinderAllocateOnRequest(bool v) const
{
  if (texture_binder_) {
    texture_binder_->SetAllocateOnRequest(v);
  }
}

void MaterialBinderTest::SetTextureBinderErrorKey(
  const content::ResourceKey& key) const
{
  texture_binder_->SetErrorKey(key);
}

} // namespace oxygen::vortex::testing
