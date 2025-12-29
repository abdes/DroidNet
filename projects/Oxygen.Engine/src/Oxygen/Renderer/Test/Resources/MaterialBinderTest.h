//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Renderer/Resources/IResourceBinder.h>
#include <Oxygen/Renderer/Resources/MaterialBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>

namespace oxygen::renderer::testing {

class MaterialBinderTest : public ::testing::Test {
protected:
  auto SetUp() -> void override;

  [[nodiscard]] auto GfxPtr() const -> observer_ptr<Graphics>;

  [[nodiscard]] auto Uploader() const -> engine::upload::UploadCoordinator&;
  [[nodiscard]] auto TexBinder() const -> resources::IResourceBinder&;
  [[nodiscard]] auto MatBinder() const -> resources::MaterialBinder&;

  [[nodiscard]] auto AllocatedTextureSrvCount() const -> uint32_t;

  [[nodiscard]] auto TexBinderGetOrAllocateTotalCalls() const -> uint32_t;
  [[nodiscard]] auto TexBinderGetOrAllocateCallsForKey(
    const content::ResourceKey& key) const -> uint32_t;

  // Test helpers to control / observe FakeTextureBinder behavior.
  [[nodiscard]] auto GetPlaceholderIndexForKey(
    const content::ResourceKey& key) const -> ShaderVisibleIndex;
  void SetTextureBinderAllocateOnRequest(bool v) const;

  // Test helper: mark a ResourceKey which the FakeTextureBinder will report as
  // error.
  void SetTextureBinderErrorKey(const content::ResourceKey& key) const;

private:
  class FakeTextureBinder final : public resources::IResourceBinder {
  public:
    FakeTextureBinder() = default;

    void SetDescriptorAllocator(graphics::DescriptorAllocator* a)
    {
      allocator_ = a;
    }

    [[nodiscard]] auto GetDescriptorAllocator() const
      -> graphics::DescriptorAllocator*
    {
      return allocator_;
    }

    void SetErrorKey(content::ResourceKey key) { error_key_ = key; }
    void SetAllocateOnRequest(bool v) { allocate_on_request_ = v; }

    [[nodiscard]] auto GetOrAllocateTotalCalls() const -> uint32_t
    {
      return get_or_allocate_total_calls_;
    }

    [[nodiscard]] auto GetOrAllocateCallsForKey(
      const content::ResourceKey& key) const -> uint32_t
    {
      const auto it = get_or_allocate_calls_by_key_.find(key);
      return it == get_or_allocate_calls_by_key_.end() ? 0U : it->second;
    }

    [[nodiscard]] auto GetOrAllocate(const content::ResourceKey& key)
      -> ShaderVisibleIndex override
    {
      ++get_or_allocate_total_calls_;
      ++get_or_allocate_calls_by_key_[key];

      if (error_key_.has_value() && error_key_.value() == key) {
        return GetErrorTextureIndex();
      }

      const auto it = map_.find(key);
      if (it != map_.end()) {
        return it->second;
      }

      // If a descriptor allocator is provided and explicit allocation is
      // enabled, allocate a shader-visible descriptor to reflect real
      // TextureBinder behavior in tests. When allocation is disabled the
      // binder returns placeholder indices without consuming descriptors so
      // MaterialBinder can be exercised without triggering SRV allocations.
      if ((allocator_ != nullptr) && allocate_on_request_) {
        const auto handle
          = allocator_->Allocate(graphics::ResourceViewType::kTexture_SRV,
            graphics::DescriptorVisibility::kShaderVisible);
        const auto idx
          = static_cast<uint32_t>(handle.GetBindlessHandle().get());
        const auto sv = ShaderVisibleIndex { idx };
        map_.emplace(key, sv);
        return sv;
      }

      auto [newIt, inserted]
        = map_.try_emplace(key, ShaderVisibleIndex { next_ });
      if (inserted) {
        ++next_;
      }
      return newIt->second;
    }

    [[nodiscard]] static auto GetErrorTextureIndex() -> ShaderVisibleIndex
    {
      return ShaderVisibleIndex { 0U };
    }

  private:
    std::unordered_map<content::ResourceKey, ShaderVisibleIndex> map_;
    std::unordered_map<content::ResourceKey, uint32_t>
      get_or_allocate_calls_by_key_;
    uint32_t get_or_allocate_total_calls_ { 0U };
    std::uint32_t next_ { 1U };
    std::optional<content::ResourceKey> error_key_;
    graphics::DescriptorAllocator* allocator_ { nullptr };
    // The fake should mimic real TextureBinder: allocate shader-visible
    // descriptors for per-entry placeholders immediately. Tests may toggle
    // this for diagnostics, but the default behavior matches production.
    bool allocate_on_request_ { true };
  };

  std::shared_ptr<FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeTextureBinder> texture_binder_;
  std::unique_ptr<graphics::DescriptorAllocator> texture_descriptor_allocator_;
  std::unique_ptr<resources::MaterialBinder> material_binder_;
};

} // namespace oxygen::renderer::testing
