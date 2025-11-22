//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Graphics/Common/Surface.h>

namespace Oxygen::Editor::EngineInterface {

class SurfaceRegistry {
public:
  using GuidKey = std::array<std::uint8_t, 16>;

  SurfaceRegistry() = default;
  ~SurfaceRegistry() = default;

  SurfaceRegistry(const SurfaceRegistry&) = delete;
  SurfaceRegistry& operator=(const SurfaceRegistry&) = delete;

  SurfaceRegistry(SurfaceRegistry&&) noexcept = delete;
  SurfaceRegistry& operator=(SurfaceRegistry&&) noexcept = delete;

  auto RegisterSurface(GuidKey key,
    std::shared_ptr<oxygen::graphics::Surface> surface) -> void
  {
    if (!surface) {
      return;
    }

    std::scoped_lock lock(mutex_);
    entries_[key] = std::move(surface);
  }

  auto RemoveSurface(const GuidKey& key)
    -> std::shared_ptr<oxygen::graphics::Surface>
  {
    std::scoped_lock lock(mutex_);
    auto iter = entries_.find(key);
    if (iter == entries_.end()) {
      return {};
    }

    auto surface = iter->second;
    entries_.erase(iter);
    return surface;
  }

  auto FindSurface(const GuidKey& key) const
    -> std::shared_ptr<oxygen::graphics::Surface>
  {
    std::scoped_lock lock(mutex_);
    auto iter = entries_.find(key);
    return iter != entries_.end() ? iter->second : std::shared_ptr<oxygen::graphics::Surface>();
  }

  auto SnapshotSurfaces() const
    -> std::vector<std::shared_ptr<oxygen::graphics::Surface>>
  {
    std::scoped_lock lock(mutex_);
    std::vector<std::shared_ptr<oxygen::graphics::Surface>> snapshot;
    snapshot.reserve(entries_.size());
    for (const auto& pair : entries_) {
      snapshot.push_back(pair.second);
    }

    return snapshot;
  }

  auto Clear() -> void
  {
    std::scoped_lock lock(mutex_);
    entries_.clear();
  }

private:
  struct GuidHasher {
    auto operator()(const GuidKey& key) const noexcept -> std::size_t
    {
      std::size_t hash = 1469598103934665603ULL;
      for (auto byte : key) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= 1099511628211ULL;
      }

      return hash;
    }
  };

  mutable std::mutex mutex_;
  std::unordered_map<GuidKey, std::shared_ptr<oxygen::graphics::Surface>, GuidHasher> entries_;
};

} // namespace Oxygen::Editor::EngineInterface

#pragma managed(pop)
