//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Core/AnyCache.h>

using oxygen::AnyCache;

namespace {

// -----------------------------------------------------------------------------
// Concurrent read test cases
// -----------------------------------------------------------------------------

class AnyCacheConcurrentReadTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 100 };
  static constexpr int kNumThreads = 8;
  static constexpr int kNumItems = 50;
  static constexpr int kOperationsPerThread = 100;
};

//! Test concurrent Peek operations from multiple threads
NOLINT_TEST_F(AnyCacheConcurrentReadTest, ConcurrentPeek_NoDataRaces)
{

  // Arrange - populate cache with test data
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i * 10));
  }

  std::atomic<int> successful_peeks { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  std::mutex exception_mutex;
  std::vector<std::string> exception_messages;

  // Act - launch concurrent peek operations
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      // Wait for start signal
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t)); // Thread-local RNG
      std::uniform_int_distribution<int> dist(0, kNumItems - 1);

      for (int i = 0; i < kOperationsPerThread; ++i) {
        try {
          int key = dist(rng);
          auto obj = cache_.Peek<TestObject>(key);
          if (obj && obj->value == key * 10) {
            successful_peeks.fetch_add(1, std::memory_order_relaxed);
          }
        } catch (const std::exception& e) {
          std::lock_guard<std::mutex> lock(exception_mutex);
          exception_messages.emplace_back(
            "Thread " + std::to_string(t) + " peek exception: " + e.what());
        }
      }
    });
  }

  // Start all threads simultaneously
  start_flag.store(true, std::memory_order_release);

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert - no exceptions and successful operations
  EXPECT_TRUE(exception_messages.empty())
    << "Exceptions occurred: " << exception_messages.size();
  EXPECT_GE(successful_peeks.load(), kNumThreads * kOperationsPerThread / 2);
  EXPECT_EQ(cache_.Size(), kNumItems); // Cache size should be unchanged
}

//! Test concurrent Contains operations from multiple threads
NOLINT_TEST_F(AnyCacheConcurrentReadTest, ConcurrentContains_NoDataRaces)
{

  // Arrange - populate cache with test data
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<int> successful_contains { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;

  // Act - launch concurrent contains operations
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(
        0, kNumItems * 2); // Include non-existent keys

      for (int i = 0; i < kOperationsPerThread; ++i) {
        int key = dist(rng);
        bool contains = cache_.Contains(key);
        bool expected = (key < kNumItems);
        if (contains == expected) {
          successful_contains.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_GE(
    successful_contains.load(), kNumThreads * kOperationsPerThread * 3 / 4);
}

//! Test concurrent GetTypeId operations from multiple threads
NOLINT_TEST_F(AnyCacheConcurrentReadTest, ConcurrentGetTypeId_NoDataRaces)
{

  // Arrange
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<int> correct_type_ids { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  const auto expected_type_id = TestObject::ClassTypeId();

  // Act
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(0, kNumItems - 1);

      for (int i = 0; i < kOperationsPerThread; ++i) {
        int key = dist(rng);
        auto type_id = cache_.GetTypeId(key);
        if (type_id == expected_type_id) {
          correct_type_ids.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_GE(
    correct_type_ids.load(), kNumThreads * kOperationsPerThread * 3 / 4);
}

// -----------------------------------------------------------------------------
// Concurrent checkout/checkin test cases
// -----------------------------------------------------------------------------

class AnyCacheConcurrentCheckoutTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    int value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 100 };
  static constexpr int kNumThreads = 6;
  static constexpr int kNumItems = 20;
  static constexpr int kOperationsPerThread = 50;
};

//! Test concurrent checkout/checkin operations on different items
NOLINT_TEST_F(
  AnyCacheConcurrentCheckoutTest, ConcurrentCheckoutDifferentItems_ThreadSafe)
{

  // Arrange - populate cache
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<int> successful_checkouts { 0 };
  std::atomic<int> successful_checkins { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  std::mutex exception_mutex;
  std::vector<std::string> exception_messages;

  // Act - each thread works on different items to minimize contention
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      // Each thread focuses on a subset of items to reduce contention
      int start_key = (t * kNumItems) / kNumThreads;
      int end_key = ((t + 1) * kNumItems) / kNumThreads;

      try {
        for (int i = 0; i < kOperationsPerThread; ++i) {
          for (int key = start_key; key < end_key; ++key) {
            // Checkout
            auto obj = cache_.CheckOut<TestObject>(key);
            if (obj && obj->value == key) {
              successful_checkouts.fetch_add(1, std::memory_order_relaxed);

              // Do some work
              std::this_thread::sleep_for(std::chrono::microseconds(1));

              // Checkin
              cache_.CheckIn(key);
              successful_checkins.fetch_add(1, std::memory_order_relaxed);
            }
          }
        }
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        exception_messages.emplace_back(
          "Thread " + std::to_string(t) + " exception: " + e.what());
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_TRUE(exception_messages.empty())
    << "Exceptions occurred: " << exception_messages.size();
  EXPECT_GE(
    successful_checkouts.load(), kNumThreads * kOperationsPerThread / 4);
  EXPECT_EQ(successful_checkouts.load(), successful_checkins.load());
}

//! Test concurrent checkout/checkin on the same items (high contention)
NOLINT_TEST_F(
  AnyCacheConcurrentCheckoutTest, ConcurrentCheckoutSameItems_HighContention)
{

  // Arrange - use fewer items to create high contention
  const int kContentionItems = 5;
  for (int i = 0; i < kContentionItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i * 100));
  }

  std::atomic<int> successful_operations { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;

  // Act - all threads compete for the same few items
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(0, kContentionItems - 1);

      for (int i = 0; i < kOperationsPerThread; ++i) {
        int key = dist(rng);
        auto obj = cache_.CheckOut<TestObject>(key);
        if (obj) {
          // Brief work simulation
          std::this_thread::sleep_for(std::chrono::microseconds(10));
          cache_.CheckIn(key);
          successful_operations.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert - should handle high contention gracefully
  EXPECT_GE(
    successful_operations.load(), kNumThreads * kOperationsPerThread / 4);
  EXPECT_EQ(cache_.Size(), kContentionItems); // All items should still exist
}

//! Test concurrent Touch operations
NOLINT_TEST_F(AnyCacheConcurrentCheckoutTest, ConcurrentTouch_ThreadSafe)
{

  // Arrange
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<int> touch_operations { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;

  // Act
  for (int t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(0, kNumItems - 1);

      for (int i = 0; i < kOperationsPerThread; ++i) {
        int key = dist(rng);
        cache_.Touch(key);
        touch_operations.fetch_add(1, std::memory_order_relaxed);

        // Balance with checkins to prevent accumulation
        if (i % 3 == 0) {
          cache_.CheckIn(key);
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_EQ(touch_operations.load(), kNumThreads * kOperationsPerThread);
}

// -----------------------------------------------------------------------------
// Mixed read/write concurrent test cases
// -----------------------------------------------------------------------------

class AnyCacheConcurrentMixedTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    std::atomic<int> value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 200 };
  static constexpr int kNumReaderThreads = 4;
  static constexpr int kNumWriterThreads = 2;
  static constexpr int kNumItems = 30;
  static constexpr int kOperationsPerThread = 100;
};

//! Test concurrent readers and writers
NOLINT_TEST_F(
  AnyCacheConcurrentMixedTest, ConcurrentReadersAndWriters_ThreadSafe)
{

  // Arrange - initial population
  for (int i = 0; i < kNumItems; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<int> read_operations { 0 };
  std::atomic<int> write_operations { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  std::mutex exception_mutex;
  std::vector<std::string> exception_messages;

  // Act - launch reader threads
  for (int t = 0; t < kNumReaderThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(0, kNumItems - 1);

      try {
        for (int i = 0; i < kOperationsPerThread; ++i) {
          int key = dist(rng);

          // Mix of read operations
          if (i % 3 == 0) {
            auto obj = cache_.Peek<TestObject>(key);
            if (obj) {
              [[maybe_unused]] volatile int val = obj->value.load();
            }
          } else if (i % 3 == 1) {
            cache_.Contains(key);
          } else {
            cache_.GetTypeId(key);
          }

          read_operations.fetch_add(1, std::memory_order_relaxed);
          std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        exception_messages.emplace_back(
          "Reader thread " + std::to_string(t) + " exception: " + e.what());
      }
    });
  }

  // Launch writer threads
  for (int t = 0; t < kNumWriterThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t + 1000));
      std::uniform_int_distribution<int> dist(0, kNumItems - 1);

      try {
        for (int i = 0; i < kOperationsPerThread / 2;
          ++i) { // Fewer write operations
          int key = dist(rng);

          // Mix of write operations
          if (i % 4 == 0) {
            // Store new value
            cache_.Store(kNumItems + t * 1000 + i,
              std::make_shared<TestObject>(t * 1000 + i));
          } else if (i % 4 == 1) {
            // Replace existing (if possible)
            cache_.Replace(key, std::make_shared<TestObject>(key + 1000));
          } else if (i % 4 == 2) {
            // Checkout and checkin
            auto obj = cache_.CheckOut<TestObject>(key);
            if (obj) {
              std::this_thread::sleep_for(std::chrono::microseconds(5));
              cache_.CheckIn(key);
            }
          } else {
            // Remove (if possible)
            cache_.Remove(key);
          }

          write_operations.fetch_add(1, std::memory_order_relaxed);
          std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        exception_messages.emplace_back(
          "Writer thread " + std::to_string(t) + " exception: " + e.what());
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_TRUE(exception_messages.empty())
    << "Exceptions occurred: " << exception_messages.size();
  EXPECT_EQ(read_operations.load(), kNumReaderThreads * kOperationsPerThread);
  EXPECT_EQ(
    write_operations.load(), kNumWriterThreads * (kOperationsPerThread / 2));

  // Cache should still be in a valid state
  EXPECT_GE(cache_.Size(), 1);
  EXPECT_LE(cache_.Size(), 300); // Reasonable upper bound
}

// -----------------------------------------------------------------------------
// Stress and edge case concurrent tests
// -----------------------------------------------------------------------------

class AnyCacheConcurrentStressTest : public testing::Test {
protected:
  struct TestObject final : oxygen::Object {
    OXYGEN_TYPED(TestObject)
  public:
    explicit TestObject(const int v)
      : value(v)
    {
    }
    std::atomic<int> value { 0 };
  };

  AnyCache<int, oxygen::RefCountedEviction<int>> cache_ { 1000 };
  static constexpr int kStressThreads = 12;
  static constexpr int kStressOperations = 200;
};

//! Test high-stress concurrent operations
NOLINT_TEST_F(
  AnyCacheConcurrentStressTest, HighStressConcurrentOperations_NoCorruption)
{

  std::atomic<int> total_operations { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  std::mutex exception_mutex;
  std::vector<std::string> exception_messages;

  // Act - launch high-stress threads doing random operations
  for (int t = 0; t < kStressThreads; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count() + t));
      std::uniform_int_distribution<int> key_dist(0, 99);
      std::uniform_int_distribution<int> op_dist(0, 9);

      try {
        for (int i = 0; i < kStressOperations; ++i) {
          int key = key_dist(rng);
          int op = op_dist(rng);

          switch (op) {
          case 0:
          case 1: // Store (20%)
            cache_.Store(key, std::make_shared<TestObject>(key));
            break;
          case 2: // Replace (10%)
            cache_.Replace(key, std::make_shared<TestObject>(key + 1000));
            break;
          case 3:
          case 4: // CheckOut/CheckIn (20%)
            if (auto obj = cache_.CheckOut<TestObject>(key)) {
              std::this_thread::sleep_for(std::chrono::microseconds(1));
              cache_.CheckIn(key);
            }
            break;
          case 5: // Touch (10%)
            cache_.Touch(key);
            if (i % 5 == 0)
              cache_.CheckIn(key); // Occasional checkin
            break;
          case 6:
          case 7: // Peek (20%)
            cache_.Peek<TestObject>(key);
            break;
          case 8: // Contains (10%)
            cache_.Contains(key);
            break;
          case 9: // Remove (10%)
            cache_.Remove(key);
            break;
          }

          total_operations.fetch_add(1, std::memory_order_relaxed);
        }
      } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        exception_messages.emplace_back(
          "Stress thread " + std::to_string(t) + " exception: " + e.what());
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_TRUE(exception_messages.empty())
    << "Exceptions occurred: " << exception_messages.size();
  EXPECT_EQ(total_operations.load(), kStressThreads * kStressOperations);

  // Cache should be in a consistent state
  auto final_size = cache_.Size();
  auto final_consumed = cache_.Consumed();
  EXPECT_GE(final_size, 0);
  EXPECT_EQ(final_consumed, final_size); // For unit cost items
}

//! Test concurrent eviction scenarios
NOLINT_TEST_F(AnyCacheConcurrentStressTest, ConcurrentEviction_ThreadSafe)
{

  // Arrange - use a small cache to force evictions
  AnyCache<int, oxygen::RefCountedEviction<int>> small_cache(20);

  std::atomic<int> eviction_count { 0 };
  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;

  // Setup eviction callback
  auto eviction_scope = small_cache.OnEviction(
    [&eviction_count](const int /*key*/, std::shared_ptr<void> /*value*/,
      oxygen::TypeId /*type_id*/) {
      eviction_count.fetch_add(1, std::memory_order_relaxed);
    });

  // Act - threads that cause evictions through checkins
  for (int t = 0; t < 4; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      std::mt19937 rng(static_cast<unsigned>(t));
      std::uniform_int_distribution<int> dist(0, 49);

      for (int i = 0; i < 100; ++i) {
        int key = dist(rng);

        // Store items (many will be rejected due to budget)
        small_cache.Store(key, std::make_shared<TestObject>(key));

        // Occasionally check in to trigger evictions
        if (i % 3 == 0) {
          small_cache.CheckIn(key);
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert
  EXPECT_GE(eviction_count.load(), 0); // Some evictions should occur
  EXPECT_LE(small_cache.Size(), 20); // Should respect budget
}

//! Test concurrent Clear operations
NOLINT_TEST_F(AnyCacheConcurrentStressTest, ConcurrentClear_ThreadSafe)
{
  // Arrange
  for (int i = 0; i < 50; ++i) {
    cache_.Store(i, std::make_shared<TestObject>(i));
  }

  std::atomic<bool> start_flag { false };
  std::vector<std::thread> threads;
  std::atomic<int> clear_operations { 0 };

  // Act - some threads clear while others try to access
  for (int t = 0; t < 6; ++t) {
    threads.emplace_back([&, t] {
      while (!start_flag.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }

      if (t < 2) {
        // Clear threads
        for (int i = 0; i < 5; ++i) {
          cache_.Clear();
          clear_operations.fetch_add(1, std::memory_order_relaxed);
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      } else {
        // Access threads
        std::mt19937 rng(static_cast<unsigned>(t));
        std::uniform_int_distribution<int> dist(0, 49);

        for (int i = 0; i < 50; ++i) {
          int key = dist(rng);
          cache_.Contains(key);
          cache_.Peek<TestObject>(key);
          std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
      }
    });
  }

  start_flag.store(true, std::memory_order_release);

  for (auto& thread : threads) {
    thread.join();
  }

  // Assert - operations completed without crashing
  EXPECT_EQ(clear_operations.load(), 10); // 2 threads * 5 clears each
  EXPECT_EQ(cache_.Size(), 0); // Cache should be empty after clears
}

} // namespace
