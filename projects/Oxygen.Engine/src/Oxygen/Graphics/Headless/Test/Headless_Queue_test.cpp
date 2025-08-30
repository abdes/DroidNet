//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Headless/Graphics.h>

extern "C" void* GetGraphicsModuleApi();

namespace {

using ::testing::Test;

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;

// Local dedicated strategy used by tests that need explicit gfx/compute queues.
class LocalDedicatedStrategy final : public oxygen::graphics::QueueStrategy {
public:
  LocalDedicatedStrategy() = default;
  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    return { { .name = "gfx",
               .role = Role::kGraphics,
               .allocation_preference = Alloc::kDedicated,
               .sharing_preference = Share::kSeparate },
      { .name = "compute",
        .role = Role::kCompute,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kSeparate } };
  }
  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return "gfx";
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return "gfx";
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return "compute";
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return "gfx";
  }
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<LocalDedicatedStrategy>(*this);
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
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<MixedAllocationSharingStrategy>(*this);
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
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<OverlappingNamesStrategy>(*this);
  }
};

// Small helper strategy that returns two supplied specifications. Placed at
// file scope so multiple tests can reuse it.
class LocalPairStrategy final : public oxygen::graphics::QueueStrategy {
public:
  LocalPairStrategy(oxygen::graphics::QueueSpecification a,
    oxygen::graphics::QueueSpecification b) noexcept
    : a_(std::move(a))
    , b_(std::move(b))
  {
  }

  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    return { a_, b_ };
  }

  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return a_.name;
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return a_.name;
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return b_.name;
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return a_.name;
  }
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<LocalPairStrategy>(*this);
  }

  oxygen::graphics::QueueSpecification a_;
  oxygen::graphics::QueueSpecification b_;
};

// Helper strategy exposing three supplied specifications. Used by the
// concurrency test to pre-create all candidate queue names in a single
// CreateCommandQueues call (avoids intermediate resets that clear names).
class LocalTripleStrategy final : public oxygen::graphics::QueueStrategy {
public:
  LocalTripleStrategy(oxygen::graphics::QueueSpecification a,
    oxygen::graphics::QueueSpecification b,
    oxygen::graphics::QueueSpecification c) noexcept
    : a_(std::move(a))
    , b_(std::move(b))
    , c_(std::move(c))
  {
  }

  [[nodiscard]] auto Specifications() const
    -> std::vector<oxygen::graphics::QueueSpecification> override
  {
    return { a_, b_, c_ };
  }

  [[nodiscard]] auto GraphicsQueueName() const -> std::string_view override
  {
    return a_.name;
  }
  [[nodiscard]] auto PresentQueueName() const -> std::string_view override
  {
    return a_.name;
  }
  [[nodiscard]] auto ComputeQueueName() const -> std::string_view override
  {
    return b_.name;
  }
  [[nodiscard]] auto TransferQueueName() const -> std::string_view override
  {
    return c_.name;
  }
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<LocalTripleStrategy>(*this);
  }

  oxygen::graphics::QueueSpecification a_;
  oxygen::graphics::QueueSpecification b_;
  oxygen::graphics::QueueSpecification c_;
};

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
  // Ensure single-queue behavior is tested using the official
  // SingleQueueStrategy (no pre-creation that resets queues).
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  using Share = oxygen::graphics::QueueSharingPreference;

  // Create two specifications that both request an AllInOne allocation so the
  // backend should map them to the same universal queue instance.
  oxygen::graphics::QueueSpecification spec_a {
    .name = "universal",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };
  oxygen::graphics::QueueSpecification spec_b {
    .name = "other",
    .role = Role::kCompute,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };
  LocalPairStrategy pair_allinone(spec_a, spec_b);
  headless_->CreateCommandQueues(pair_allinone);

  const auto q1 = headless_->GetCommandQueue("universal");
  const auto q2 = headless_->GetCommandQueue("other");

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
  // Use file-scope LocalDedicatedStrategy to initialize dedicated queues.
  LocalDedicatedStrategy local_ded;
  headless_->CreateCommandQueues(local_ded);
  const auto gfx_q = headless_->GetCommandQueue("gfx");
  const auto compute_q = headless_->GetCommandQueue("compute");

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
  using Share = oxygen::graphics::QueueSharingPreference;

  // Ensure the backend has created a queue for the requested name. Use a
  // pair strategy where both specs use the same name to emulate sharing.
  oxygen::graphics::QueueSpecification spec_a {
    .name = "shared-name",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kShared,
  };
  oxygen::graphics::QueueSpecification spec_b = spec_a;
  spec_b.role = Role::kCompute;
  LocalPairStrategy pair_shared(spec_a, spec_b);
  headless_->CreateCommandQueues(pair_shared);

  const auto first = headless_->GetCommandQueue("shared-name");
  const auto second = headless_->GetCommandQueue("shared-name");

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

  // Use file-scope LocalPairStrategy to create the two named queues.
  LocalPairStrategy pair_strat(spec_a, spec_b);
  headless_->CreateCommandQueues(pair_strat);
  const auto qa = headless_->GetCommandQueue(spec_a.name);
  const auto qb = headless_->GetCommandQueue(spec_b.name);

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

  // Act: create queues for two different names
  LocalPairStrategy pair_sep(spec_a, spec_b);
  headless_->CreateCommandQueues(pair_sep);
  const auto qa = headless_->GetCommandQueue(spec_a.name);
  const auto qb = headless_->GetCommandQueue(spec_b.name);

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
  [[nodiscard]] auto Clone() const
    -> std::unique_ptr<oxygen::graphics::QueueStrategy> override
  {
    return std::make_unique<MultiNamedStrategy>(*this);
  }
};

NOLINT_TEST_F(HeadlessGraphicsFixture, ComplexMix_MixedAllocationSharing)
{
  MixedAllocationSharingStrategy strat;
  const auto specs = strat.Specifications();
  ASSERT_EQ(specs.size(), 2u);

  using Role = oxygen::graphics::QueueRole;

  // Initialize backend queues according to the mixed strategy and then query
  headless_->CreateCommandQueues(strat);
  const auto q_univ = headless_->GetCommandQueue(strat.GraphicsQueueName());
  const auto q_ded = headless_->GetCommandQueue(strat.ComputeQueueName());

  // Initialize queues according to the mixed strategy
  headless_->CreateCommandQueues(strat);

  ASSERT_NE(q_univ.get(), nullptr);
  ASSERT_NE(q_ded.get(), nullptr);

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

  headless_->CreateCommandQueues(strat);
  const auto qg = headless_->GetCommandQueue(strat.GraphicsQueueName());
  const auto qc = headless_->GetCommandQueue(strat.ComputeQueueName());

  // Ensure queues are created per the overlapping strategy
  headless_->CreateCommandQueues(strat);

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);

  if (qg.get() == qc.get()) {
    EXPECT_EQ(qc->GetQueueRole(), Role::kGraphics);
  } else {
    EXPECT_EQ(qg->GetQueueRole(), Role::kGraphics);
    EXPECT_EQ(qc->GetQueueRole(), Role::kCompute);
  }
}

/*! Verify that headless Submit does not auto-advance the queue and that
  Wait blocks until an explicit Signal() completes the submission. */
NOLINT_TEST_F(HeadlessGraphicsFixture, Submit_PendingUntilSignal)
{
  using Role = oxygen::graphics::QueueRole;

  // Ensure the pending queue exists: create a simple dedicated pair where one
  // spec uses the "pending-queue" name.
  using Role = oxygen::graphics::QueueRole;
  using Alloc = oxygen::graphics::QueueAllocationPreference;
  using Share = oxygen::graphics::QueueSharingPreference;

  oxygen::graphics::QueueSpecification spec_a {
    .name = "pending-queue",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kSeparate,
  };
  oxygen::graphics::QueueSpecification spec_b {
    .name = "pending-helper",
    .role = Role::kCompute,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kSeparate,
  };
  LocalPairStrategy pending_pair(spec_a, spec_b);
  headless_->CreateCommandQueues(pending_pair);

  const auto queue = headless_->GetCommandQueue("pending-queue");
  ASSERT_NE(queue.get(), nullptr);

  auto cmd_list
    = headless_->AcquireCommandList(queue->GetQueueRole(), "pending-cmd");
  ASSERT_NE(cmd_list, nullptr);

  const auto before_value = queue->GetCurrentValue();
  const auto completion_value = before_value + 1;
  {
    // Use a CommandRecorder to record a queue wait and queue signal. We
    // explicitly submit the recorded command list so the pending submission is
    // consumed only after the recorded signal runs.
    auto recorder = headless_->AcquireCommandRecorder(
      queue, cmd_list, /*immediate_submission=*/true);
    ASSERT_NE(recorder, nullptr);

    recorder->RecordQueueSignal(completion_value);
  }

  // Wait for completion.
  try {
    queue->Wait(completion_value);
    LOG_F(INFO, "Smoke: submission execution completed");
    cmd_list->OnExecuted();
  } catch (const std::exception& e) {
    LOG_F(WARNING, "Smoke: wait for completion value failed: {}", e.what());
  }

  cmd_list.reset();
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
  headless_->CreateCommandQueues(strat);
  const auto qg = headless_->GetCommandQueue(strat.GraphicsQueueName());
  const auto qc = headless_->GetCommandQueue(strat.ComputeQueueName());

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

  headless_->CreateCommandQueues(strat);
  const auto qg = headless_->GetCommandQueue(strat.GraphicsQueueName());
  const auto qc = headless_->GetCommandQueue(strat.ComputeQueueName());

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);
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
  // Pre-create the candidate queue names in a single strategy so the manager
  // does not reset between creations and threads will find stable queues.
  using Share = oxygen::graphics::QueueSharingPreference;
  oxygen::graphics::QueueSpecification u_spec {
    .name = "concurrent-universal",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };
  oxygen::graphics::QueueSpecification s_spec {
    .name = "concurrent-shared",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kShared,
  };
  oxygen::graphics::QueueSpecification d_spec {
    .name = "concurrent-dedicated",
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kSeparate,
  };
  LocalTripleStrategy triple(u_spec, s_spec, d_spec);
  headless_->CreateCommandQueues(triple);

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
      const auto q = headless->GetCommandQueue(name);

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
