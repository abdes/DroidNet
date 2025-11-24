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
#include <functional>
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

    // Mark the specified surface for destruction. The surface is moved out
    // of the live entries and into a pending-destructions list which the
    // engine module will drain on the next frame-start. An optional callback
    // may be provided which will be invoked (on the engine thread) when the
    // destruction has been processed.
    auto RemoveSurface(const GuidKey& key, std::function<void(bool)> onProcessed = {}) -> void
    {
      std::scoped_lock lock(mutex_);
      auto iter = entries_.find(key);
      if (iter == entries_.end()) {
        if (onProcessed) {
          // Notify caller that the surface was not found.
          onProcessed(false);
        }
        return;
      }

      PendingDestruction entry;
      entry.key = key;
      entry.surface = std::move(iter->second);
      entry.callback = std::move(onProcessed);
      entries_.erase(iter);
      pending_destructions_.emplace_back(std::move(entry));
    }

    auto FindSurface(const GuidKey& key) const
      -> std::shared_ptr<oxygen::graphics::Surface>
    {
      std::scoped_lock lock(mutex_);
      auto iter = entries_.find(key);
      return iter != entries_.end() ? iter->second : std::shared_ptr<oxygen::graphics::Surface>();
    }

    auto SnapshotSurfaces() const
      -> std::vector<std::pair<GuidKey, std::shared_ptr<oxygen::graphics::Surface>>>
    {
      std::scoped_lock lock(mutex_);
      std::vector<std::pair<GuidKey, std::shared_ptr<oxygen::graphics::Surface>>> snapshot;
      snapshot.reserve(entries_.size());
      for (const auto& pair : entries_) {
        snapshot.emplace_back(pair.first, pair.second);
      }

      return snapshot;
    }

    auto Clear() -> void
    {
      // Move all live entries into the pending destruction list so the engine
      // module may process them on the next frame. This avoids final releases
      // on the caller thread.
      std::scoped_lock lock(mutex_);
      for (auto& pair : entries_) {
        PendingDestruction entry;
        entry.key = pair.first;
        entry.surface = std::move(pair.second);
        pending_destructions_.emplace_back(std::move(entry));
      }
      entries_.clear();
    }

    // Drain any pending destructions. Called by the engine module on the
    // engine thread to retrieve surfaces slated for destruction.
    auto DrainPendingDestructions() -> std::vector<std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>, std::function<void(bool)>>> >
    {
      std::scoped_lock lock(mutex_);
      std::vector<std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>, std::function<void(bool)>>>> result;
      result.reserve(pending_destructions_.size());
      for (auto& pd : pending_destructions_) {
        result.emplace_back(pd.key, std::make_pair(pd.surface, pd.callback));
      }
      pending_destructions_.clear();
      return result;
    }

    // Register a callback to be invoked when the requested surface has been
    // processed for resize on the engine thread. Multiple callbacks are
    // allowed; they will be invoked and cleared when the resize happens.
    auto RegisterResizeCallback(const GuidKey& key, std::function<void(bool)> cb) -> void
    {
      std::scoped_lock lock(mutex_);
      resize_callbacks_[key].emplace_back(std::move(cb));
    }

    // Pop all registered resize callbacks for a given key (engine-thread only).
    auto DrainResizeCallbacks(const GuidKey& key) -> std::vector<std::function<void(bool)>>
    {
      std::scoped_lock lock(mutex_);
      std::vector<std::function<void(bool)>> cbs;
      auto iter = resize_callbacks_.find(key);
      if (iter == resize_callbacks_.end()) {
        return cbs;
      }
      cbs = std::move(iter->second);
      resize_callbacks_.erase(iter);
      return cbs;
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

    struct PendingDestruction {
      GuidKey key;
      std::shared_ptr<oxygen::graphics::Surface> surface;
      std::function<void(bool)> callback;
    };

    std::vector<PendingDestruction> pending_destructions_;
    std::unordered_map<GuidKey, std::vector<std::function<void(bool)>>, GuidHasher> resize_callbacks_;
  };

} // namespace Oxygen::Editor::EngineInterface

#pragma managed(pop)
