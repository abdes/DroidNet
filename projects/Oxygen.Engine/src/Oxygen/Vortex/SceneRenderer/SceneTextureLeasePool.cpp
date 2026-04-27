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

auto SceneTextureLeaseKey::FromConfig(const SceneTexturesConfig& config)
  -> SceneTextureLeaseKey
{
  return SceneTextureLeaseKey {
    .extent = config.extent,
    .gbuffer_count = config.gbuffer_count,
    .enable_velocity = config.enable_velocity,
    .enable_custom_depth = config.enable_custom_depth,
    .msaa_sample_count = config.msaa_sample_count,
  };
}

SceneTextureLease::SceneTextureLease(SceneTextureLeasePool& pool,
  const std::size_t slot, const std::uint64_t generation) noexcept
  : pool_(&pool)
  , slot_(slot)
  , generation_(generation)
{
}

SceneTextureLease::~SceneTextureLease() { Release(); }

SceneTextureLease::SceneTextureLease(SceneTextureLease&& other) noexcept
  : pool_(std::exchange(other.pool_, nullptr))
  , slot_(std::exchange(other.slot_, 0U))
  , generation_(std::exchange(other.generation_, 0U))
{
}

auto SceneTextureLease::operator=(SceneTextureLease&& other) noexcept
  -> SceneTextureLease&
{
  if (this == &other) {
    return *this;
  }

  Release();
  pool_ = std::exchange(other.pool_, nullptr);
  slot_ = std::exchange(other.slot_, 0U);
  generation_ = std::exchange(other.generation_, 0U);
  return *this;
}

auto SceneTextureLease::IsValid() const noexcept -> bool
{
  return pool_ != nullptr;
}

auto SceneTextureLease::GetSceneTextures() -> SceneTextures&
{
  CHECK_F(pool_ != nullptr, "SceneTextureLease is not valid");
  auto& entry = pool_->entries_.at(slot_);
  CHECK_F(entry.active && entry.generation == generation_,
    "SceneTextureLease no longer owns its scene-texture family");
  return *entry.scene_textures;
}

auto SceneTextureLease::GetSceneTextures() const -> const SceneTextures&
{
  CHECK_F(pool_ != nullptr, "SceneTextureLease is not valid");
  const auto& entry = pool_->entries_.at(slot_);
  CHECK_F(entry.active && entry.generation == generation_,
    "SceneTextureLease no longer owns its scene-texture family");
  return *entry.scene_textures;
}

auto SceneTextureLease::GetKey() const -> const SceneTextureLeaseKey&
{
  CHECK_F(pool_ != nullptr, "SceneTextureLease is not valid");
  const auto& entry = pool_->entries_.at(slot_);
  CHECK_F(entry.active && entry.generation == generation_,
    "SceneTextureLease no longer owns its scene-texture family");
  return entry.key;
}

auto SceneTextureLease::GetLeaseId() const noexcept -> std::uint64_t
{
  if (pool_ == nullptr) {
    return 0U;
  }
  const auto& entry = pool_->entries_.at(slot_);
  return entry.lease_id;
}

void SceneTextureLease::Release() noexcept
{
  if (pool_ == nullptr) {
    return;
  }

  pool_->Release(slot_, generation_);
  pool_ = nullptr;
  slot_ = 0U;
  generation_ = 0U;
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

auto SceneTextureLeasePool::Acquire(const SceneTextureLeaseKey& key)
  -> SceneTextureLease
{
  SceneTextures::ValidateConfig(BuildConfig(key));

  const auto reusable = std::ranges::find_if(entries_,
    [&key](const Entry& entry) {
      return !entry.active && entry.key == key;
    });
  if (reusable != entries_.end()) {
    reusable->active = true;
    ++reusable->generation;
    reusable->lease_id = next_lease_id_++;
    const auto slot = static_cast<std::size_t>(
      std::distance(entries_.begin(), reusable));
    return SceneTextureLease { *this, slot, reusable->generation };
  }

  if (CountLiveLeasesForKey(key) >= max_live_leases_per_key_) {
    throw std::runtime_error(fmt::format(
      "SceneTextureLeasePool exhausted for key extent={}x{} msaa={} "
      "velocity={} custom_depth={} queue={}",
      key.extent.x, key.extent.y, key.msaa_sample_count,
      key.enable_velocity, key.enable_custom_depth,
      static_cast<std::uint32_t>(key.queue_affinity)));
  }

  auto entry = Entry {
    .key = key,
    .scene_textures = std::make_unique<SceneTextures>(gfx_, BuildConfig(key)),
    .active = true,
    .generation = 1U,
    .lease_id = next_lease_id_++,
  };
  entries_.push_back(std::move(entry));
  ++allocation_count_;
  return SceneTextureLease {
    *this, entries_.size() - 1U, entries_.back().generation
  };
}

auto SceneTextureLeasePool::GetAllocationCount() const noexcept -> std::size_t
{
  return allocation_count_;
}

auto SceneTextureLeasePool::GetLiveLeaseCount() const noexcept -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(
    entries_, [](const Entry& entry) { return entry.active; }));
}

auto SceneTextureLeasePool::GetLeaseCountForKey(
  const SceneTextureLeaseKey& key) const noexcept -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(entries_,
    [&key](const Entry& entry) { return entry.key == key; }));
}

auto SceneTextureLeasePool::GetMaxLiveLeasesPerKey() const noexcept
  -> std::size_t
{
  return max_live_leases_per_key_;
}

void SceneTextureLeasePool::Release(
  const std::size_t slot, const std::uint64_t generation) noexcept
{
  if (slot >= entries_.size()) {
    return;
  }

  auto& entry = entries_.at(slot);
  if (entry.generation != generation) {
    return;
  }
  entry.active = false;
}

auto SceneTextureLeasePool::BuildConfig(const SceneTextureLeaseKey& key) const
  -> SceneTexturesConfig
{
  auto config = base_config_;
  config.extent = key.extent;
  config.enable_velocity = key.enable_velocity;
  config.enable_custom_depth = key.enable_custom_depth;
  config.gbuffer_count = key.gbuffer_count;
  config.msaa_sample_count = key.msaa_sample_count;
  return config;
}

auto SceneTextureLeasePool::CountLiveLeasesForKey(
  const SceneTextureLeaseKey& key) const noexcept -> std::size_t
{
  return static_cast<std::size_t>(std::ranges::count_if(entries_,
    [&key](const Entry& entry) { return entry.active && entry.key == key; }));
}

} // namespace oxygen::vortex
