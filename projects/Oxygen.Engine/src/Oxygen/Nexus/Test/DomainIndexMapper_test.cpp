//===----------------------------------------------------------------------===//
// Tests for DomainIndexMapper
//===----------------------------------------------------------------------===//

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Nexus/DomainIndexMapper.h>

using oxygen::graphics::DescriptorHandle;

namespace {

//! Fake DescriptorAllocator used to drive DomainIndexMapper tests.
class FakeAllocator : public oxygen::graphics::DescriptorAllocator {
public:
  FakeAllocator()
  {
    // provide small domain capacities via internal map
    bases_[{
      oxygen::graphics::ResourceViewType::kTexture_SRV,
      oxygen::graphics::DescriptorVisibility::kShaderVisible,
    }] = oxygen::bindless::HeapIndex { 10u };
  }

  auto SetBase(oxygen::graphics::ResourceViewType vt,
    oxygen::graphics::DescriptorVisibility vis,
    oxygen::bindless::HeapIndex base) -> void
  {
    bases_[{ vt, vis }] = base;
  }

  auto Allocate(oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility) -> DescriptorHandle override
  {
    return DescriptorHandle {};
  }

  auto Release(DescriptorHandle&) -> void override { }

  auto CopyDescriptor(const DescriptorHandle&, const DescriptorHandle&)
    -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(
    oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 5u };
  }

  [[nodiscard]] auto GetDomainBaseIndex(
    oxygen::graphics::ResourceViewType view_type,
    oxygen::graphics::DescriptorVisibility vis) const
    -> oxygen::bindless::HeapIndex override
  {
    auto it = bases_.find({ view_type, vis });
    if (it != bases_.end()) {
      return it->second;
    }
    return oxygen::bindless::HeapIndex { 0u };
  }

  [[nodiscard]] auto Reserve(oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility, oxygen::bindless::Count)
    -> std::optional<oxygen::bindless::HeapIndex> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto Contains(const DescriptorHandle&) const -> bool override
  {
    return false;
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 0u };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const DescriptorHandle& /*handle*/) const noexcept
    -> oxygen::bindless::ShaderVisibleIndex override
  {
    return oxygen::kInvalidShaderVisibleIndex;
  }

private:
  struct KeyHash {
    auto operator()(const std::pair<oxygen::graphics::ResourceViewType,
      oxygen::graphics::DescriptorVisibility>& k) const noexcept -> std::size_t
    {
      return (static_cast<std::size_t>(k.first) << 16)
        ^ static_cast<std::size_t>(k.second);
    }
  };
  std::unordered_map<std::pair<oxygen::graphics::ResourceViewType,
                       oxygen::graphics::DescriptorVisibility>,
    oxygen::bindless::HeapIndex, KeyHash>
    bases_;
};

//! Ensure that a known domain key maps to the expected absolute range and
//! that a sample index resolves back to the same domain key.
NOLINT_TEST(DomainIndexMapperTest,
  GetDomainRange_ValidKey_ReturnsCorrectRangeAndResolvesBack)
{
  // Arrange
  FakeAllocator alloc;
  // Local aliases to improve readability in the test scope.
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  oxygen::nexus::DomainIndexMapper mapper(alloc, { dk });

  // Act
  const auto range = mapper.GetDomainRange(dk);
  const auto res = mapper.ResolveDomain(b::HeapIndex { 12u });

  // Assert
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->start.get(), 10u);
  EXPECT_TRUE(res.has_value());
  EXPECT_EQ(res->view_type, dk.view_type);
  EXPECT_EQ(res->visibility, dk.visibility);
}

//! Verify ResolveDomain returns nullopt when the index falls outside any
//! configured domain range.
NOLINT_TEST(
  DomainIndexMapperTest, ResolveDomain_IndexOutsideAnyRange_ReturnsNullopt)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;
  using BindlessHeapIndex = b::HeapIndex;

  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  oxygen::nexus::DomainIndexMapper mapper(alloc, { dk });

  // Act
  // index well outside domain
  const auto res = mapper.ResolveDomain(b::HeapIndex { 1000u });

  // Assert
  EXPECT_FALSE(res.has_value());
}

//! Verify resolution at boundary indices across multiple adjacent domains.
NOLINT_TEST(DomainIndexMapperTest,
  ResolveDomain_MultipleDomains_ResolvesCorrectlyAtBoundaries)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  // create two domains with adjacent ranges
  constexpr DomainKey d0 {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  constexpr DomainKey d1 {
    ResourceViewType::kTexture_UAV,
    DescriptorVisibility::kShaderVisible,
  };

  // tweak fake allocator so d1 base is immediately after d0 (10+5)
  alloc = FakeAllocator();
  // insert second base at 15
  alloc.SetBase(d1.view_type, d1.visibility, b::HeapIndex { 15u });

  oxygen::nexus::DomainIndexMapper mapper(alloc, { d0, d1 });

  // Act
  // index at the boundary between d0 and d1
  const auto r0 = mapper.ResolveDomain(b::HeapIndex { 14u });
  const auto r1 = mapper.ResolveDomain(b::HeapIndex { 15u });

  // Assert
  ASSERT_TRUE(r0.has_value());
  ASSERT_TRUE(r1.has_value());
  EXPECT_EQ(r0->view_type, d0.view_type);
  EXPECT_EQ(r1->view_type, d1.view_type);
}

//! Verify that an empty domain list results in no valid ranges and all
//! resolution attempts return nullopt.
NOLINT_TEST(DomainIndexMapperTest, EmptyDomainList_NoRanges_AllResolutionsFail)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  namespace b = oxygen::bindless;

  // Empty domain list
  oxygen::nexus::DomainIndexMapper mapper(alloc, {});

  // Act & Assert
  const auto res_zero = mapper.ResolveDomain(b::HeapIndex { 0u });
  const auto res_mid = mapper.ResolveDomain(b::HeapIndex { 100u });
  const auto res_large = mapper.ResolveDomain(b::HeapIndex { 1000u });

  EXPECT_FALSE(res_zero.has_value());
  EXPECT_FALSE(res_mid.has_value());
  EXPECT_FALSE(res_large.has_value());
}

//! Verify that a domain with zero remaining capacity returns an empty range
//! and no indices resolve to that domain (since there are no valid indices).
NOLINT_TEST(
  DomainIndexMapperTest, GetDomainRange_ZeroCapacityDomain_ReturnsEmptyRange)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  // Override the fake allocator to return zero remaining capacity
  class ZeroCapacityAllocator : public FakeAllocator {
  public:
    [[nodiscard]] auto GetRemainingDescriptorsCount(
      oxygen::graphics::ResourceViewType,
      oxygen::graphics::DescriptorVisibility) const
      -> oxygen::bindless::Count override
    {
      return oxygen::bindless::Count { 0u };
    }
  };

  ZeroCapacityAllocator zero_alloc;
  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  oxygen::nexus::DomainIndexMapper mapper(zero_alloc, { dk });

  // Act
  const auto range = mapper.GetDomainRange(dk);
  const auto res_base
    = mapper.ResolveDomain(b::HeapIndex { 10u }); // base index
  const auto res_any = mapper.ResolveDomain(b::HeapIndex { 15u }); // any index

  // Assert
  ASSERT_TRUE(range.has_value());
  EXPECT_EQ(range->start.get(), 10u);
  EXPECT_EQ(range->capacity.get(), 0u); // zero capacity
  // With zero capacity, no indices should resolve to this domain
  EXPECT_FALSE(res_base.has_value());
  EXPECT_FALSE(res_any.has_value());
}

//! Verify resolution works correctly at exact start and end boundaries
//! of domain ranges.
NOLINT_TEST(
  DomainIndexMapperTest, ResolveDomain_ExactBoundaries_ResolvesCorrectly)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  oxygen::nexus::DomainIndexMapper mapper(alloc, { dk });

  // Act
  const auto range = mapper.GetDomainRange(dk);
  ASSERT_TRUE(range.has_value());

  const auto start_idx = range->start.get();
  // Only test boundaries if capacity > 0
  if (range->capacity.get() > 0) {
    const auto end_idx = start_idx + range->capacity.get() - 1u;

    const auto res_start = mapper.ResolveDomain(b::HeapIndex { start_idx });
    const auto res_end = mapper.ResolveDomain(b::HeapIndex { end_idx });
    const auto res_before_start
      = mapper.ResolveDomain(b::HeapIndex { start_idx - 1u });
    const auto res_after_end
      = mapper.ResolveDomain(b::HeapIndex { end_idx + 1u });

    // Assert
    ASSERT_TRUE(res_start.has_value());
    EXPECT_EQ(res_start->view_type, dk.view_type);

    ASSERT_TRUE(res_end.has_value());
    EXPECT_EQ(res_end->view_type, dk.view_type);

    EXPECT_FALSE(res_before_start.has_value());
    EXPECT_FALSE(res_after_end.has_value());
  } else {
    // For zero capacity, only the start index should resolve
    const auto res_start = mapper.ResolveDomain(b::HeapIndex { start_idx });
    ASSERT_TRUE(res_start.has_value());
    EXPECT_EQ(res_start->view_type, dk.view_type);
  }
}

//! Verify resolution behavior with multiple domains that have gaps between
//! them.
NOLINT_TEST(DomainIndexMapperTest,
  ResolveDomain_DomainsWithGaps_ResolvesCorrectlyAndFailsInGaps)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  constexpr DomainKey d0 {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  constexpr DomainKey d1 {
    ResourceViewType::kTexture_UAV,
    DescriptorVisibility::kShaderVisible,
  };

  // Set up domains with a gap: d0 at 10-14, d1 at 20-24 (gap at 15-19)
  alloc = FakeAllocator();
  alloc.SetBase(d1.view_type, d1.visibility, b::HeapIndex { 20u });

  oxygen::nexus::DomainIndexMapper mapper(alloc, { d0, d1 });

  // Act
  const auto res_d0 = mapper.ResolveDomain(b::HeapIndex { 12u });
  const auto res_gap = mapper.ResolveDomain(b::HeapIndex { 17u });
  const auto res_d1 = mapper.ResolveDomain(b::HeapIndex { 22u });

  // Assert
  ASSERT_TRUE(res_d0.has_value());
  EXPECT_EQ(res_d0->view_type, d0.view_type);

  EXPECT_FALSE(res_gap.has_value()); // gap should not resolve

  ASSERT_TRUE(res_d1.has_value());
  EXPECT_EQ(res_d1->view_type, d1.view_type);
}

//! Verify GetDomainRange returns nullopt for unknown domain keys.
NOLINT_TEST(DomainIndexMapperTest, GetDomainRange_UnknownDomain_ReturnsNullopt)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;

  constexpr DomainKey known_dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  constexpr DomainKey unknown_dk {
    ResourceViewType::kRawBuffer_UAV, // different type
    DescriptorVisibility::kShaderVisible,
  };

  oxygen::nexus::DomainIndexMapper mapper(alloc, { known_dk });

  // Act
  const auto known_range = mapper.GetDomainRange(known_dk);
  const auto unknown_range = mapper.GetDomainRange(unknown_dk);

  // Assert
  EXPECT_TRUE(known_range.has_value());
  EXPECT_FALSE(unknown_range.has_value());
}

//===----------------------------------------------------------------------===//
// Thread Safety Tests
//===----------------------------------------------------------------------===//

//! Test fixture for DomainIndexMapper thread safety scenarios.
class DomainIndexMapperThreadSafetyTest : public testing::Test {
protected:
  auto SetUp() -> void override { }
  auto TearDown() -> void override { }
};

//! Verify concurrent GetDomainRange operations are thread-safe and
//! return consistent results across multiple threads.
NOLINT_TEST_F(DomainIndexMapperThreadSafetyTest,
  GetDomainRange_ConcurrentAccess_ReturnsConsistentResults)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;

  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };

  oxygen::nexus::DomainIndexMapper mapper(alloc, { dk });

  constexpr int num_threads = 20;
  constexpr int queries_per_thread = 1000;

  std::vector<std::thread> threads;
  std::vector<std::vector<std::optional<oxygen::nexus::DomainRange>>> results(
    num_threads);

  // Act: Multiple threads concurrently querying the same domain
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&mapper, dk, t, &results, queries_per_thread]() {
      results[t].reserve(queries_per_thread);
      for (int i = 0; i < queries_per_thread; ++i) {
        results[t].push_back(mapper.GetDomainRange(dk));
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: All threads should get identical results
  const auto expected_result = mapper.GetDomainRange(dk);

  for (const auto& thread_results : results) {
    for (const auto& result : thread_results) {
      ASSERT_EQ(result.has_value(), expected_result.has_value());
      if (result.has_value() && expected_result.has_value()) {
        EXPECT_EQ(result->start.get(), expected_result->start.get());
        EXPECT_EQ(result->capacity.get(), expected_result->capacity.get());
      }
    }
  }
}

//! Verify concurrent ResolveDomain operations are thread-safe and
//! return consistent results for the same input indices.
NOLINT_TEST_F(DomainIndexMapperThreadSafetyTest,
  ResolveDomain_ConcurrentAccess_ReturnsConsistentResults)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  constexpr DomainKey dk {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };

  oxygen::nexus::DomainIndexMapper mapper(alloc, { dk });

  constexpr int num_threads = 15;
  constexpr int queries_per_thread = 800;
  const auto test_indices = std::vector<b::HeapIndex> {
    b::HeapIndex { 12u }, // valid index within domain
    b::HeapIndex { 5u }, // valid index within domain
    b::HeapIndex { 500u } // invalid index outside domain
  };

  std::vector<std::thread> threads;
  std::vector<std::vector<std::optional<DomainKey>>> results(num_threads);

  // Act: Multiple threads concurrently resolving various indices
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back(
      [&mapper, &test_indices, t, &results, queries_per_thread]() {
        results[t].reserve(queries_per_thread);
        for (int i = 0; i < queries_per_thread; ++i) {
          const auto idx = test_indices[i % test_indices.size()];
          results[t].push_back(mapper.ResolveDomain(idx));
        }
      });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: All threads should get identical results for each test index
  for (size_t idx_i = 0; idx_i < test_indices.size(); ++idx_i) {
    const auto expected_result = mapper.ResolveDomain(test_indices[idx_i]);

    for (const auto& thread_results : results) {
      for (size_t i = idx_i; i < thread_results.size();
        i += test_indices.size()) {
        const auto& result = thread_results[i];
        ASSERT_EQ(result.has_value(), expected_result.has_value());
        if (result.has_value() && expected_result.has_value()) {
          EXPECT_EQ(result->view_type, expected_result->view_type);
          EXPECT_EQ(result->visibility, expected_result->visibility);
        }
      }
    }
  }
}

//! Verify mixed concurrent operations (GetDomainRange and ResolveDomain)
//! maintain consistency and don't interfere with each other.
NOLINT_TEST_F(DomainIndexMapperThreadSafetyTest,
  MixedOperations_ConcurrentAccess_MaintainsConsistency)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  constexpr DomainKey d0 {
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible,
  };
  constexpr DomainKey d1 {
    ResourceViewType::kTexture_UAV,
    DescriptorVisibility::kShaderVisible,
  };

  // Configure allocator with multiple domains
  alloc.SetBase(d1.view_type, d1.visibility, b::HeapIndex { 20u });

  oxygen::nexus::DomainIndexMapper mapper(alloc, { d0, d1 });

  constexpr int num_range_threads = 8;
  constexpr int num_resolve_threads = 8;
  constexpr int operations_per_thread = 600;

  std::vector<std::thread> threads;
  std::atomic<bool> start_flag { false };
  std::atomic<uint32_t> total_successful_operations { 0 };

  // Range query threads
  for (int t = 0; t < num_range_threads; ++t) {
    threads.emplace_back(
      [&mapper, d0, d1, &start_flag, &total_successful_operations,
        operations_per_thread]() {
        while (!start_flag.load()) {
          std::this_thread::yield();
        }

        uint32_t local_success = 0;
        for (int i = 0; i < operations_per_thread; ++i) {
          const auto& key = (i % 2 == 0) ? d0 : d1;
          const auto result = mapper.GetDomainRange(key);
          if (result.has_value()) {
            local_success++;
          }
          std::this_thread::yield();
        }

        total_successful_operations.fetch_add(local_success);
      });
  }

  // Resolve query threads
  const auto test_indices = std::vector<b::HeapIndex> {
    b::HeapIndex { 12u }, // d0 range
    b::HeapIndex { 22u }, // d1 range
    b::HeapIndex { 500u } // no domain
  };

  for (int t = 0; t < num_resolve_threads; ++t) {
    threads.emplace_back(
      [&mapper, &test_indices, &start_flag, &total_successful_operations,
        operations_per_thread]() {
        while (!start_flag.load()) {
          std::this_thread::yield();
        }

        uint32_t local_success = 0;
        for (int i = 0; i < operations_per_thread; ++i) {
          const auto idx = test_indices[i % test_indices.size()];
          const auto result = mapper.ResolveDomain(idx);
          if (result.has_value()) {
            local_success++;
          }
          std::this_thread::yield();
        }

        total_successful_operations.fetch_add(local_success);
      });
  }

  // Start all threads simultaneously
  start_flag.store(true);

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: Verify operations completed successfully
  // Each range thread should succeed on all operations (2 domains)
  // Each resolve thread should succeed on 2/3 operations (2 valid indices)
  constexpr auto expected_range_successes
    = num_range_threads * operations_per_thread;
  constexpr auto expected_resolve_successes
    = num_resolve_threads * operations_per_thread * 2 / 3; // 2/3 success rate

  constexpr auto total_expected
    = expected_range_successes + expected_resolve_successes;
  EXPECT_GE(total_successful_operations.load(), total_expected);
}

//! Verify thread safety with multiple domains and high contention scenarios.
NOLINT_TEST_F(DomainIndexMapperThreadSafetyTest,
  MultipleDomains_HighContention_MaintainsThreadSafety)
{
  // Arrange
  FakeAllocator alloc;
  using DomainKey = oxygen::nexus::DomainKey;
  using ResourceViewType = oxygen::graphics::ResourceViewType;
  using DescriptorVisibility = oxygen::graphics::DescriptorVisibility;
  namespace b = oxygen::bindless;

  const std::vector<DomainKey> domains = {
    { ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kTexture_UAV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kRawBuffer_SRV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kRawBuffer_UAV, DescriptorVisibility::kShaderVisible }
  };

  // Configure allocator with multiple domains at different bases
  alloc.SetBase(
    domains[1].view_type, domains[1].visibility, b::HeapIndex { 30u });
  alloc.SetBase(
    domains[2].view_type, domains[2].visibility, b::HeapIndex { 60u });
  alloc.SetBase(
    domains[3].view_type, domains[3].visibility, b::HeapIndex { 90u });

  oxygen::nexus::DomainIndexMapper mapper(
    alloc, { domains[0], domains[1], domains[2], domains[3] });

  constexpr int num_threads = 25;
  constexpr int operations_per_thread = 400;

  std::vector<std::thread> threads;
  std::atomic<uint32_t> consistency_check_passed { 0 };

  // Act: High contention scenario with all operations mixed
  for (int t = 0; t < num_threads; ++t) {
    threads.emplace_back([&mapper, &domains, t, &consistency_check_passed,
                           operations_per_thread]() {
      bool all_consistent = true;

      for (int i = 0; i < operations_per_thread; ++i) {
        const auto& domain = domains[i % domains.size()];

        // Get domain range
        const auto range = mapper.GetDomainRange(domain);
        if (!range.has_value()) {
          all_consistent = false;
          break;
        }

        // Test resolve in the range
        const auto test_idx
          = b::HeapIndex { range->start.get() + (i % range->capacity.get()) };
        const auto resolved = mapper.ResolveDomain(test_idx);

        // Should resolve back to the same domain
        if (!resolved.has_value() || resolved->view_type != domain.view_type
          || resolved->visibility != domain.visibility) {
          all_consistent = false;
          break;
        }

        std::this_thread::yield();
      }

      if (all_consistent) {
        consistency_check_passed.fetch_add(1);
      }
    });
  }

  // Wait for all threads to complete
  for (auto& thread : threads) {
    thread.join();
  }

  // Assert: All threads should pass consistency checks
  EXPECT_EQ(consistency_check_passed.load(), num_threads);
}

} // namespace
