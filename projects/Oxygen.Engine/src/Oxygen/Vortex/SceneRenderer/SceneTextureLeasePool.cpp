//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>

namespace oxygen::vortex {

// A retained extraction can outlive the pool owner. Its family is reusable only
// after the last lease is released, without callbacks into a destroyed pool.
struct SceneTextureLeaseStorage {
  SceneTextureLeaseKey key;
  std::unique_ptr<SceneTextures> scene_textures;
  std::uint64_t lease_id;
  bool retired { true };
};

auto SceneTextureLeaseKey::FromConfig(const SceneTexturesConfig& config)
  -> SceneTextureLeaseKey
{
  return SceneTextureLeaseKey {
    .extent = config.extent,
    .scene_color_format = config.scene_color_format,
    .gbuffer_count = config.gbuffer_count,
    .enable_velocity = config.enable_velocity,
    .enable_custom_depth = config.enable_custom_depth,
    .msaa_sample_count = config.msaa_sample_count,
  };
}

SceneTextureLease::SceneTextureLease(
  std::shared_ptr<SceneTextureLeaseStorage> storage) noexcept
  : storage_(std::move(storage))
{
}
SceneTextureLease::~SceneTextureLease() = default;
SceneTextureLease::SceneTextureLease(SceneTextureLease&& other) noexcept
  = default;
auto SceneTextureLease::operator=(SceneTextureLease&& other) noexcept
  -> SceneTextureLease&
{
  if (this != &other) {
    Release();
    storage_ = std::move(other.storage_);
  }
  return *this;
}
auto SceneTextureLease::IsValid() const noexcept -> bool
{
  return storage_ != nullptr;
}
auto SceneTextureLease::GetSceneTextures() -> SceneTextures&
{
  CHECK_NOTNULL_F(storage_.get());
  return *storage_->scene_textures;
}
auto SceneTextureLease::GetSceneTextures() const -> const SceneTextures&
{
  CHECK_NOTNULL_F(storage_.get());
  return *storage_->scene_textures;
}
auto SceneTextureLease::GetKey() const -> const SceneTextureLeaseKey&
{
  CHECK_NOTNULL_F(storage_.get());
  return storage_->key;
}
auto SceneTextureLease::GetLeaseId() const noexcept -> std::uint64_t
{
  return storage_ ? storage_->lease_id : 0U;
}
void SceneTextureLease::Release() noexcept { storage_.reset(); }

void SceneTextureLease::Retire(Graphics& gfx)
{
  CHECK_NOTNULL_F(storage_.get());
  if (!storage_->retired) {
    return;
  }
  storage_->retired = false;
  // Release the writable color lease at submission, not at attachment
  // retirement. Its own readers start its fence retirement independently.
  storage_->scene_textures->ReleaseLeasedSceneColor();
  // The pool may be destroyed before this frame retires. A weak token neither
  // calls a destroyed pool nor creates a Graphics ownership cycle via color.
  gfx.GetDeferredReclaimer().RegisterDeferredAction(
    [entry = std::weak_ptr<SceneTextureLeaseStorage>(storage_)] {
      if (const auto storage = entry.lock()) {
        storage->retired = true;
      }
    });
}

SceneTextureLeasePool::SceneTextureLeasePool(Graphics& gfx,
  SceneTexturesConfig base_config, const std::size_t max_live_leases_per_key)
  : gfx_(gfx)
  , base_config_(base_config)
  , max_live_leases_per_key_(max_live_leases_per_key)
{
  if (max_live_leases_per_key_ == 0U) {
    throw std::invalid_argument(
      "SceneTextureLeasePool requires at least one live lease per key");
  }
  SceneTextures::ValidateConfig(base_config_);
}

SceneTextureLeasePool::~SceneTextureLeasePool() = default;

auto SceneTextureLeasePool::Acquire(const SceneTextureLeaseKey& key,
  std::shared_ptr<graphics::Texture> leased_color) -> SceneTextureLease
{
  SceneTextures::ValidateConfig(BuildConfig(key));

  const auto reusable
    = std::ranges::find_if(entries_, [&key](const auto& entry) {
        return entry.use_count() == 1 && entry->retired && entry->key == key;
      });
  if (reusable != entries_.end()) {
    if (leased_color) {
      (*reusable)->scene_textures->SetLeasedSceneColor(std::move(leased_color));
    }
    (*reusable)->lease_id = next_lease_id_++;
    return SceneTextureLease { *reusable };
  }

  if (CountLiveLeasesForKey(key) >= max_live_leases_per_key_) {
    throw std::runtime_error(fmt::format(
      "SceneTextureLeasePool exhausted for key extent={}x{} msaa={} "
      "velocity={} custom_depth={} queue={}",
      key.extent.x, key.extent.y, key.msaa_sample_count, key.enable_velocity,
      key.enable_custom_depth, static_cast<std::uint32_t>(key.queue_affinity)));
  }

  auto entry
    = std::make_shared<SceneTextureLeaseStorage>(SceneTextureLeaseStorage {
      .key = key,
      .scene_textures = std::make_unique<SceneTextures>(
        gfx_, BuildConfig(key), std::move(leased_color)),
      .lease_id = next_lease_id_++,
    });
  entries_.push_back(entry);
  ++allocation_count_;
  return SceneTextureLease { std::move(entry) };
}

auto SceneTextureLeasePool::GetAllocationCount() const noexcept -> std::size_t
{
  return allocation_count_;
}

auto SceneTextureLeasePool::GetLiveLeaseCount() const noexcept -> std::size_t
{
  return static_cast<std::size_t>(
    std::ranges::count_if(entries_, [](const auto& entry) {
      return entry.use_count() > 1 || !entry->retired;
    }));
}

auto SceneTextureLeasePool::GetLeaseCountForKey(
  const SceneTextureLeaseKey& key) const noexcept -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(
    entries_, [&key](const auto& entry) { return entry->key == key; }));
}

auto SceneTextureLeasePool::GetMaxLiveLeasesPerKey() const noexcept
  -> std::size_t
{
  return max_live_leases_per_key_;
}

auto SceneTextureLeasePool::BuildConfig(const SceneTextureLeaseKey& key) const
  -> SceneTexturesConfig
{
  auto config = base_config_;
  config.extent = key.extent;
  config.scene_color_format = key.scene_color_format;
  config.enable_velocity = key.enable_velocity;
  config.enable_custom_depth = key.enable_custom_depth;
  config.gbuffer_count = key.gbuffer_count;
  config.msaa_sample_count = key.msaa_sample_count;
  return config;
}

auto SceneTextureLeasePool::CountLiveLeasesForKey(
  const SceneTextureLeaseKey& key) const noexcept -> std::size_t
{
  return static_cast<std::size_t>(
    std::ranges::count_if(entries_, [&key](const auto& entry) {
      return (entry.use_count() > 1 || !entry->retired) && entry->key == key;
    }));
}

} // namespace oxygen::vortex
