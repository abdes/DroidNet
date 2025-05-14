//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Graphics/Common/ViewCache.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::graphics::DefaultViewCache;
using oxygen::graphics::NativeObject;

namespace {

struct DummyResource {
    uint64_t id;
    explicit DummyResource(const uint64_t _id)
        : id(_id)
    {
    }
};

struct DummyKey {
    int value;
    auto operator==(const DummyKey& other) const -> bool { return value == other.value; }
};

} // namespace

template <>
struct std::hash<DummyKey> {
    auto operator()(const DummyKey& k) const noexcept -> std::size_t
    {
        return std::hash<int>()(k.value);
    }
}; // namespace std

namespace {

// -----------------------------------------------------------------------------
// Basic Store/Find/Remove API
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, StoresAndFindsViewForResource)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    const auto resource = std::make_shared<DummyResource>(1);
    constexpr DummyKey key { 42 };
    constexpr NativeObject obj(123, 456);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj));
    const auto found = cache.Find(*resource, key);

    ASSERT_TRUE(found.IsValid());
    EXPECT_EQ(found.AsInteger(), 123);
    EXPECT_EQ(found.OwnerTypeId(), 456);
}

NOLINT_TEST(DefaultViewCache, ReturnsInvalidForMissingView)
{
    const DefaultViewCache<DummyResource, DummyKey> cache;
    const DummyResource resource { 2 };
    constexpr DummyKey key { 99 };

    NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = cache.Find(resource, key));
    const auto found = cache.Find(resource, key);
    EXPECT_FALSE(found.IsValid());
}

NOLINT_TEST(DefaultViewCache, RemoveRemovesView)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(3);
    DummyKey key { 7 };
    NativeObject obj(1, 2);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj));
    NOLINT_EXPECT_NO_THROW(ASSERT_TRUE(cache.Remove(*resource, key)));
    auto found = cache.Find(*resource, key);
    EXPECT_FALSE(found.IsValid());
}

NOLINT_TEST(DefaultViewCache, RemoveNonexistentKey)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(300);
    DummyKey key { 1 }, missing_key { 2 };
    NativeObject obj(123, 1);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj));
    bool removed = false;
    NOLINT_EXPECT_NO_THROW(removed = cache.Remove(*resource, missing_key));
    EXPECT_FALSE(removed);
}

NOLINT_TEST(DefaultViewCache, RemoveOnEmptyCache)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    const DummyResource resource { 600 };
    constexpr DummyKey key { 1 };
    bool removed = false;
    NOLINT_EXPECT_NO_THROW(removed = cache.Remove(resource, key));
    EXPECT_FALSE(removed);
}

// -----------------------------------------------------------------------------
// Multiple Keys/Resources and Overwrite
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, MultipleKeysForSameResource)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(100);
    DummyKey key1 { 1 }, key2 { 2 }, key3 { 3 };
    NativeObject obj1(10, 1), obj2(20, 2), obj3(30, 3);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key1, obj1));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key2, obj2));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key3, obj3));

    EXPECT_EQ(cache.Find(*resource, key1).AsInteger(), 10);
    EXPECT_EQ(cache.Find(*resource, key2).AsInteger(), 20);
    EXPECT_EQ(cache.Find(*resource, key3).AsInteger(), 30);

    NOLINT_EXPECT_NO_THROW(cache.Remove(*resource, key2));
    EXPECT_FALSE(cache.Find(*resource, key2).IsValid());
    EXPECT_TRUE(cache.Find(*resource, key1).IsValid());
    EXPECT_TRUE(cache.Find(*resource, key3).IsValid());
}

NOLINT_TEST(DefaultViewCache, MultipleResourcesSameKey)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource1 = std::make_shared<DummyResource>(200);
    auto resource2 = std::make_shared<DummyResource>(201);
    DummyKey key { 99 };
    NativeObject obj1(111, 1), obj2(222, 2);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource1, key, obj1));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource2, key, obj2));

    EXPECT_EQ(cache.Find(*resource1, key).AsInteger(), 111);
    EXPECT_EQ(cache.Find(*resource2, key).AsInteger(), 222);

    NOLINT_EXPECT_NO_THROW(cache.Remove(*resource1, key));
    EXPECT_FALSE(cache.Find(*resource1, key).IsValid());
    EXPECT_TRUE(cache.Find(*resource2, key).IsValid());
}

NOLINT_TEST(DefaultViewCache, OverwritesViewForSameKey)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(10);
    DummyKey key { 100 };
    NativeObject obj1(111, 222);
    NativeObject obj2(333, 444);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj1));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj2));
    auto found = cache.Find(*resource, key);

    ASSERT_TRUE(found.IsValid());
    EXPECT_EQ(found.AsInteger(), 333);
    EXPECT_EQ(found.OwnerTypeId(), 444);
}

// -----------------------------------------------------------------------------
// RemoveAll, Clear, and Cache State
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, RemoveAllRemovesAllViewsForResource)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(4);
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, DummyKey { 1 }, NativeObject(1, 1)));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, DummyKey { 2 }, NativeObject(2, 2)));

    std::size_t removed { 0 };
    NOLINT_EXPECT_NO_THROW(removed = cache.RemoveAll(*resource));
    EXPECT_EQ(removed, 2);
    EXPECT_FALSE(cache.Find(*resource, DummyKey { 1 }).IsValid());
    EXPECT_FALSE(cache.Find(*resource, DummyKey { 2 }).IsValid());
}

NOLINT_TEST(DefaultViewCache, RemoveAllOnResourceWithNoViews)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    const DummyResource resource { 400 };
    std::size_t removed = 0;
    NOLINT_EXPECT_NO_THROW(removed = cache.RemoveAll(resource));
    EXPECT_EQ(removed, 0);
}

NOLINT_TEST(DefaultViewCache, FindAfterRemoveAll)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(500);
    DummyKey key1 { 1 }, key2 { 2 };
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key1, NativeObject(1, 1)));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key2, NativeObject(2, 2)));
    NOLINT_EXPECT_NO_THROW(cache.RemoveAll(*resource));
    EXPECT_FALSE(cache.Find(*resource, key1).IsValid());
    EXPECT_FALSE(cache.Find(*resource, key2).IsValid());
}

NOLINT_TEST(DefaultViewCache, ClearRemovesEverything)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource1 = std::make_shared<DummyResource>(5);
    auto resource2 = std::make_shared<DummyResource>(6);
    NOLINT_EXPECT_NO_THROW(cache.Store(resource1, DummyKey { 1 }, NativeObject(1, 1)));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource2, DummyKey { 2 }, NativeObject(2, 2)));

    NOLINT_EXPECT_NO_THROW(cache.Clear());
    EXPECT_FALSE(cache.Find(*resource1, DummyKey { 1 }).IsValid());
    EXPECT_FALSE(cache.Find(*resource2, DummyKey { 2 }).IsValid());
}

NOLINT_TEST(DefaultViewCache, ClearOnEmptyCache)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    NOLINT_EXPECT_NO_THROW(cache.Clear());
    // Should not throw or crash
}

// -----------------------------------------------------------------------------
// Store/Remove/Store Again Patterns
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, StoreRemoveStoreAgain)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(700);
    DummyKey key { 1 };
    NativeObject obj1(1, 1), obj2(2, 2);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj1));
    NOLINT_EXPECT_NO_THROW(cache.Remove(*resource, key));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj2));
    EXPECT_EQ(cache.Find(*resource, key).AsInteger(), 2);
}

NOLINT_TEST(DefaultViewCache, StoreRemoveAllStoreAgain)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(800);
    DummyKey key { 1 };
    NativeObject obj1(1, 1), obj2(2, 2);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj1));
    NOLINT_EXPECT_NO_THROW(cache.RemoveAll(*resource));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj2));
    EXPECT_EQ(cache.Find(*resource, key).AsInteger(), 2);
}

// -----------------------------------------------------------------------------
// Expiry, Purge, and Pointer Identity
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, ReturnsInvalidIfResourceIsDestroyed)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(20);
    DummyKey key { 200 };
    NativeObject obj(555, 666);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj));
    resource.reset();

    DummyResource dummy { 20 };
    NOLINT_EXPECT_NO_THROW([[maybe_unused]] auto _ = cache.Find(dummy, key));
    auto found = cache.Find(dummy, key);
    EXPECT_FALSE(found.IsValid());
}

NOLINT_TEST(DefaultViewCache, PurgeExpiredResourcesRemovesStaleEntries)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource1 = std::make_shared<DummyResource>(30);
    auto resource2 = std::make_shared<DummyResource>(31);
    NOLINT_EXPECT_NO_THROW(cache.Store(resource1, DummyKey { 1 }, NativeObject(1, 1)));
    NOLINT_EXPECT_NO_THROW(cache.Store(resource2, DummyKey { 2 }, NativeObject(2, 2)));

    // Expire resource2 only
    resource2.reset();

    NOLINT_EXPECT_NO_THROW(cache.PurgeExpiredResources());

    // resource1 should still be valid, resource2 should be purged
    EXPECT_TRUE(cache.Find(*resource1, DummyKey { 1 }).IsValid());
    DummyResource dummy2 { 31 };
    EXPECT_FALSE(cache.Find(dummy2, DummyKey { 2 }).IsValid());
}

NOLINT_TEST(DefaultViewCache, PurgeExpiredResourcesOnEmptyCache)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    NOLINT_EXPECT_NO_THROW(cache.PurgeExpiredResources());
    // Should not throw or crash
}

NOLINT_TEST(DefaultViewCache, FindWithDifferentResourceInstanceSameValue)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    const auto resource = std::make_shared<DummyResource>(900);
    constexpr DummyKey key { 1 };
    constexpr NativeObject obj(123, 1);

    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, obj));
    const DummyResource same_value_resource { 900 };
    // Should not find, as cache is pointer-based
    EXPECT_FALSE(cache.Find(same_value_resource, key).IsValid());
}

// -----------------------------------------------------------------------------
// Error/Edge Cases: Invalid/Null Resource or View
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, StoringInvalidViewDoesNotStore)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    auto resource = std::make_shared<DummyResource>(40);
    DummyKey key { 400 };
    NativeObject invalid_obj(0ULL, 0);

#if !defined(NDEBUG)
    EXPECT_DEATH_IF_SUPPORTED(cache.Store(resource, key, invalid_obj), "invalid view");
#else
    NOLINT_EXPECT_NO_THROW(cache.Store(resource, key, invalid_obj));
    auto found = cache.Find(*resource, key);
    EXPECT_FALSE(found.IsValid());
#endif
}

NOLINT_TEST(DefaultViewCache, StoringNullResourceDoesNotCrash)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    std::shared_ptr<DummyResource> null_resource;
    DummyKey key { 500 };
    NativeObject obj(123, 456);

#if !defined(NDEBUG)
    EXPECT_DEATH_IF_SUPPORTED(cache.Store(null_resource, key, obj), "null resource");
#else
    // In release, should not throw and should not add anything to the cache
    NOLINT_EXPECT_NO_THROW(cache.Store(null_resource, key, obj));
    DummyResource dummy { 500 };
    auto found = cache.Find(dummy, key);
    EXPECT_FALSE(found.IsValid());
#endif
}

// -----------------------------------------------------------------------------
// Multi-threaded Use
// -----------------------------------------------------------------------------

NOLINT_TEST(DefaultViewCache, ConcurrentStoreAndFind)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    constexpr int num_threads = 8;
    constexpr int num_keys = 100;
    auto resource = std::make_shared<DummyResource>(1000);

    std::atomic<int> found_count { 0 };

    auto store_and_find_task = [&cache, &resource, &found_count](const int tid) {
        for (int i = 0; i < num_keys; ++i) {
            DummyKey key { tid * num_keys + i };
            const NativeObject obj(static_cast<uint64_t>(tid * num_keys + i + 1), tid + 1);
            cache.Store(resource, key, obj);
            // Immediately try to find after storing
            if (auto found = cache.Find(*resource, key); found.IsValid()) {
                ++found_count;
            }
        }
    };

    std::vector<std::thread> threads;
    // Launch threads that do store and find interleaved
    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(store_and_find_task, t);
    }
    for (auto& th : threads)
        th.join();

    EXPECT_EQ(found_count.load(), num_threads * num_keys);
}

NOLINT_TEST(DefaultViewCache, ConcurrentStoreRemoveFind)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    constexpr int num_threads = 4;
    constexpr int num_keys = 50;
    auto resource = std::make_shared<DummyResource>(2000);

    // Store initial values (ensure all NativeObjects are valid)
    for (int i = 0; i < num_threads * num_keys; ++i) {
        DummyKey key { i };
        const NativeObject obj(static_cast<uint64_t>(i + 1), 1); // +1 to avoid 0 handles
        cache.Store(resource, key, obj);
    }

    std::atomic<int> removed_count { 0 };
    std::atomic<int> found_count { 0 };

    auto remove_task = [&cache, &resource, &removed_count](const int tid) {
        for (int i = 0; i < num_keys; ++i) {
            if (DummyKey key { tid * num_keys + i }; cache.Remove(*resource, key)) {
                ++removed_count;
            }
        }
    };

    auto find_task = [&cache, &resource, &found_count](const int tid) {
        for (int i = 0; i < num_keys; ++i) {
            DummyKey key { tid * num_keys + i };
            if (auto found = cache.Find(*resource, key); found.IsValid()) {
                ++found_count;
            }
        }
    };

    std::vector<std::thread> threads;
    // Launch remove and find threads concurrently
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(remove_task, t);
        threads.emplace_back(find_task, t);
    }
    for (auto& th : threads)
        th.join();

    // After all removals, none should be found
    int remaining = 0;
    for (int i = 0; i < num_threads * num_keys; ++i) {
        DummyKey key { i };
        if (cache.Find(*resource, key).IsValid()) {
            ++remaining;
        }
    }
    EXPECT_EQ(remaining, 0);
}

NOLINT_TEST(DefaultViewCache, ConcurrentContention)
{
    DefaultViewCache<DummyResource, DummyKey> cache;
    constexpr int num_threads = 8;
    constexpr int num_keys = 10; // Small number of keys to ensure contention
    const auto resource = std::make_shared<DummyResource>(3000);

    std::atomic<int> operations_completed { 0 };

    auto mixed_operations_task = [&](const int tid) {
        // Each thread will perform all operations on all keys
        for (int i = 0; i < num_keys; ++i) {
            DummyKey key { i }; // All threads operate on the same key range

            // Store operation
            const NativeObject obj(static_cast<uint64_t>(tid * 1000 + i + 1), tid + 1);
            cache.Store(resource, key, obj);

            // Find operation - may get own value or another thread's value
            if (auto found = cache.Find(*resource, key); found.IsValid()) {
                ++operations_completed;
            }

            // Sometimes remove the key
            if ((tid + i) % 3 == 0) {
                if (cache.Remove(*resource, key)) {
                    ++operations_completed;
                }
            }

            // Store again with different value
            const NativeObject obj2(static_cast<uint64_t>(tid * 2000 + i + 1), tid + 1);
            cache.Store(resource, key, obj2);
            ++operations_completed;
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back(mixed_operations_task, t);
    }

    for (auto& th : threads)
        th.join();

    // Verify the cache is in a consistent state
    // (not checking specific content since it's non-deterministic)
    int valid_count = 0;
    for (int i = 0; i < num_keys; ++i) {
        if (DummyKey key { i }; cache.Find(*resource, key).IsValid()) {
            ++valid_count;
        }
    }

    // Each key is either removed or has a valid value
    EXPECT_LE(valid_count, num_keys);

    // At least some operations should have completed successfully
    EXPECT_GT(operations_completed.load(), 0);
}

} // namespace
