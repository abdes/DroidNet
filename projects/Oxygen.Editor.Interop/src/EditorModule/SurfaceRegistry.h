//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Oxygen/Graphics/Common/Surface.h>

namespace oxygen::interop::module {

  //! Thread-safe registry that stores and manages shared ownership of
  //! `oxygen::graphics::Surface` instances keyed by a 16-byte `GuidKey`. The
  //! registry defers surface destruction from caller threads by moving removed
  //! entries into an engine-thread friendly pending-destructions list which the
  //! engine module drains and processes on the next frame. This avoids
  //! releasing surfaces and graphic.
  /*!
   - Thread-safety: a single mutex (`mutex_`) protects all public operations;
     callers do not need additional synchronization.
   - Ownership: surfaces are stored as `std::shared_ptr`; moving a stored
     `shared_ptr` into the pending-destructions list transfers ownership and
     defers the final release until the engine thread drains the queue.
   - Deferred destruction: `RemoveSurface` and `Clear` move entries to
     `pending_destructions_` rather than destroying them immediately on the
     caller thread.
   - Engine-thread processing: <see cref="DrainPendingDestructions"/> returns
     the queued surfaces and their optional callbacks so the engine thread can
     perform destruction and notify clients.
   - Resize callbacks: callers may register multiple callbacks per key via
     `RegisterResizeCallback`; the `EditorModule`, which runs on the engine
     thread, retrieves and clears them with `DrainResizeCallbacks` before
     invoking.
   - Keying and hashing: keys are 16-byte arrays (`GuidKey`) with a custom
     FNV-1a-like hasher for use in unordered maps.
   - Read access: `FindSurface` and `SnapshotSurfaces` provide safe,
     snapshot-style read access to live entries.
   - Complexity: map operations are average O(1); snapshotting is linear in the
     number of live entries.
  */
  class SurfaceRegistry {
  public:
    //! 16-byte key type used to identify registered surfaces.
    using GuidKey = std::array<std::uint8_t, 16>;

    //! The type of the callback invoked when a surface operation has been
    //! processed.
    using OnProcessed = std::function<void(bool)>;

    SurfaceRegistry() = default;
    ~SurfaceRegistry() = default;

    SurfaceRegistry(const SurfaceRegistry&) = delete;
    SurfaceRegistry& operator=(const SurfaceRegistry&) = delete;

    SurfaceRegistry(SurfaceRegistry&&) noexcept = delete;
    SurfaceRegistry& operator=(SurfaceRegistry&&) noexcept = delete;

    //! Queue a surface for registration during the next frame.
    /*!
     The surface is added to a pending-registrations list which the engine
     module will drain on the next frame-start. An optional callback may be
     provided which will be invoked (on the engine thread) when the
     registration has been processed.
    */
    //! Register (stage) a surface for commitment on the next engine frame.
    /*!
      This is the public-facing method used by caller threads (UI or others)
      to stage a surface registration. It behaves symmetrically to
      RemoveSurface(): the entry becomes visible only after the engine
      commits the pending registration during the frame processing phase.
      An optional callback is invoked on the engine thread once processed.
    */
    auto RegisterSurface(GuidKey key,
      std::shared_ptr<oxygen::graphics::Surface> surface,
      std::function<void(bool)> onProcessed = {}) -> void {
      if (!surface) {
        if (onProcessed) {
          onProcessed(false);
        }
        return;
      }

      std::scoped_lock lock(mutex_);
      PendingRegistration entry;
      entry.key = key;
      entry.surface = std::move(surface);
      entry.callback = std::move(onProcessed);
      pending_registrations_.emplace_back(std::move(entry));
    }

    //! Commit the given surface into the live entries map (engine-thread only)
    /*
      This method inserts or replaces the entry for the supplied key.
      It is intended to be called only from the engine thread while
      processing pending registrations; external callers should use
      the public RegisterSurface(...) overload to stage a registration.
    */
    auto CommitRegistration(GuidKey key,
      std::shared_ptr<oxygen::graphics::Surface> surface)
      -> void {
      if (!surface)
        return;
      std::scoped_lock lock(mutex_);
      entries_[key] = std::move(surface);
    }

    //! Mark the specified surface for destruction.
    /*!
     The surface is moved out of the live entries and into a
     pending-destructions list which the engine module will drain on the next
     frame-start. An optional callback may be provided which will be invoked (on
     the engine thread) when the destruction has been processed.
    */
    auto RemoveSurface(const GuidKey& key,
      std::function<void(bool)> onProcessed = {}) -> void {
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
      -> std::shared_ptr<oxygen::graphics::Surface> {
      std::scoped_lock lock(mutex_);
      auto iter = entries_.find(key);
      return iter != entries_.end()
        ? iter->second
        : std::shared_ptr<oxygen::graphics::Surface>();
    }

    auto SnapshotSurfaces() const -> std::vector<
      std::pair<GuidKey, std::shared_ptr<oxygen::graphics::Surface>>> {
      std::scoped_lock lock(mutex_);
      std::vector<std::pair<GuidKey, std::shared_ptr<oxygen::graphics::Surface>>>
        snapshot;
      snapshot.reserve(entries_.size());
      for (const auto& pair : entries_) {
        snapshot.emplace_back(pair.first, pair.second);
      }

      return snapshot;
    }

    //! Move all live entries into the pending destruction list so the engine
    //! module may process them on the next frame.
    /*!
     This avoids final releases on the caller thread, and most importantly
     immediate release of surfaces, or asociated resources, that are still being
     used by the GPU.
    */
    auto Clear() -> void {
      std::scoped_lock lock(mutex_);
      for (auto& pair : entries_) {
        PendingDestruction entry;
        entry.key = pair.first;
        entry.surface = std::move(pair.second);
        pending_destructions_.emplace_back(std::move(entry));
      }
      entries_.clear();
    }

    //! Drain any pending destructions. Called by the engine module on the
    //! engine thread to retrieve surfaces slated for destruction.
    auto DrainPendingDestructions() -> std::vector<
      std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>,
      std::function<void(bool)>>>> {
      std::scoped_lock lock(mutex_);
      std::vector<
        std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>,
        std::function<void(bool)>>>>
        result;
      result.reserve(pending_destructions_.size());
      for (auto& pd : pending_destructions_) {
        result.emplace_back(pd.key, std::make_pair(pd.surface, pd.callback));
      }
      pending_destructions_.clear();
      return result;
    }

    //! Drain any pending registrations. Called by the engine module on the
    //! engine thread to retrieve surfaces queued for registration.
    auto DrainPendingRegistrations() -> std::vector<
      std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>,
      std::function<void(bool)>>>> {
      std::scoped_lock lock(mutex_);
      std::vector<
        std::pair<GuidKey, std::pair<std::shared_ptr<oxygen::graphics::Surface>,
        std::function<void(bool)>>>>
        result;
      result.reserve(pending_registrations_.size());
      for (auto& pr : pending_registrations_) {
        result.emplace_back(pr.key, std::make_pair(pr.surface, pr.callback));
      }
      pending_registrations_.clear();
      return result;
    }

    //! Register a callback to be invoked when the requested surface has been
    //! processed for resize on the engine thread. Multiple callbacks are
    //! allowed; they will be invoked and cleared when the resize happens.
    auto RegisterResizeCallback(const GuidKey& key, std::function<void(bool)> cb)
      -> void {
      std::scoped_lock lock(mutex_);
      resize_callbacks_[key].emplace_back(std::move(cb));
    }

    //! Pop all registered resize callbacks for a given key (engine-thread only).
    auto DrainResizeCallbacks(const GuidKey& key)
      -> std::vector<std::function<void(bool)>> {
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
      auto operator()(const GuidKey& key) const noexcept -> std::size_t {
        std::size_t hash = 1469598103934665603ULL;
        for (auto byte : key) {
          hash ^= static_cast<std::size_t>(byte);
          hash *= 1099511628211ULL;
        }

        return hash;
      }
    };

    mutable std::mutex mutex_;
    std::unordered_map<GuidKey, std::shared_ptr<oxygen::graphics::Surface>,
      GuidHasher>
      entries_;

    struct PendingDestruction {
      GuidKey key;
      std::shared_ptr<oxygen::graphics::Surface> surface;
      std::function<void(bool)> callback;
    };

    struct PendingRegistration {
      GuidKey key;
      std::shared_ptr<oxygen::graphics::Surface> surface;
      std::function<void(bool)> callback;
    };

    std::vector<PendingDestruction> pending_destructions_;
    std::vector<PendingRegistration> pending_registrations_;
    std::unordered_map<GuidKey, std::vector<std::function<void(bool)>>,
      GuidHasher>
      resize_callbacks_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
