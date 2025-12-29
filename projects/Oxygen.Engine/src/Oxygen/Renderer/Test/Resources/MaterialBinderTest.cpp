//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/EngineTag.h>
#include <Oxygen/Renderer/RendererTag.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploaderTag.h>

#include <Oxygen/Renderer/Test/Resources/MaterialBinderTest.h>

#ifdef OXYGEN_ENGINE_TESTING

namespace oxygen::content::internal {
auto EngineTagFactory::Get() noexcept -> EngineTag { return EngineTag {}; }
} // namespace oxygen::content::internal

namespace oxygen::engine::upload::internal {
auto UploaderTagFactory::Get() noexcept -> UploaderTag
{
  return UploaderTag {};
}
} // namespace oxygen::engine::upload::internal

namespace oxygen::renderer::internal {
auto RendererTagFactory::Get() noexcept -> RendererTag
{
  return RendererTag {};
}
} // namespace oxygen::renderer::internal

#endif // OXYGEN_ENGINE_TESTING

namespace oxygen::renderer::testing {

auto MaterialBinderTest::SetUp() -> void
{
  using graphics::SingleQueueStrategy;

  gfx_ = std::make_shared<FakeGraphics>();
  gfx_->CreateCommandQueues(SingleQueueStrategy());

  uploader_ = std::make_unique<engine::upload::UploadCoordinator>(
    observer_ptr { gfx_.get() }, engine::upload::DefaultUploadPolicy());

  staging_provider_
    = uploader_->CreateRingBufferStaging(frame::SlotCount { 1 }, 4);

  texture_binder_ = std::make_unique<FakeTextureBinder>();

  // Create a dedicated descriptor allocator for texture bindings so tests
  // can observe texture-binder allocations independently of the graphics
  // backend allocator (material atlas SRV creation etc.).
  texture_descriptor_allocator_ = std::make_unique<MiniDescriptorAllocator>();
  texture_binder_->SetDescriptorAllocator(texture_descriptor_allocator_.get());

  material_binder_ = std::make_unique<resources::MaterialBinder>(
    observer_ptr { gfx_.get() }, observer_ptr { uploader_.get() },
    observer_ptr { staging_provider_.get() },
    observer_ptr { texture_binder_.get() });
}

auto MaterialBinderTest::GfxPtr() const -> observer_ptr<Graphics>
{
  return observer_ptr<Graphics>(gfx_.get());
}

auto MaterialBinderTest::Uploader() const -> engine::upload::UploadCoordinator&
{
  return *uploader_;
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
  return ShaderVisibleIndex { 0U };
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

} // namespace oxygen::renderer::testing
