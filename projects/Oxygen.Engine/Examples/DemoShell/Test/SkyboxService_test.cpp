//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Scene.h>

#include "DemoShell/Services/SkyboxService.h"

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::examples::testing {

namespace {

  //! Delays only the public texture completion boundary; all other loader
  //! interfaces retain their production implementation.
  class DeferredTextureLoader final : public content::AssetLoader {
  public:
    DeferredTextureLoader()
      : AssetLoader(engine::internal::EngineTagFactory::Get())
    {
    }

    struct Request {
      content::ResourceKey key;
      std::shared_ptr<data::TextureResource> texture;
      TextureCallback callback;
    };

    using AssetLoader::StartLoadTexture;
    auto StartLoadTexture(
      content::CookedResourceData<data::TextureResource> cooked,
      TextureCallback on_complete) -> void override
    {
      data::TextureResource::DescT desc;
      ASSERT_GE(cooked.bytes.size(), sizeof(desc));
      std::memcpy(&desc, cooked.bytes.data(), sizeof(desc));
      const auto payload = cooked.bytes.subspan(sizeof(desc));
      requests.push_back(Request {
        .key = cooked.key,
        .texture = std::make_shared<data::TextureResource>(
          desc, std::vector<std::uint8_t>(payload.begin(), payload.end())),
        .callback = std::move(on_complete),
      });
    }

    auto MintSyntheticTextureKey() -> content::ResourceKey override
    {
      return content::ResourceKey { next_key_++ };
    }

    auto PinResource(const content::ResourceKey key) -> bool override
    {
      pins.push_back(key);
      return true;
    }

    auto UnpinResource(const content::ResourceKey key) -> bool override
    {
      unpins.push_back(key);
      return true;
    }

    auto Complete(const std::size_t index, const bool success = true) -> void
    {
      ASSERT_LT(index, requests.size());
      auto callback = std::exchange(requests[index].callback, {});
      ASSERT_TRUE(callback);
      callback(success ? requests[index].texture : nullptr);
    }

    std::vector<Request> requests;
    std::vector<content::ResourceKey> pins;
    std::vector<content::ResourceKey> unpins;

  private:
    auto UnsubscribeResourceEvictions(
      TypeId /*resource_type*/, std::uint64_t /*id*/) noexcept -> void override
    {
      ADD_FAILURE() << "Skybox tests do not create eviction subscriptions";
    }

    std::uint64_t next_key_ { 100 };
  };

  class SkyboxServiceTest : public ::testing::Test {
  protected:
    auto SetUp() -> void override
    {
      image_path_ = std::filesystem::current_path()
        / ("skybox-service-"
          + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count())
          + ".tga");
      ASSERT_FALSE(std::filesystem::exists(image_path_));
      std::ofstream image(image_path_, std::ios::binary);
      ASSERT_TRUE(image.is_open());
      owns_image_ = true;
      // Uncompressed 12x2 RGB TGA: six square faces in a horizontal strip.
      std::array<std::uint8_t, 18> header {};
      header[2] = 2;
      header[12] = 12;
      header[14] = 2;
      header[16] = 24;
      header[17] = 0x20;
      image.write(reinterpret_cast<const char*>(header.data()), header.size());
      const std::array<std::uint8_t, 72> pixels {};
      image.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
      ASSERT_TRUE(image.good());
      service_ = std::make_unique<SkyboxService>(
        observer_ptr { &loader_ }, observer_ptr { scene_.get() });
    }

    auto TearDown() -> void override
    {
      service_.reset();
      if (owns_image_) {
        std::error_code error;
        std::filesystem::remove(image_path_, error);
        EXPECT_FALSE(error);
      }
    }

    auto Load(const float intensity = 1.0F) -> void
    {
      service_->LoadAndEquip(image_path_.string(), options_,
        SkyboxService::SkyLightParams { .intensity_mul = intensity },
        [this](const SkyboxService::LoadResult result) {
          completed_.push_back(result);
        });
    }

    DeferredTextureLoader loader_;
    std::shared_ptr<scene::Scene> scene_
      = std::make_shared<scene::Scene>("Skybox Lifetime", 16);
    std::unique_ptr<SkyboxService> service_;
    std::filesystem::path image_path_;
    bool owns_image_ { false };
    SkyboxService::LoadOptions options_ {
      .layout = SkyboxService::Layout::kHorizontalStrip,
      .cube_face_size = 2,
    };
    std::vector<SkyboxService::LoadResult> completed_;
  };

} // namespace

NOLINT_TEST_F(SkyboxServiceTest, NewestCompletionWinsWhenLoadsFinishOutOfOrder)
{
  Load(1.0F);
  Load(2.5F);
  ASSERT_EQ(loader_.requests.size(), 2U);
  const auto newest_key = loader_.requests[1].key;

  loader_.Complete(1);
  loader_.Complete(0);

  ASSERT_EQ(completed_.size(), 1U);
  EXPECT_TRUE(completed_[0].success);
  EXPECT_EQ(completed_[0].resource_key, newest_key);
  EXPECT_EQ(service_->GetCurrentResourceKey(), newest_key);
  EXPECT_EQ(loader_.pins, std::vector { newest_key });
  const auto sky_light
    = scene_->GetEnvironment()->TryGetSystem<scene::environment::SkyLight>();
  ASSERT_TRUE(sky_light);
  EXPECT_EQ(sky_light->GetCubemapResource(), newest_key);
  EXPECT_FLOAT_EQ(sky_light->GetIntensityMul(), 2.5F);
  service_.reset();
  EXPECT_EQ(loader_.unpins, std::vector { newest_key });
}

NOLINT_TEST_F(SkyboxServiceTest, CancelPreventsPinEquipAndStatusPublication)
{
  Load();
  ASSERT_EQ(loader_.requests.size(), 1U);
  service_->CancelPendingLoads();

  loader_.Complete(0);

  EXPECT_TRUE(completed_.empty());
  EXPECT_TRUE(loader_.pins.empty());
  EXPECT_FALSE(scene_->HasEnvironment());
  EXPECT_TRUE(service_->GetCurrentResourceKey().IsPlaceholder());
}

NOLINT_TEST_F(SkyboxServiceTest, DestructionInvalidatesDeferredCompletion)
{
  Load();
  ASSERT_EQ(loader_.requests.size(), 1U);
  service_.reset();

  loader_.Complete(0);

  EXPECT_TRUE(completed_.empty());
  EXPECT_TRUE(loader_.pins.empty());
  EXPECT_FALSE(scene_->HasEnvironment());
}

NOLINT_TEST_F(SkyboxServiceTest, SceneExpiryInvalidatesDeferredCompletion)
{
  Load();
  ASSERT_EQ(loader_.requests.size(), 1U);
  const std::weak_ptr<scene::Scene> lifetime = scene_;
  scene_.reset();
  ASSERT_TRUE(lifetime.expired());

  loader_.Complete(0);

  EXPECT_TRUE(completed_.empty());
  EXPECT_TRUE(loader_.pins.empty());
  EXPECT_TRUE(service_->GetCurrentResourceKey().IsPlaceholder());
}

NOLINT_TEST_F(
  SkyboxServiceTest, CancellationPreservesPreviouslyEquippedResource)
{
  Load();
  ASSERT_EQ(loader_.requests.size(), 1U);
  loader_.Complete(0);
  const auto equipped_key = loader_.requests[0].key;
  Load(3.0F);
  ASSERT_EQ(loader_.requests.size(), 2U);
  service_->CancelPendingLoads();

  loader_.Complete(1, false);

  EXPECT_EQ(completed_.size(), 1U);
  EXPECT_EQ(service_->GetCurrentResourceKey(), equipped_key);
  EXPECT_EQ(loader_.pins, std::vector { equipped_key });
  EXPECT_TRUE(loader_.unpins.empty());
}

NOLINT_TEST_F(SkyboxServiceTest, RejectedNewRequestStillInvalidatesPreviousLoad)
{
  Load();
  ASSERT_EQ(loader_.requests.size(), 1U);
  service_->StartLoadSkybox(
    {}, options_, [this](SkyboxService::LoadResult result) {
      completed_.push_back(std::move(result));
    });
  ASSERT_EQ(completed_.size(), 1U);
  EXPECT_FALSE(completed_[0].success);

  loader_.Complete(0);

  EXPECT_EQ(completed_.size(), 1U);
  EXPECT_TRUE(loader_.pins.empty());
  EXPECT_FALSE(scene_->HasEnvironment());
}

} // namespace oxygen::examples::testing
