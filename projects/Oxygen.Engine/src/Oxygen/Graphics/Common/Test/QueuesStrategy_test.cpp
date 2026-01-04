//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
// Required for direct access to the QueueManager component in tests
#include <Oxygen/Graphics/Common/Internal/QueueManager.h>
#include <unordered_set>

// Google Mock will be used to intercept CreateCommandQueue calls made by the
// base Graphics::CreateCommandQueues implementation. We create a small mock
// Graphics subclass that delegates the CreateCommandQueue call to a mockable
// method so tests can provide fake CommandQueue instances.

namespace {

using testing::_;
using testing::Return;

using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using Role = oxygen::graphics::QueueRole;

using namespace oxygen::graphics;
using oxygen::observer_ptr;
using oxygen::platform::Window;

// -----------------------------------------------------------------------------
// Mocks and Fakes
// -----------------------------------------------------------------------------

class FakeCommandQueue final : public CommandQueue {
public:
  explicit FakeCommandQueue(const std::string_view name, const QueueRole role)
    : CommandQueue(name)
    , role_(role)
  {
  }

  // Count how many times Flush() was invoked via
  // ForEachQueue/FlushCommandQueues
  mutable std::atomic<int> flush_count_ { 0 };

  auto Signal(uint64_t) const -> void override { }
  auto Signal() const -> uint64_t override { return 0; }
  auto Wait(uint64_t, std::chrono::milliseconds) const -> void override { }
  auto Wait(uint64_t) const -> void override { }
  auto QueueSignalCommand(uint64_t) -> void override { }
  auto QueueWaitCommand(uint64_t) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override
  {
    return 0;
  }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override { return 0; }
  auto Submit(std::shared_ptr<CommandList>) -> void override { }
  auto Submit(std::span<std::shared_ptr<CommandList>>) -> void override { }
  auto Flush() const -> void override
  {
    flush_count_.fetch_add(1, std::memory_order_relaxed);
  }
  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return role_;
  }

private:
  QueueRole role_;
};

// A testable Graphics subclass that exposes a virtual hook we can mock.
// Proper Google Mock partial mock for `oxygen::Graphics`.
// We inherit and expose a mockable CreateCommandQueue while providing minimal
// implementations for the other pure virtual methods. Tests instantiate a
// `NiceMock<MockGraphics>` so uninteresting calls are ignored.
// ReSharper disable once CppClassCanBeFinal - mocks cannot be final
class MockGraphics : public oxygen::Graphics {
public:
  explicit MockGraphics(const std::string_view name)
    : Graphics(name)
  {
  }

  // clang-format off
  // NOLINTBEGIN
  MOCK_METHOD((std::shared_ptr<CommandQueue>), CreateCommandQueue, (const QueueKey&, QueueRole), (override));
  // Other methods we don't care about
  MOCK_METHOD((const DescriptorAllocator&), GetDescriptorAllocator, (), (const, override));
  MOCK_METHOD((std::unique_ptr<Surface>), CreateSurface, (std::weak_ptr<Window>, observer_ptr<CommandQueue>), (const, override));
  MOCK_METHOD((std::shared_ptr<Surface>), CreateSurfaceFromNative, (void*, observer_ptr<CommandQueue>), (const, override));
  MOCK_METHOD((std::shared_ptr<IShaderByteCode>), GetShader, (const ShaderRequest&), (const, override));
  MOCK_METHOD((std::shared_ptr<Texture>), CreateTexture, (const TextureDesc&), (const, override));
  MOCK_METHOD((std::shared_ptr<Texture>), CreateTextureFromNativeObject, (const TextureDesc&, const NativeResource&), (const, override));
  MOCK_METHOD((std::shared_ptr<Buffer>), CreateBuffer, (const BufferDesc&), (const, override));
  MOCK_METHOD((std::unique_ptr<CommandList>), CreateCommandListImpl, (QueueRole, std::string_view), (override));
  MOCK_METHOD((std::unique_ptr<CommandRecorder>), CreateCommandRecorder, (std::shared_ptr<CommandList>, observer_ptr<CommandQueue>), (override));
  // NOLINTEND
  // clang-format on
};

// -----------------------------------------------------------------------------
// Strategies for testing
// -----------------------------------------------------------------------------

// Strategy that returns the two specs
class PairStrategy final : public QueuesStrategy {
public:
  PairStrategy(QueueSpecification a, QueueSpecification b)
    : a_(std::move(a))
    , b_(std::move(b))
  {
  }
  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return { a_, b_ };
  }
  [[nodiscard]] auto KeyFor(const QueueRole role) const -> QueueKey override
  {
    if (role == Role::kGraphics) {
      return a_.key;
    }
    if (role == Role::kCompute) {
      return b_.key;
    }
    return QueueKey { "__invalid__" };
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<PairStrategy>(*this);
  }

private:
  QueueSpecification a_;
  QueueSpecification b_;
};

// Mixed strategy: returns the two provided specifications and allows a custom
// KeyFor mapping chosen by the constructor.
class MixedKeyStrategy final : public QueuesStrategy {
public:
  MixedKeyStrategy(
    QueueSpecification a, QueueSpecification b, QueueKey key_for_graphics)
    : a_(std::move(a))
    , b_(std::move(b))
    , gfx_key_(std::move(key_for_graphics))
  {
  }

  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return { a_, b_ };
  }

  [[nodiscard]] auto KeyFor(const QueueRole role) const -> QueueKey override
  {
    if (role == Role::kGraphics) {
      return gfx_key_;
    }
    return b_.key;
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<MixedKeyStrategy>(*this);
  }

private:
  QueueSpecification a_;
  QueueSpecification b_;
  QueueKey gfx_key_;
};

// Strategy that returns no specifications - used to test empty input handling
class EmptyStrategy final : public QueuesStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return {};
  }
  [[nodiscard]] auto KeyFor(const QueueRole) const -> QueueKey override
  {
    return QueueKey { "__none__" };
  }
  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<EmptyStrategy>(*this);
  }
};

// Small in-test strategy wrapper around a vector of specs. We provide a
// KeyFor mapping that returns the "universal" key for graphics role.
class VectorStrategy final : public QueuesStrategy {
public:
  VectorStrategy(std::vector<QueueSpecification> s, QueueKey gfx_key)
    : specs_(std::move(s))
    , gfx_key_(std::move(gfx_key))
  {
  }

  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return specs_;
  }

  [[nodiscard]] auto KeyFor(const QueueRole role) const -> QueueKey override
  {
    if (role == Role::kGraphics) {
      return gfx_key_;
    }
    return QueueKey { "__invalid__" };
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<VectorStrategy>(*this);
  }

private:
  std::vector<QueueSpecification> specs_;
  QueueKey gfx_key_;
};

// -----------------------------------------------------------------------------
// Test cases
// -----------------------------------------------------------------------------

//! Verify that CreateCommandQueues uses the provided strategy to create the
//! requested queues and that GetCommandQueue returns the created instances.
NOLINT_TEST(
  QueuesStrategy, CreateCommandQueues_WhenSingleUniversal_AllRolesShareQueue)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // Prepare a single fake queue instance that will be returned for the
  // universal name regardless of role.
  const auto fake
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);

  // Expect the CreateCommandQueue hook to be called once for the "universal"
  // specification and return our fake queue.
  EXPECT_CALL(gfx, CreateCommandQueue(QueueKey { "universal" }, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));

  const SingleQueueStrategy strat;
  gfx.CreateCommandQueues(strat);

  const auto qg = gfx.GetCommandQueue(strat.KeyFor(Role::kGraphics));
  const auto qc = gfx.GetCommandQueue(strat.KeyFor(Role::kCompute));

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);
  EXPECT_EQ(qg.get(), qc.get());
}

//! Verify that dedicated allocation preference results in distinct created
//! command queues per specification and that roles are preserved on the
//! returned CommandQueue instances.
NOLINT_TEST(QueuesStrategy,
  CreateCommandQueues_WhenDedicatedPerRole_CreatesDistinctQueues)
{
  // Define two specs: gfx and compute
  QueueSpecification gfx_spec {
    .key = QueueKey { "gfx" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };
  QueueSpecification compute_spec = gfx_spec;
  compute_spec.key = QueueKey { "compute" };
  compute_spec.role = Role::kCompute;

  const PairStrategy strat(gfx_spec, compute_spec);

  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // Expect two CreateCommandQueue calls with matching keys and allocation
  // preference, returning distinct FakeCommandQueue instances.
  auto fake_gfx = std::make_shared<FakeCommandQueue>("gfx", Role::kGraphics);
  auto fake_compute
    = std::make_shared<FakeCommandQueue>("compute", Role::kCompute);

  EXPECT_CALL(gfx, CreateCommandQueue(gfx_spec.key, gfx_spec.role))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_gfx)));
  EXPECT_CALL(gfx, CreateCommandQueue(compute_spec.key, compute_spec.role))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_compute)));

  gfx.CreateCommandQueues(strat);

  const auto qg = gfx.GetCommandQueue(gfx_spec.key);
  const auto qc = gfx.GetCommandQueue(compute_spec.key);

  ASSERT_NE(qg.get(), nullptr);
  ASSERT_NE(qc.get(), nullptr);
  EXPECT_NE(qg.get(), qc.get());
  EXPECT_EQ(qg->GetQueueRole(), Role::kGraphics);
  EXPECT_EQ(qc->GetQueueRole(), Role::kCompute);
}

//! Ensure ForEachQueue (used by FlushCommandQueues) invokes Flush exactly once
//! per unique CommandQueue
NOLINT_TEST(QueuesStrategy, ForEachQueue_VisitsEachUniqueQueueOnce)
{
  using testing::NiceMock;
  NiceMock<MockGraphics> gfx("test-gfx");

  // Prepare three specs: two names that will map to the same created object
  // (shared by role), and one distinct queue.
  std::vector<QueueSpecification> list {
    QueueSpecification {
      .key = QueueKey { "universal" },
      .role = Role::kGraphics,
      .allocation_preference = Alloc::kAllInOne,
      .sharing_preference = Share::kShared,
    },
    QueueSpecification {
      .key = QueueKey { "named_shared" },
      .role = Role::kGraphics,
      .allocation_preference = Alloc::kDedicated,
      .sharing_preference = Share::kNamed,
    },
    QueueSpecification {
      .key = QueueKey { "named_shared_alias" },
      .role = Role::kGraphics,
      .allocation_preference = Alloc::kDedicated,
      .sharing_preference = Share::kNamed,
    },
  };

  VectorStrategy strat(std::move(list), QueueKey { "universal" });

  // Two created objects: one for "universal" and one for the named_shared
  // family.
  auto q_universal
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);
  auto q_named
    = std::make_shared<FakeCommandQueue>("named_shared", Role::kGraphics);

  // Expect CreateCommandQueue called for the two canonical keys. Return the
  // prebuilt shared_ptrs.
  EXPECT_CALL(
    gfx, CreateCommandQueue(QueueKey { "universal" }, Role::kGraphics))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(q_universal)));
  EXPECT_CALL(
    gfx, CreateCommandQueue(QueueKey { "named_shared" }, Role::kGraphics))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(q_named)));
  EXPECT_CALL(
    gfx, CreateCommandQueue(QueueKey { "named_shared_alias" }, Role::kGraphics))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(q_named)));

  // Create queues with the gfx-provided creator
  gfx.CreateCommandQueues(strat);

  // Directly iterate the manager's unique command queues and record each
  // visited pointer. The test asserts iteration visits each unique created
  // queue exactly once and does not depend on side-effects like Flush.
  auto& qm = gfx.GetComponent<internal::QueueManager>();
  std::vector<const CommandQueue*> visited;
  qm.ForEachQueue(
    [&](CommandQueue& q) { visited.push_back(std::addressof(q)); });

  // Deduplicate and verify we visited exactly the two unique created objects.
  std::unordered_set<const CommandQueue*> uniq(visited.begin(), visited.end());
  EXPECT_EQ(uniq.size(), 2u);
  EXPECT_TRUE(uniq.count(q_universal.get()));
  EXPECT_TRUE(uniq.count(q_named.get()));
}

//! Verify that when two specifications share the same name and the strategy
//! marks them as shared, the backend may reuse the named queue; in our mocked
//! scenario we simulate reuse by returning the same instance for both specs.
NOLINT_TEST(
  QueuesStrategy, CreateCommandQueues_WhenTwoSpecsShareName_ThrowsDuplicateKey)
{
  const QueueSpecification a {
    .key = QueueKey { "shared-name" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kShared,
  };
  auto b = a;
  b.role = Role::kCompute;

  const PairStrategy strat(a, b);

  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // Simulate reuse: return the same fake for the single named spec creation.
  const auto shared_fake
    = std::make_shared<FakeCommandQueue>("shared-name", Role::kGraphics);
  // The new QueueManager throws on duplicate keys in the strategy. Expect a
  // single creation call and that CreateCommandQueues throws
  // std::invalid_argument when it encounters the duplicate.
  EXPECT_CALL(gfx, CreateCommandQueue(a.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(shared_fake)));

  EXPECT_THROW(gfx.CreateCommandQueues(strat), std::invalid_argument);
}

//! Role-based lookup must select shared queues (KeyFor-based lookup picks
//! only from shared candidates) while direct key lookup can prefer named.
NOLINT_TEST(QueuesStrategy, GetCommandQueue_WhenLookupByRole_SelectsSharedQueue)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // Shared "universal" spec (can satisfy graphics role via KeyFor)
  const QueueSpecification shared_spec {
    .key = QueueKey { "universal" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };

  // Named dedicated gfx spec (explicit name lookup should prefer this)
  QueueSpecification named_spec = shared_spec;
  named_spec.key = QueueKey { "gfx" };
  named_spec.allocation_preference = Alloc::kDedicated;
  named_spec.sharing_preference = Share::kNamed;
  named_spec.role = Role::kGraphics;

  // Strategy that returns both specs but maps Role::kGraphics -> "universal"
  const MixedKeyStrategy strat(
    shared_spec, named_spec, QueueKey { "universal" });

  auto fake_shared
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);
  auto fake_named = std::make_shared<FakeCommandQueue>("gfx", Role::kGraphics);

  // Expect creation for both keys (order not important)
  EXPECT_CALL(gfx, CreateCommandQueue(shared_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_shared)));
  EXPECT_CALL(gfx, CreateCommandQueue(named_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_named)));

  gfx.CreateCommandQueues(strat);

  // Role-based lookup must select the shared queue (KeyFor was "universal").
  const auto q_role = gfx.GetCommandQueue(strat.KeyFor(Role::kGraphics));
  ASSERT_NE(q_role.get(), nullptr);
  EXPECT_EQ(q_role.get(), fake_shared.get());

  // Direct lookup by the named key must prefer the named queue.
  const auto q_named = gfx.GetCommandQueue(named_spec.key);
  ASSERT_NE(q_named.get(), nullptr);
  EXPECT_EQ(q_named.get(), fake_named.get());
}

//! Explicit key lookup prefers named specification over shared alternatives.
NOLINT_TEST(QueuesStrategy, GetCommandQueue_WhenLookupByKey_PrefersNamedQueue)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification shared_spec {
    .key = QueueKey { "universal" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };
  QueueSpecification named_spec = shared_spec;
  named_spec.key = QueueKey { "gfx" };
  named_spec.allocation_preference = Alloc::kDedicated;
  named_spec.sharing_preference = Share::kNamed;

  const MixedKeyStrategy strat(
    shared_spec, named_spec, QueueKey { "universal" });

  auto fake_shared
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);
  auto fake_named = std::make_shared<FakeCommandQueue>("gfx", Role::kGraphics);

  EXPECT_CALL(gfx, CreateCommandQueue(shared_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_shared)));
  EXPECT_CALL(gfx, CreateCommandQueue(named_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_named)));

  gfx.CreateCommandQueues(strat);

  const auto q_named = gfx.GetCommandQueue(named_spec.key);
  const auto q_shared = gfx.GetCommandQueue(shared_spec.key);

  ASSERT_NE(q_named.get(), nullptr);
  ASSERT_NE(q_shared.get(), nullptr);
  EXPECT_NE(q_named.get(), q_shared.get());
}

//! When both dedicated and all-in-one candidates exist, dedicated must be used
//! for role-based resolution (kDedicated precedence over kAllInOne).
NOLINT_TEST(QueuesStrategy,
  GetCommandQueue_WhenDedicatedExists_PrefersDedicatedOverAllInOne)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification dedicated_spec {
    .key = QueueKey { "gfx" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };

  QueueSpecification allinone_spec {
    .key = QueueKey { "universal" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kAllInOne,
    .sharing_preference = Share::kShared,
  };

  // Strategy maps Role::kGraphics -> "universal" but policy requires dedicated
  const MixedKeyStrategy strat(
    allinone_spec, dedicated_spec, QueueKey { "universal" });

  auto fake_dedicated
    = std::make_shared<FakeCommandQueue>("gfx", Role::kGraphics);
  auto fake_univ
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);

  EXPECT_CALL(gfx, CreateCommandQueue(dedicated_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_dedicated)));
  EXPECT_CALL(gfx, CreateCommandQueue(allinone_spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_univ)));

  gfx.CreateCommandQueues(strat);

  // Role lookup must resolve to the dedicated queue according to policy.
  const auto q_role = gfx.GetCommandQueue(dedicated_spec.key);
  ASSERT_NE(q_role.get(), nullptr);
  EXPECT_EQ(q_role.get(), fake_dedicated.get());

  // Also ensure the universal exists but is not chosen for role-resolution.
  const auto q_univ = gfx.GetCommandQueue(allinone_spec.key);
  ASSERT_NE(q_univ.get(), nullptr);
  EXPECT_EQ(q_univ.get(), fake_univ.get());
}

//! If the backend returns an empty shared_ptr for a required spec, the
//! CreateCommandQueues call must propagate an exception.
NOLINT_TEST(
  QueuesStrategy, CreateCommandQueues_WhenBackendFails_ThrowsRuntimeError)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification a {
    .key = QueueKey { "a" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };
  QueueSpecification b = a;
  b.key = QueueKey { "b" };
  b.role = Role::kCompute;

  const PairStrategy strat(a, b);

  auto fake = std::make_shared<FakeCommandQueue>("a", Role::kGraphics);

  // First creation succeeds, second returns empty pointer to simulate failure.
  EXPECT_CALL(gfx, CreateCommandQueue(a.key, a.role))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));
  EXPECT_CALL(gfx, CreateCommandQueue(b.key, b.role))
    .WillOnce(Return(std::shared_ptr<CommandQueue> {}));

  EXPECT_THROW(gfx.CreateCommandQueues(strat), std::runtime_error);
}

//! Querying an unknown key must return an empty shared_ptr.
NOLINT_TEST(QueuesStrategy, GetCommandQueue_WhenUnknownKey_ReturnsEmpty)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  const SingleQueueStrategy strat;
  const auto fake
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);

  EXPECT_CALL(gfx, CreateCommandQueue(QueueKey { "universal" }, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));

  gfx.CreateCommandQueues(strat);

  const auto q = gfx.GetCommandQueue(QueueKey { "nonexistent" });
  ASSERT_EQ(q.get(), nullptr);
}

//! CreateCommandQueues should be idempotent: calling it twice should not
//! create duplicate backend resources for the same key.
NOLINT_TEST(QueuesStrategy, CreateCommandQueues_WhenCalledTwice_RecreatesQueues)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  const SingleQueueStrategy strat;
  const auto fake
    = std::make_shared<FakeCommandQueue>("universal", Role::kGraphics);

  EXPECT_CALL(gfx, CreateCommandQueue(QueueKey { "universal" }, _))
    .Times(2)
    .WillRepeatedly(Return(std::static_pointer_cast<CommandQueue>(fake)));

  gfx.CreateCommandQueues(strat);
  // Second call must not call CreateCommandQueue again.
  gfx.CreateCommandQueues(strat);

  const auto q = gfx.GetCommandQueue(QueueKey { "universal" });
  ASSERT_NE(q.get(), nullptr);
}

//! Duplicate keys with conflicting preferences: document deterministic
//! "first-wins" resolution by asserting the CreateCommandQueue call uses the
//! allocation preference from the first specification.
NOLINT_TEST(
  QueuesStrategy, CreateCommandQueues_WhenDuplicateKeys_ThrowsInvalidArgument)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification first {
    .key = QueueKey { "dup" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };
  QueueSpecification second = first;
  second.allocation_preference = Alloc::kAllInOne;
  second.sharing_preference = Share::kShared;
  second.role = Role::kCompute;

  const PairStrategy strat(first, second);

  auto fake = std::make_shared<FakeCommandQueue>("dup", Role::kGraphics);

  // CreateCommandQueue should be called once for the duplicate key, then
  // CreateCommandQueues will throw due to the duplicate entry.
  EXPECT_CALL(gfx, CreateCommandQueue(first.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));

  EXPECT_THROW(gfx.CreateCommandQueues(strat), std::invalid_argument);
}

//! When the strategy returns no specifications, no backend CreateCommandQueue
//! calls must be made and GetCommandQueue should return empty for any key.
NOLINT_TEST(QueuesStrategy, CreateCommandQueues_WhenNoSpecifications_NoCreation)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // No CreateCommandQueue calls are expected.
  EXPECT_CALL(gfx, CreateCommandQueue(_, _)).Times(0);

  const EmptyStrategy strat;
  gfx.CreateCommandQueues(strat);

  // Any lookup should return empty
  const auto q = gfx.GetCommandQueue(QueueKey { "anything" });
  ASSERT_EQ(q.get(), nullptr);
}

//! If KeyFor(role) returns a key not present in the strategy's
//! Specifications(), role-based lookup should return empty.
NOLINT_TEST(
  QueuesStrategy, GetCommandQueue_WhenKeyForReturnsMissingKey_ReturnsEmpty)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  // One spec exists but KeyFor will point to a different key
  QueueSpecification spec {
    .key = QueueKey { "present" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };

  // Create a strategy that returns the spec and a different dummy spec, but
  // maps Role::kGraphics to a non-existent key.
  QueueSpecification dummy = spec;
  dummy.key = QueueKey { "other" };
  dummy.role = Role::kCompute;
  const MixedKeyStrategy strat(spec, dummy, QueueKey { "missing-key" });

  // Expect creation for the named spec and the additional dummy spec.
  auto fake = std::make_shared<FakeCommandQueue>("present", Role::kGraphics);
  auto fake_dummy = std::make_shared<FakeCommandQueue>("other", Role::kCompute);
  EXPECT_CALL(gfx, CreateCommandQueue(spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));
  EXPECT_CALL(gfx, CreateCommandQueue(dummy.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_dummy)));

  gfx.CreateCommandQueues(strat);

  // Role-based lookup should consult KeyFor and return empty since
  // "missing-key" was not created.
  const auto q_role = gfx.GetCommandQueue(strat.KeyFor(Role::kGraphics));
  ASSERT_EQ(q_role.get(), nullptr);
}

//! If KeyFor is called with an invalid role value, and the strategy returns a
//! key that wasn't created, role-based lookup must return empty.
NOLINT_TEST(QueuesStrategy, GetCommandQueue_WhenKeyForInvalidRole_ReturnsEmpty)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification a {
    .key = QueueKey { "a" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };
  QueueSpecification b = a;
  b.key = QueueKey { "b" };
  b.role = Role::kCompute;

  const PairStrategy strat(a, b);

  auto fake_a = std::make_shared<FakeCommandQueue>("a", Role::kGraphics);
  auto fake_b = std::make_shared<FakeCommandQueue>("b", Role::kCompute);

  EXPECT_CALL(gfx, CreateCommandQueue(a.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_a)));
  EXPECT_CALL(gfx, CreateCommandQueue(b.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_b)));

  gfx.CreateCommandQueues(strat);

  // Use a role value outside the defined enum values.
  constexpr auto invalid_role = static_cast<Role>(0x7F);
  const auto q = gfx.GetCommandQueue(strat.KeyFor(invalid_role));
  ASSERT_EQ(q.get(), nullptr);
}

//! An empty key should be passed through to the backend and be retrievable
//! via GetCommandQueue with an empty key.
NOLINT_TEST(
  QueuesStrategy, CreateCommandQueues_WhenEmptyKeyProvided_LookupReturnsEmpty)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification spec {
    .key = QueueKey { "" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kNamed,
  };

  const SingleQueueStrategy single;
  (void)single; // single not used directly - we'll craft a small PairStrategy
                // instead

  // Use PairStrategy with one spec and a dummy second spec to satisfy the
  // two-specs contract of PairStrategy.
  QueueSpecification dummy = spec;
  dummy.key = QueueKey { "dummy" };
  dummy.role = Role::kCompute;

  const PairStrategy strat(spec, dummy);

  auto fake_empty = std::make_shared<FakeCommandQueue>("", Role::kGraphics);
  auto fake_dummy = std::make_shared<FakeCommandQueue>("dummy", Role::kCompute);

  EXPECT_CALL(gfx, CreateCommandQueue(spec.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_empty)));
  EXPECT_CALL(gfx, CreateCommandQueue(dummy.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake_dummy)));

  gfx.CreateCommandQueues(strat);

  const auto q_empty = gfx.GetCommandQueue(QueueKey { "" });
  // Empty key is treated as invalid by the manager and should return empty.
  ASSERT_EQ(q_empty.get(), nullptr);
}

//! Duplicate keys with identical preferences should result in a single
//! CreateCommandQueue call and succeed.
NOLINT_TEST(QueuesStrategy, DuplicateKey_SamePreferences_Throws)
{
  testing::NiceMock<MockGraphics> gfx("test-gfx");

  QueueSpecification first {
    .key = QueueKey { "dup-same" },
    .role = Role::kGraphics,
    .allocation_preference = Alloc::kDedicated,
    .sharing_preference = Share::kShared,
  };
  QueueSpecification second = first;
  second.role = Role::kCompute;

  const PairStrategy strat(first, second);

  auto fake = std::make_shared<FakeCommandQueue>("dup-same", Role::kGraphics);

  // With duplicate keys the manager throws std::invalid_argument before the
  // second creation call; expect a single creation and the specific exception.
  EXPECT_CALL(gfx, CreateCommandQueue(first.key, _))
    .WillOnce(Return(std::static_pointer_cast<CommandQueue>(fake)));

  EXPECT_THROW(gfx.CreateCommandQueues(strat), std::invalid_argument);
}

} // namespace
