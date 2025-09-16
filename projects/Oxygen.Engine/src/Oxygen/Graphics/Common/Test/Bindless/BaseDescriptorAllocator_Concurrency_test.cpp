//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

/**
 * @file BaseDescriptorAllocator_Concurrency_test.cpp
 *
 * Unit tests for the BaseDescriptorAllocator class covering concurrency and
 * thread safety behaviors.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

#include "./BaseDescriptorAllocatorTest.h"
#include "./Mocks/MockDescriptorAllocator.h"
#include "./Mocks/MockDescriptorSegment.h"

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;

using oxygen::graphics::bindless::testing::BaseDescriptorAllocatorTest;
using oxygen::graphics::bindless::testing::MockDescriptorSegment;

using oxygen::kInvalidBindlessHeapIndex;
namespace b = oxygen::bindless;

class AllocatorSimpleConcurrencyTest : public BaseDescriptorAllocatorTest {
public:
  std::atomic<b::HeapIndex::UnderlyingType> next_index_ { 0 };
};

class AllocatorCombiningConcurrencyTest : public BaseDescriptorAllocatorTest {
public:
  // Setup the segment factory with base indices for different type/visibility
  // combinations
  std::map<std::pair<ResourceViewType, DescriptorVisibility>, b::HeapIndex>
    base_indices_ = { { { ResourceViewType::kTexture_SRV,
                          DescriptorVisibility::kShaderVisible },
                        b::HeapIndex { 1000 } },
      { { ResourceViewType::kTexture_UAV,
          DescriptorVisibility::kShaderVisible },
        b::HeapIndex { 2000 } },
      { { ResourceViewType::kRawBuffer_SRV,
          DescriptorVisibility::kShaderVisible },
        b::HeapIndex { 3000 } },
      { { ResourceViewType::kRawBuffer_UAV,
          DescriptorVisibility::kShaderVisible },
        b::HeapIndex { 4000 } },
      { { ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly },
        b::HeapIndex { 5000 } },
      { { ResourceViewType::kTexture_UAV, DescriptorVisibility::kCpuOnly },
        b::HeapIndex { 6000 } } };

  std::map<std::pair<ResourceViewType, DescriptorVisibility>,
    std::atomic<b::HeapIndex::UnderlyingType>>
    next_indices_;

protected:
  auto SetUp() -> void override
  {
    BaseDescriptorAllocatorTest::SetUp();

    // Track next index per type/visibility
    for (auto& [pair, baseIndex] : base_indices_) {
      next_indices_[pair].store(baseIndex.get());
    }
  }
};

NOLINT_TEST_F(
  AllocatorSimpleConcurrencyTest, ThreadSafetyWithConcurrentAllocRelease)
{
  // Tests that concurrent allocations and releases from multiple threads
  // do not cause race conditions or data corruption

  // Keep track of allocated indices to verify correctness
  constexpr b::Capacity capacity { 1000 };

  // Setup the segment factory to create mock segments
  allocator_->segment_factory_ = [this, capacity](const ResourceViewType type,
                                   const DescriptorVisibility vis) {
    auto segment = std::make_unique<testing::NiceMock<MockDescriptorSegment>>();

    // Setup allocate to return sequential indices
    ON_CALL(*segment, Allocate()).WillByDefault([this, capacity] {
      const auto idx = next_index_.fetch_add(1);
      return idx < capacity.get() ? b::HeapIndex { idx }
                                  : kInvalidBindlessHeapIndex;
    });

    // Setup release to always succeed
    ON_CALL(*segment, Release(::testing::_))
      .WillByDefault(testing::Return(true));

    // Setup other required methods
    ON_CALL(*segment, GetAvailableCount())
      .WillByDefault(testing::Return(b::Count { capacity.get() }));
    ON_CALL(*segment, GetViewType()).WillByDefault(testing::Return(type));
    ON_CALL(*segment, GetVisibility()).WillByDefault(testing::Return(vis));
    ON_CALL(*segment, GetBaseIndex())
      .WillByDefault(testing::Return(b::HeapIndex { 0 }));
    ON_CALL(*segment, GetCapacity()).WillByDefault(testing::Return(capacity));
    ON_CALL(*segment, GetAllocatedCount()).WillByDefault([this] {
      return b::Count { next_index_.load() };
    });

    return segment;
  };

  // Number of operations per thread
  constexpr size_t kOperationsPerThread = 100;

  // Number of threads
  constexpr size_t kNumThreads = 4;

  // Flag to start all threads at the same time
  std::atomic start_flag(false);

  // Threads will record successful operations here
  std::atomic<size_t> successful_allocations(0);
  std::atomic<size_t> successful_releases(0);

  // Vector to hold all generated handles across threads
  std::vector<std::vector<DescriptorHandle>> thread_handles(kNumThreads);

  // For collecting exception messages from threads
  std::vector<std::string> exception_messages;
  std::mutex exception_mutex;

  // Create threads that will perform allocations and releases
  std::vector<std::thread> threads;
  threads.reserve(kNumThreads);
  for (size_t t = 0; t < kNumThreads; ++t) {
    threads.emplace_back([&, t] {
      // Wait for the start signal
      while (!start_flag.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
      }

      // Reserve space for handles
      thread_handles[t].reserve(kOperationsPerThread);

      // Perform allocations
      for (size_t i = 0; i < kOperationsPerThread; ++i) {
        try {
          auto handle = allocator_->Allocate(ResourceViewType::kTexture_SRV,
            DescriptorVisibility::kShaderVisible);
          if (handle.IsValid()) {
            thread_handles[t].push_back(std::move(handle));
            successful_allocations.fetch_add(1, std::memory_order_relaxed);
          }
        } catch (const std::exception& e) {
          // Collect the exception message for later reporting
          std::lock_guard<std::mutex> lock(exception_mutex);
          exception_messages.emplace_back(
            "Thread " + std::to_string(t) + " caught exception: " + e.what());
        }

        // Occasionally release a handle we've allocated
        if (!thread_handles[t].empty() && (i % 3 == 0)) {
          const size_t index = i % thread_handles[t].size();
          if (thread_handles[t][index].IsValid()) {
            try {
              allocator_->Release(thread_handles[t][index]);
              successful_releases.fetch_add(1, std::memory_order_relaxed);
            } catch (const std::exception& e) {
              std::lock_guard<std::mutex> lock(exception_mutex);
              exception_messages.emplace_back("Thread " + std::to_string(t)
                + " release exception: " + e.what());
            }
          }
        }
      }
    });
  }

  // Start all threads
  start_flag.store(true, std::memory_order_relaxed);

  // Wait for all threads to finish
  for (auto& t : threads) {
    t.join();
  }

  // Report all collected exceptions as test failures
  for (const auto& msg : exception_messages) {
    ADD_FAILURE() << msg;
  }

  // Verify results
  const size_t total_allocations = successful_allocations.load();
  const size_t total_releases = successful_releases.load();
  size_t remaining_valid_handles = 0;

  // Count remaining valid handles
  for (const auto& handles : thread_handles) {
    for (const auto& h : handles) {
      if (h.IsValid()) {
        remaining_valid_handles++;
      }
    }
  }

  // Verify balance: allocations - releases = remaining valid handles
  EXPECT_EQ(total_allocations - total_releases, remaining_valid_handles);

  // Verify that at least some operations were successful
  EXPECT_GT(total_allocations, 0);
}

NOLINT_TEST_F(
  AllocatorCombiningConcurrencyTest, MultiThreadedDifferentTypeVisibility)
{
  // Tests parallel operations with different types and visibilities

  constexpr b::Capacity capacity { 500 };

  allocator_->segment_factory_ = [this, capacity](ResourceViewType type,
                                   DescriptorVisibility vis) {
    // Skip combinations not in baseIndices to match thread skip logic
    if (!base_indices_.contains({ type, vis })) {
      return std::unique_ptr<testing::NiceMock<MockDescriptorSegment>> {};
    }

    auto segment = std::make_unique<testing::NiceMock<MockDescriptorSegment>>();
    auto base_index = base_indices_[{ type, vis }];

    // Setup allocate to return sequential indices for this type/visibility
    ON_CALL(*segment, Allocate())
      .WillByDefault([this, type, vis, capacity, base_index] {
        const auto idx = next_indices_[{ type, vis }].fetch_add(1);
        return (idx - base_index.get()) < capacity.get()
          ? b::HeapIndex { idx }
          : kInvalidBindlessHeapIndex;
      });

    // Setup release to always succeed
    ON_CALL(*segment, Release(::testing::_))
      .WillByDefault(testing::Return(true));

    // Setup other required methods
    ON_CALL(*segment, GetAvailableCount())
      .WillByDefault(testing::Return(b::Count { capacity.get() }));
    ON_CALL(*segment, GetViewType()).WillByDefault(testing::Return(type));
    ON_CALL(*segment, GetVisibility()).WillByDefault(testing::Return(vis));
    ON_CALL(*segment, GetBaseIndex())
      .WillByDefault(testing::Return(base_index));
    ON_CALL(*segment, GetCapacity()).WillByDefault(testing::Return(capacity));
    ON_CALL(*segment, GetAllocatedCount())
      .WillByDefault([this, type, vis, base_index] {
        const auto count
          = next_indices_[{ type, vis }].load() - base_index.get();
        return b::Count { count };
      });

    return segment;
  };

  // Resource types to test with
  const std::vector types
    = { ResourceViewType::kTexture_SRV, ResourceViewType::kTexture_UAV,
        ResourceViewType::kRawBuffer_SRV, ResourceViewType::kRawBuffer_UAV };

  // Visibilities to test with
  const std::vector visibilities
    = { DescriptorVisibility::kShaderVisible, DescriptorVisibility::kCpuOnly };

  // For collecting exception messages from threads
  std::vector<std::string> exception_messages;
  std::mutex exception_mutex;

  // Start threads testing different combinations
  std::vector<std::thread> threads;
  for (auto type : types) {
    for (auto vis : visibilities) {
      threads.emplace_back(
        [type, vis, this, &exception_mutex, &exception_messages] {
          // Skip some combinations to avoid too many threads
          if ((type == ResourceViewType::kRawBuffer_SRV
                || type == ResourceViewType::kRawBuffer_UAV)
            && vis == DescriptorVisibility::kCpuOnly) {
            return;
          }

          std::vector<DescriptorHandle> handles;

          // Perform some allocations
          for (int i = 0; i < 20; ++i) {
            try {
              if (auto handle = allocator_->Allocate(type, vis);
                handle.IsValid()) {
                handles.push_back(std::move(handle));

                // Only check properties if handle is valid and not
                // kInvalidIndex
                auto expected_base_index = base_indices_[{ type, vis }];
                EXPECT_NE(handles.back().GetBindlessHandle(),
                  kInvalidBindlessHeapIndex);
                EXPECT_GE(
                  handles.back().GetBindlessHandle(), expected_base_index);
                EXPECT_LT(handles.back().GetBindlessHandle().get(),
                  expected_base_index.get() + 500);
                // Only check type/vis if valid
                EXPECT_TRUE(handles.back().IsValid());
                EXPECT_TRUE(handles.back().GetViewType() == type);
                EXPECT_TRUE(handles.back().GetVisibility() == vis);
              }
            } catch (const std::exception& e) {
              std::lock_guard<std::mutex> lock(exception_mutex);
              exception_messages.emplace_back("Thread for "
                + std::to_string(static_cast<int>(type)) + ","
                + std::to_string(static_cast<int>(vis))
                + " exception: " + e.what());
            }
          }

          // Release some handles
          for (size_t i = 0; i < handles.size(); i += 2) {
            try {
              allocator_->Release(handles[i]);
            } catch (const std::exception& e) {
              std::lock_guard<std::mutex> lock(exception_mutex);
              exception_messages.emplace_back(
                "Release exception: " + std::string(e.what()));
            }
          }
        });
    }
  }

  // Wait for all threads to finish
  for (auto& t : threads) {
    t.join();
  }

  // Report all collected exceptions as test failures
  for (const auto& msg : exception_messages) {
    ADD_FAILURE() << msg;
  }

  // Verify that each segment has the expected number of allocated descriptors
  for (const auto type : types) {
    for (const auto vis : visibilities) {
      // Skip some combinations to match the thread creation logic
      if ((type == ResourceViewType::kRawBuffer_SRV
            || type == ResourceViewType::kRawBuffer_UAV)
        && vis == DescriptorVisibility::kCpuOnly) {
        continue;
      }

      auto allocator_size = allocator_->GetAllocatedDescriptorsCount(type, vis);
      EXPECT_GE(allocator_size, b::Count { 0 });
      EXPECT_LE(
        allocator_size, b::Count { 20 }); // we allocated at most 20 per thread

      // Get remaining capacity
      auto remaining = allocator_->GetRemainingDescriptorsCount(type, vis);
      EXPECT_GT(remaining, b::Count { 0 });
    }
  }
}
