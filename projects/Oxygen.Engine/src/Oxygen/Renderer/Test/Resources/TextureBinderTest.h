//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/TextureResourceLoader.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Resources/TextureBinderTestPayloads.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>
#include <Oxygen/Renderer/Upload/UploadPolicy.h>

namespace oxygen::renderer::testing {

//! Deterministic fake for callback-based texture loads.
/*!
 This fake avoids OxCo activation requirements by completing loads
 synchronously on the calling thread.
*/
class FakeTextureResourceLoader final : public content::TextureResourceLoader {
public:
  FakeTextureResourceLoader() = default;
  ~FakeTextureResourceLoader() override = default;

  OXYGEN_MAKE_NON_COPYABLE(FakeTextureResourceLoader)
  OXYGEN_DEFAULT_MOVABLE(FakeTextureResourceLoader)

  void StartLoadTexture(content::ResourceKey key,
    std::function<void(std::shared_ptr<data::TextureResource>)> on_complete)
    override
  {
    const auto it = results_.find(key);
    if (it == results_.end()) {
      on_complete(nullptr);
      return;
    }
    on_complete(it->second);
  }

  [[nodiscard]] auto MintSyntheticTextureKey() -> content::ResourceKey override
  {
    return content::ResourceKey { next_key_++ };
  }

  auto SetTexture(content::ResourceKey key,
    std::shared_ptr<data::TextureResource> texture) -> void
  {
    results_.insert_or_assign(key, std::move(texture));
  }

  auto SetLoadFailure(content::ResourceKey key) -> void
  {
    results_.insert_or_assign(key, nullptr);
  }

  [[nodiscard]] auto PreloadCookedTexture(std::span<const std::uint8_t> payload)
    -> content::ResourceKey
  {
    const auto key = MintSyntheticTextureKey();
    PreloadCookedTexture(key, payload);
    return key;
  }

  auto PreloadCookedTexture(
    content::ResourceKey key, std::span<const std::uint8_t> payload) -> void
  {
    const auto decoded = DecodeCookedTexturePayload(payload);
    if (decoded == nullptr) {
      throw std::runtime_error("DecodeCookedTexturePayload failed");
    }
    SetTexture(key, decoded);
  }

private:
  std::unordered_map<content::ResourceKey,
    std::shared_ptr<data::TextureResource>>
    results_;

  std::uint64_t next_key_ { 1U };
};

//! Test harness for TextureBinder unit tests.
/*! Provides a minimal renderer upload environment (FakeGraphics + real
    UploadCoordinator + staging provider) without depending on the
    UploadCoordinator test suite.

    The harness also owns a fake TextureResourceLoader and constructs a
    TextureBinder ready for tests.
*/
class TextureBinderTest : public ::testing::Test {
protected:
  TextureBinderTest() = default;

  virtual auto ConfigureGraphics(renderer::testing::FakeGraphics&) -> void { }

  auto SetUp() -> void override;

  [[nodiscard]] auto Gfx() -> renderer::testing::FakeGraphics& { return *gfx_; }
  [[nodiscard]] auto GfxPtr() const -> observer_ptr<::oxygen::Graphics>
  {
    return observer_ptr<::oxygen::Graphics>(gfx_.get());
  }

  [[nodiscard]] auto Uploader() -> engine::upload::UploadCoordinator&
  {
    return *uploader_;
  }

  [[nodiscard]] auto Staging() -> engine::upload::StagingProvider&
  {
    return *staging_provider_;
  }

  [[nodiscard]] auto Loader() -> FakeTextureResourceLoader&
  {
    return *texture_loader_;
  }

  [[nodiscard]] auto Binder() -> resources::TextureBinder&
  {
    return *texture_binder_;
  }

  [[nodiscard]] auto AllocatedSrvCount() const -> uint32_t;

private:
  std::shared_ptr<renderer::testing::FakeGraphics> gfx_;
  std::unique_ptr<engine::upload::UploadCoordinator> uploader_;
  std::shared_ptr<engine::upload::StagingProvider> staging_provider_;
  std::unique_ptr<FakeTextureResourceLoader> texture_loader_;
  std::unique_ptr<resources::TextureBinder> texture_binder_;
};

} // namespace oxygen::renderer::testing
