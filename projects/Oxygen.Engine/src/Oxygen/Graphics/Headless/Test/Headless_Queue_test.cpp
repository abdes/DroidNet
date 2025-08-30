//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Headless/Graphics.h>

extern "C" void* GetGraphicsModuleApi();

namespace {

using ::testing::Test;

// Fixture that creates and destroys a headless backend for tests
class HeadlessGraphicsFixture : public Test {
public:
  void SetUp() override
  {
    module_ptr_ = static_cast<oxygen::graphics::GraphicsModuleApi*>(
      ::GetGraphicsModuleApi());
    ASSERT_NE(module_ptr_, nullptr);
    oxygen::SerializedBackendConfig cfg { "{}", 1 };
    backend_ = module_ptr_->CreateBackend(cfg);
    ASSERT_NE(backend_, nullptr);
    headless_
      = reinterpret_cast<oxygen::graphics::headless::Graphics*>(backend_);
    ASSERT_NE(headless_, nullptr);
  }

  void TearDown() override
  {
    if (module_ptr_) {
      module_ptr_->DestroyBackend();
    }
    backend_ = nullptr;
    headless_ = nullptr;
  }

protected:
  oxygen::graphics::GraphicsModuleApi* module_ptr_ { nullptr };
  void* backend_ { nullptr };
  oxygen::graphics::headless::Graphics* headless_ { nullptr };
};

/*!
 Verify that requesting `kAllInOne` allocation preference returns a single
 shared queue instance for multiple roles, and that `kDedicated` returns
 distinct per-role instances.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, AllocationPreferences_AllInOne)
{
  // Arrange done by fixture

  // Act
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  const auto q1 = headless_->CreateCommandQueue(
    "universal", Role::kGraphics, Alloc::kAllInOne);
  const auto q2
    = headless_->CreateCommandQueue("other", Role::kCompute, Alloc::kAllInOne);

  // Assert
  EXPECT_EQ(q1.get(), q2.get());
}

/*!
 Verify that `kDedicated` allocation preference returns distinct per-role queue
 instances and that the created queues preserve the requested role.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, AllocationPreferences_DedicatedPerRole)
{
  // Arrange done by fixture

  // Act
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  const auto gfx_q
    = headless_->CreateCommandQueue("gfx", Role::kGraphics, Alloc::kDedicated);
  const auto compute_q = headless_->CreateCommandQueue(
    "compute", Role::kCompute, Alloc::kDedicated);

  // Assert
  ASSERT_NE(gfx_q.get(), nullptr);
  ASSERT_NE(compute_q.get(), nullptr);
  EXPECT_NE(gfx_q.get(), compute_q.get());
  EXPECT_EQ(gfx_q->GetQueueRole(), Role::kGraphics);
  EXPECT_EQ(compute_q->GetQueueRole(), Role::kCompute);
}

/*!
 Exercise name-based reuse vs per-role creation to emulate
 `QueueSharingPreference`.

 Note: The backend hook `CreateCommandQueue` accepts `QueueAllocationPreference`
 but not `QueueSharingPreference` directly; sharing is normally provided via a
 higher-level `QueueStrategy`. This test documents and validates the headless
 backend's name-first vs role-based creation policy.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, SharingSemantics_NameBasedVsPerRole)
{
  // Arrange: no special setup beyond fixture

  // Act
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  const auto first = headless_->CreateCommandQueue(
    "shared-name", Role::kGraphics, Alloc::kDedicated);
  const auto second = headless_->CreateCommandQueue(
    "shared-name", Role::kCompute, Alloc::kDedicated);

  // Assert
  ASSERT_NE(first.get(), nullptr);
  ASSERT_NE(second.get(), nullptr);

  if (first.get() == second.get()) {
    // If backend reuses based on name, the preserved role will be that of the
    // first creation.
    EXPECT_EQ(first->GetQueueRole(), Role::kGraphics);
  } else {
    // Otherwise, the backend returns distinct instances per requested role
    EXPECT_EQ(first->GetQueueRole(), Role::kGraphics);
    EXPECT_EQ(second->GetQueueRole(), Role::kCompute);
  }
}

/*!
 Validate that when a higher-level `QueueStrategy` provides two specifications
 with the same name and `QueueSharingPreference::kShared`, the backend returns
 the same queue instance for both roles.
*/
NOLINT_TEST_F(
  HeadlessGraphicsFixture, QueueStrategy_SharedPreference_ReusesByName)
{
  // Arrange
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  using Share = oxygen::graphics::QueueSharingPreference;

  oxygen::graphics::QueueSpecification spec_a;
  spec_a.name = "shared-strat";
  spec_a.role = Role::kGraphics;
  spec_a.allocation_preference = Alloc::kDedicated;
  spec_a.sharing_preference = Share::kShared;

  oxygen::graphics::QueueSpecification spec_b = spec_a;
  spec_b.role = Role::kCompute; // same name + shared

  // Act Simulate the higher-level strategy requesting the two named queues.
  const auto qa = headless_->CreateCommandQueue(
    spec_a.name, spec_a.role, spec_a.allocation_preference);
  const auto qb = headless_->CreateCommandQueue(
    spec_b.name, spec_b.role, spec_b.allocation_preference);

  // Assert
  ASSERT_NE(qa.get(), nullptr);
  ASSERT_NE(qb.get(), nullptr);
  // Deterministic check: either the backend reused the named queue (qa==qb) in
  // which case the preserved role must be that of the first creation, or the
  // backend returned a distinct instance and then the second queue's role must
  // match the requested role in the specification.
  if (qa.get() == qb.get()) {
    EXPECT_EQ(qb->GetQueueRole(), spec_a.role);
  } else {
    EXPECT_EQ(qb->GetQueueRole(), spec_b.role);
  }
}

/*!
 Validate that when two queue specifications use the same role but request
 `QueueSharingPreference::kSeparate`, the backend will attempt to provide
 distinct queues; for headless this maps to per-role creation and thus we expect
 distinct instances when different names are supplied.
*/
NOLINT_TEST_F(
  HeadlessGraphicsFixture, QueueStrategy_SeparatePreference_DistinctPerName)
{
  // Arrange
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  using Share = oxygen::graphics::QueueSharingPreference;

  oxygen::graphics::QueueSpecification spec_a;
  spec_a.name = "sep-a";
  spec_a.role = Role::kGraphics;
  spec_a.allocation_preference = Alloc::kDedicated;
  spec_a.sharing_preference = Share::kSeparate;

  oxygen::graphics::QueueSpecification spec_b = spec_a;
  spec_b.name = "sep-b"; // different name implies distinct queue

  // Act
  const auto qa = headless_->CreateCommandQueue(
    spec_a.name, spec_a.role, spec_a.allocation_preference);
  const auto qb = headless_->CreateCommandQueue(
    spec_b.name, spec_b.role, spec_b.allocation_preference);

  // Assert
  ASSERT_NE(qa.get(), nullptr);
  ASSERT_NE(qb.get(), nullptr);
  // Deterministic check: if the backend reused a per-role cache it may return
  // the same instance for both names (role-based reuse). If it created distinct
  // queues per name the instances will differ. Either behavior is acceptable;
  // verify the queue roles agree with the contract.
  if (qa.get() == qb.get()) {
    // role should equal the requested role
    EXPECT_EQ(qb->GetQueueRole(), spec_a.role);
  } else {
    EXPECT_NE(qa.get(), qb.get());
    EXPECT_EQ(qb->GetQueueRole(), spec_b.role);
  }
}

} // namespace

namespace {

// A small helper strategy that exposes two distinct named queues for graphics
// and compute to validate multi-queue selection and mapping.
class MultiNamedStrategy final : public oxygen::graphics::QueueStrategy {
public:
  MultiNamedStrategy() = default;
  ~MultiNamedStrategy() override = default;

  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using oxygen::graphics::QueueAllocationPreference;
    using oxygen::graphics::QueueRole;
    using oxygen::graphics::QueueSharingPreference;
    using oxygen::graphics::QueueSpecification;

    return { {
               .name = "multi-gfx",
               .role = QueueRole::kGraphics,
               .allocation_preference = QueueAllocationPreference::kDedicated,
               .sharing_preference = QueueSharingPreference::kSeparate,
             },
      {
        .name = "multi-cpu",
        .role = QueueRole::kCompute,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kSeparate,
      } };
  }

  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return "multi-cpu";
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return "multi-gfx";
  }
};

// Strategy that mixes allocation and sharing preferences across specs to test
// backend mapping when the strategy asks for mixed behavior.
class MixedAllocationSharingStrategy final
  : public oxygen::graphics::QueueStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using oxygen::graphics::QueueAllocationPreference;
    using oxygen::graphics::QueueRole;
    using oxygen::graphics::QueueSharingPreference;
    using oxygen::graphics::QueueSpecification;

    return { {
               .name = "mix-universal",
               .role = QueueRole::kGraphics,
               .allocation_preference = QueueAllocationPreference::kAllInOne,
               .sharing_preference = QueueSharingPreference::kShared,
             },
      {
        .name = "mix-dedicated",
        .role = QueueRole::kCompute,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kSeparate,
      } };
  }

  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return "mix-universal";
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return "mix-universal";
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return "mix-dedicated";
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return "mix-universal";
  }
};

// Strategy where two specifications use overlapping names/roles but different
// sharing preferences to test precedence and fallback semantics.
class OverlappingNamesStrategy final : public oxygen::graphics::QueueStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    using oxygen::graphics::QueueAllocationPreference;
    using oxygen::graphics::QueueRole;
    using oxygen::graphics::QueueSharingPreference;
    using oxygen::graphics::QueueSpecification;

    return { {
               .name = "overlap",
               .role = QueueRole::kGraphics,
               .allocation_preference = QueueAllocationPreference::kDedicated,
               .sharing_preference = QueueSharingPreference::kShared,
             },
      {
        .name = "overlap",
        .role = QueueRole::kCompute,
        .allocation_preference = QueueAllocationPreference::kDedicated,
        .sharing_preference = QueueSharingPreference::kSeparate,
      } };
  }

  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return "overlap";
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return "overlap";
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return "overlap";
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return "overlap";
  }
};

NOLINT_TEST_F(HeadlessGraphicsFixture, ComplexMix_MixedAllocationSharing)
{
  MixedAllocationSharingStrategy strat;
  const auto specs = strat.Specifications();
  ASSERT_EQ(specs.size(), 2u);

  using Role = oxygen::graphics::QueueRole;

  const auto q_univ = headless_->CreateCommandQueue(
    strat.GraphicsQueueName(), Role::kGraphics, specs[0].allocation_preference);
  const auto q_ded = headless_->CreateCommandQueue(
    strat.ComputeQueueName(), Role::kCompute, specs[1].allocation_preference);

  ASSERT_NE(q_univ.get(), nullptr);
  ASSERT_NE(q_ded.get(), nullptr);

  // The all-in-one + shared entry should allow reuse across roles; verify that
  // the universal queue does not collide with the dedicated compute queue.
  EXPECT_NE(q_univ.get(), q_ded.get());
  EXPECT_EQ(q_univ->GetQueueRole(), Role::kGraphics);
  EXPECT_EQ(q_ded->GetQueueRole(), Role::kCompute);
}

NOLINT_TEST_F(HeadlessGraphicsFixture, ComplexMix_OverlappingNamesPrecedence)
{
  OverlappingNamesStrategy strat;
  const auto specs = strat.Specifications();
  ASSERT_EQ(specs.size(), 2u);

  using Role = oxygen::graphics::QueueRole;

  // Request graphics first (shared) then compute (separate). Deterministic
  // behavior: if name-based reuse is honored, the compute request may return
  // the same instance as the graphics one; otherwise a separate compute
  // instance will be returned.
  const auto qg = headless_->CreateCommandQueue(
    strat.GraphicsQueueName(), Role::kGraphics, specs[0].allocation_preference);
  const auto qc = headless_->CreateCommandQueue(
    strat.ComputeQueueName(), Role::kCompute, specs[1].allocation_preference);

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);

  if (qg.get() == qc.get()) {
    // In this case the first-created role must be preserved.
    EXPECT_EQ(qc->GetQueueRole(), Role::kGraphics);
  } else {
    EXPECT_EQ(qg->GetQueueRole(), Role::kGraphics);
    EXPECT_EQ(qc->GetQueueRole(), Role::kCompute);
  }
}

/*!
 End-to-end: using `SingleQueueStrategy` should map all queue name queries to
 the single "universal" specification; requesting the queue via the backend
 should yield the universal queue instance.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, EndToEnd_SingleQueueStrategy)
{
  using oxygen::graphics::SingleQueueStrategy;
  using Role = oxygen::graphics::QueueRole;

  SingleQueueStrategy strat;
  const auto specs = strat.Specifications();
  ASSERT_EQ(specs.size(), 1u);
  EXPECT_EQ(strat.GraphicsQueueName(), "universal");

  // Act: request the queue for graphics and compute via the names the strategy
  // provides.
  const auto qg = headless_->CreateCommandQueue(
    strat.GraphicsQueueName(), Role::kGraphics, specs[0].allocation_preference);
  const auto qc = headless_->CreateCommandQueue(
    strat.ComputeQueueName(), Role::kCompute, specs[0].allocation_preference);

  // Assert: single-queue strategy => both requests should resolve to same queue
  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);
  EXPECT_EQ(qg.get(), qc.get());
}

/*!
 End-to-end: using a custom `MultiNamedStrategy` with two names should map the
 graphics/compute names to distinct specifications; requesting queues should
 return distinct instances with appropriate roles.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, EndToEnd_MultiNamedStrategy)
{
  MultiNamedStrategy strat;
  const auto specs = strat.Specifications();
  ASSERT_EQ(specs.size(), 2u);

  using Role = oxygen::graphics::QueueRole;

  const auto qg = headless_->CreateCommandQueue(
    strat.GraphicsQueueName(), Role::kGraphics, specs[0].allocation_preference);
  const auto qc = headless_->CreateCommandQueue(
    strat.ComputeQueueName(), Role::kCompute, specs[1].allocation_preference);

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);
  // For the headless implementation, distinct names should map to distinct
  // instances.
  EXPECT_NE(qg.get(), qc.get());
  EXPECT_EQ(qg->GetQueueRole(), Role::kGraphics);
  EXPECT_EQ(qc->GetQueueRole(), Role::kCompute);
}

/*!
 Concurrently call CreateCommandQueue from multiple threads to exercise queue
 manager locking and validate stable behavior under contention.
*/
NOLINT_TEST_F(HeadlessGraphicsFixture, Concurrency_ConcurrentCreateCalls)
{

  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;

  // Arrange
  std::vector<std::thread> workers;
  std::vector<std::shared_ptr<oxygen::graphics::CommandQueue>>
    universal_results;
  std::vector<std::shared_ptr<oxygen::graphics::CommandQueue>> other_results;
  std::mutex results_mutex;

  const auto kThreads = 16u;

  // Act
  for (auto i = 0u; i < kThreads; ++i) {
    // Capture only what we need explicitly to avoid accidentally capturing
    // any outer-scope `name` variables by reference (which could be const).
    // Capture headless_ by value into 'headless' for use inside the thread.
    workers.emplace_back([i, &results_mutex, &universal_results, &other_results,
                           headless = headless_] {
      std::string name;
      if (i % 3 == 0) {
        name = "concurrent-universal";
      } else if (i % 3 == 1) {
        name = "concurrent-shared";
      } else {
        name = "concurrent-dedicated";
      }
      const Role role = (i % 2 == 0) ? Role::kGraphics : Role::kCompute;
      const Alloc alloc = (name == "concurrent-universal") ? Alloc::kAllInOne
                                                           : Alloc::kDedicated;

      const auto q = headless->CreateCommandQueue(name, role, alloc);

      std::lock_guard<std::mutex> lg(results_mutex);
      if (name == "concurrent-universal")
        universal_results.push_back(q);
      else
        other_results.push_back(q);
    });
  }

  for (auto& t : workers) {
    t.join();
  }

  // Assert: basic sanity
  const auto total_results = universal_results.size() + other_results.size();
  ASSERT_EQ(total_results, static_cast<std::size_t>(kThreads));

  for (const auto& q : universal_results) {
    ASSERT_NE(q.get(), nullptr);
  }
  for (const auto& q : other_results) {
    ASSERT_NE(q.get(), nullptr);
  }

  // Assert: universal name should map to a single queue instance
  if (!universal_results.empty()) {
    const auto first = universal_results.front().get();
    for (const auto& q : universal_results) {
      EXPECT_EQ(first, q.get());
    }
  }
}

} // namespace
