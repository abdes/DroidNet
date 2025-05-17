//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Detail/ViewCache.h>

namespace oxygen::graphics::detail {

//! Standard implementation of view caching using an unordered_map.
/*!
 Provides full view caching capabilities, storing views in memory for later
 retrieval.

 It is important to remove the views and the resources from the cache when they
 are being destroyed. This is done by calling the `Remove` or `RemoveAll`
 methods. The cache does not keep strong references to resources, but enforces
 that the resources are not stale when a lookup is performed. It does not do any
 lifetime management for the views NativeObject handles, apart from checking
 they are valid when added to the cache.
*/
template <typename Resource, typename ViewKey>
    requires std::equality_comparable<ViewKey> && requires(ViewKey bk) {
        { std::hash<ViewKey> {}(bk) } -> std::convertible_to<std::size_t>;
    }
class DefaultViewCache : public ViewCache<Resource, ViewKey> {
public:
    DefaultViewCache() = default;

    ~DefaultViewCache() override
    {
        std::lock_guard lock(mutex_);
        if (!cache_.empty()) {
            LOG_F(WARNING, "DefaultViewCache destroyed with {} entries still in the cache!", cache_.size());
            CheckExpiredResourcesNoLock();
            cache_.clear();
        }
    }

    OXYGEN_MAKE_NON_COPYABLE(DefaultViewCache)
    OXYGEN_DEFAULT_MOVABLE(DefaultViewCache)

    void Store(
        const std::shared_ptr<const Resource>& resource,
        const ViewKey& key,
        const NativeObject view) noexcept override
    {
        std::lock_guard lock(mutex_);
        try {
            DCHECK_NOTNULL_F(resource, "Illegal attempt to store view with null resource");
            DCHECK_F(view.IsValid(), "Illegal attempt to store an invalid view");

            if (resource.get() == nullptr || !view.IsValid()) {
                DLOG_F(WARNING, "Attempt to store view with null resource or invalid view");
                return;
            }

            auto& entry = cache_[resource.get()];
            entry.resource_ref = resource;
            entry.views.insert_or_assign(key, view);
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception in Store: {}", ex.what());
        } catch (...) {
            LOG_F(ERROR, "Unknown exception in Store");
        }
    }

    [[nodiscard]] auto Find(
        const Resource& resource,
        const ViewKey& key) const noexcept -> NativeObject override
    {
        std::lock_guard lock(mutex_);
        try {
            auto resource_it = cache_.find(&resource);
            if (resource_it == cache_.end()) {
                return { 0ULL, kInvalidTypeId };
            }

            if (resource_it->second.resource_ref.expired()) {
                DLOG_F(FATAL, "Stale resource in cache used for view lookup");
                LOG_F(WARNING, "Stale resource at {}", fmt::ptr(resource_it->first));
                cache_.erase(resource_it);
                return { 0ULL, kInvalidTypeId };
            }

            if (auto view_it = resource_it->second.views.find(key);
                view_it != resource_it->second.views.end()) {
                return view_it->second;
            }

            return { 0ULL, kInvalidTypeId };
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception in Find: {}", ex.what());
            return { 0ULL, kInvalidTypeId };
        } catch (...) {
            LOG_F(ERROR, "Unknown exception in Find");
            return { 0ULL, kInvalidTypeId };
        }
    }

    auto Remove(
        const Resource& resource,
        const ViewKey& key) noexcept -> bool override
    {
        std::lock_guard lock(mutex_);
        try {
            auto resource_it = cache_.find(&resource);
            if (resource_it == cache_.end()) {
                return false;
            }

            auto& entry = resource_it->second;
            const bool removed = entry.views.erase(key) > 0;

            if (entry.views.empty()) {
                cache_.erase(resource_it);
            }

            return removed;
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception in Remove: {}", ex.what());
            return false;
        } catch (...) {
            LOG_F(ERROR, "Unknown exception in Remove");
            return false;
        }
    }

    auto RemoveAll(
        const Resource& resource) noexcept -> std::size_t override
    {
        std::lock_guard lock(mutex_);
        try {
            auto resource_it = cache_.find(&resource);
            if (resource_it == cache_.end()) {
                return 0;
            }

            auto view_count = resource_it->second.views.size();
            cache_.erase(resource_it);

            return view_count;
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception in RemoveAll: {}", ex.what());
            return 0;
        } catch (...) {
            LOG_F(ERROR, "Unknown exception in RemoveAll");
            return 0;
        }
    }

    void Clear() noexcept override
    {
        std::lock_guard lock(mutex_);
        CheckExpiredResourcesNoLock();
        cache_.clear();
    }

    //! Purges expired resources from the cache.
    //! This should be called periodically or when cache cleanup is desired.
    void PurgeExpiredResources() noexcept
    {
        LOG_SCOPE_FUNCTION(INFO);

        std::lock_guard lock(mutex_);
        try {
            std::size_t expired_count = 0;
            std::erase_if(cache_, [&](const auto& item) {
                if (item.second.resource_ref.expired()) {
                    ++expired_count;
                    DLOG_F(1, "Expired resource at {}", fmt::ptr(item.first));
                    return true;
                }
                return false;
            });
            if (expired_count > 0) {
                DLOG_F(INFO, "Purged {} expired resource(s)", expired_count);
            } else {
                DLOG_F(INFO, "No expired resources found");
            }
        } catch (const std::exception& ex) {
            LOG_F(ERROR, "Exception in PurgeExpiredResources: {}", ex.what());
        } catch (...) {
            LOG_F(ERROR, "Unknown exception in PurgeExpiredResources");
        }
    }

private:
    void CheckExpiredResourcesNoLock() const noexcept
    {
        // No locking here. This method is only called for debugging purposes
        // within the scope of an existing lock.
        try {
            const std::size_t expired_count = std::count_if(
                cache_.cbegin(), cache_.cend(), [&](const auto& item) {
                    if (item.second.resource_ref.expired()) {
                        DLOG_F(1, "Expired resource at {}", fmt::ptr(item.first));
                        return true;
                    }
                    return false;
                });
            if (expired_count > 0) {
                DLOG_F(INFO, "Cache has {} expired resource(s)", expired_count);
            } else {
                DLOG_F(1, "No expired resources found");
            }
        } catch (...) {
            LOG_F(ERROR, "Unknown exception while looking for stale resources");
        }
    }

    struct ResourceEntry {
        std::weak_ptr<const Resource> resource_ref; // Weak reference to the resource
        std::unordered_map<ViewKey, NativeObject> views;
    };

    mutable std::unordered_map<const Resource*, ResourceEntry> cache_;
    mutable std::mutex mutex_;
};

} // namespace oxygen::graphics::detail
