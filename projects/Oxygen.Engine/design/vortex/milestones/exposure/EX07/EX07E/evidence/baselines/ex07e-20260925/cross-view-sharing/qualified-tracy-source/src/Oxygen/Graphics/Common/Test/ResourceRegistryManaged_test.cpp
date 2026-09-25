//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <barrier>
#include <limits>
#include <thread>

#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>
#include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#include <Oxygen/Graphics/Common/Test/Mocks/MockGraphics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics {
struct ResourceRegistryTestAccess {
  static auto SetNextRegistration(ResourceRegistry& registry, uint64_t value)
    -> void
  {
    std::lock_guard lock(registry.state_->mutex);
    registry.state_->next_registration = value;
  }
};
} // namespace oxygen::graphics

namespace {
using namespace oxygen::graphics;
using FakeResource = oxygen::graphics::testing::FakeResource;
using TestViewDesc = oxygen::graphics::testing::TestViewDesc;
using MockGraphics = oxygen::graphics::testing::MockGraphics;

// Keep mock-framework bookkeeping out of allocation-failure scopes.
class ManagedTestAllocator final : public detail::BaseDescriptorAllocator {
public:
  auto CopyDescriptor(const DescriptorAllocationHandle&,
    const DescriptorAllocationHandle&) -> void override
  {
  }
  auto GetShaderVisibleIndex(
    const DescriptorAllocationHandle& handle) const noexcept
    -> oxygen::ShaderVisibleIndex override
  {
    return oxygen::ShaderVisibleIndex { handle.GetBindlessHandle().get() };
  }

protected:
  auto CreateHeapSegment(oxygen::bindless::Capacity capacity,
    oxygen::bindless::HeapIndex base, ResourceViewType type,
    DescriptorVisibility visibility)
    -> std::unique_ptr<detail::DescriptorSegment> override
  {
    return std::make_unique<detail::FixedDescriptorSegment>(
      capacity, base, type, visibility);
  }
};

class ManagedTestGraphics final : public ::testing::NiceMock<MockGraphics> {
public:
  explicit ManagedTestGraphics(std::shared_ptr<ManagedTestAllocator> allocator)
    : ::testing::NiceMock<MockGraphics>("managed registration")
    , allocator_(std::move(allocator))
  {
  }
  auto GetDescriptorAllocator() const -> const DescriptorAllocator& override
  {
    return *allocator_;
  }

private:
  std::shared_ptr<ManagedTestAllocator> allocator_;
};

class ManagedRegistryTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    allocator = std::make_shared<ManagedTestAllocator>();
    graphics = std::make_shared<ManagedTestGraphics>(allocator);
    graphics->InstallBackendOwner(graphics, BackendIncarnationId { 71 }, {});
    resource = std::make_shared<FakeResource>();
    resource->WithViewBehavior(
      [this](const auto& handle, const auto& description) {
        last_view_index = handle.GetBindlessHandle();
        return NativeView { description.id + 1, FakeResource::ClassTypeId() };
      });
  }
  auto TearDown() -> void override
  {
    if (graphics) {
      graphics->Close();
    }
  }
  auto Registry() -> ResourceRegistry&
  {
    return graphics->GetResourceRegistry();
  }
  auto Description(uint64_t id = 1) -> TestViewDesc
  {
    return { .view_type = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .id = id };
  }
  std::shared_ptr<ManagedTestAllocator> allocator;
  std::shared_ptr<ManagedTestGraphics> graphics;
  std::shared_ptr<FakeResource> resource;
  oxygen::bindless::HeapIndex last_view_index { 0 };
};

NOLINT_TEST_F(
  ManagedRegistryTest, LastOwnerClosesAcquisitionUntilEveryUseRetires)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  const auto identity = lease->Identity();
  auto a = Registry().RetainUse(*lease);
  auto b = Registry().RetainUse(*lease);
  ASSERT_TRUE(a);
  ASSERT_TRUE(b);
  *lease = RegistrationLease {};
  EXPECT_EQ(
    Registry().AcquireManaged(identity).error(), RegistrationError::kClosed);
  EXPECT_EQ(
    Registry().RegisterManaged(resource).error(), RegistrationError::kClosed);
  *a = RegistrationUse {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 1U);
  *b = RegistrationUse {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
  EXPECT_EQ(Registry().AcquireManaged(identity).error(),
    RegistrationError::kStaleRegistration);
  auto replacement = Registry().RegisterManaged(resource);
  ASSERT_TRUE(replacement);
  EXPECT_NE(replacement->Identity(), identity);
}

NOLINT_TEST_F(
  ManagedRegistryTest, RegistrationIdsExhaustWithoutWrapOrNativeViewCreation)
{
  constexpr auto limit = (std::numeric_limits<uint64_t>::max)();
  ResourceRegistryTestAccess::SetNextRegistration(Registry(), limit - 1);
  auto last = Registry().RegisterManaged(resource);
  ASSERT_TRUE(last);
  EXPECT_EQ(last->Identity().registration.get(), limit - 1);
  *last = RegistrationLease {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().RegisterManaged(resource).error(),
    RegistrationError::kAllocationFailed);
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
  EXPECT_EQ(resource->CallCount(), 0U);
}

NOLINT_TEST_F(ManagedRegistryTest,
  InternalAllocationOwnerKeepsAcquisitionOpenWithoutFacadeCycle)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  const auto id = lease->Identity();
  auto owner = lease->AllocationOwner();
  *lease = RegistrationLease {};
  EXPECT_TRUE(Registry().AcquireManaged(id));
  const std::weak_ptr<oxygen::Graphics> weak = graphics;
  graphics->Close();
  graphics.reset();
  EXPECT_TRUE(weak.expired());
  owner = RegistrationOwner {};
}

NOLINT_TEST_F(ManagedRegistryTest, ManualAndManagedOwnershipCannotMix)
{
  Registry().Register(resource);
  EXPECT_EQ(Registry().RegisterManaged(resource).error(),
    RegistrationError::kOwnershipConflict);
  Registry().UnRegisterResource(*resource);
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  EXPECT_THROW(Registry().Register(resource), std::logic_error);
  EXPECT_THROW(
    (void)Registry().AcquireRegistration(resource), std::logic_error);
  EXPECT_THROW(Registry().UnRegisterResource(*resource), std::logic_error);
  auto wrong = lease->Identity();
  wrong.backend = BackendIncarnationId { 72 };
  EXPECT_EQ(
    Registry().AcquireManaged(wrong).error(), RegistrationError::kWrongBackend);
}

NOLINT_TEST_F(
  ManagedRegistryTest, DifferentWrappersCannotBypassNativeOwnershipMode)
{
  const NativeResource native { uint64_t { 991 }, FakeResource::ClassTypeId() };
  resource->WithNativeResource(native);
  auto alias = std::make_shared<FakeResource>();
  alias->WithNativeResource(native);
  Registry().Register(resource);
  EXPECT_EQ(Registry().RegisterManaged(alias).error(),
    RegistrationError::kOwnershipConflict);
  Registry().UnRegisterResource(*resource);
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  EXPECT_EQ(Registry().RegisterManaged(alias).error(),
    RegistrationError::kOwnershipConflict);
  EXPECT_THROW(Registry().Register(alias), std::logic_error);
  EXPECT_THROW((void)Registry().AcquireRegistration(alias), std::logic_error);
}

NOLINT_TEST_F(
  ManagedRegistryTest, NativeStateIsForgottenOnlyAfterLastManualAlias)
{
  const NativeResource native { uint64_t { 992 }, FakeResource::ClassTypeId() };
  resource->WithNativeResource(native);
  auto alias = std::make_shared<FakeResource>();
  alias->WithNativeResource(native);
  unsigned forgotten = 0;
  Registry().SetResourceUnregisteredCallback([&](const auto&) { ++forgotten; });
  Registry().Register(resource);
  Registry().Register(alias);
  Registry().UnRegisterResource(*resource);
  EXPECT_EQ(forgotten, 0U);
  Registry().UnRegisterResource(*alias);
  EXPECT_EQ(forgotten, 1U);
  Registry().SetResourceUnregisteredCallback({});
}

NOLINT_TEST_F(
  ManagedRegistryTest, EveryRawViewMutationRejectsBeforeNativeCreation)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  const auto desc = Description();
  auto view = Registry().AcquireManagedView<FakeResource>(*lease, desc);
  ASSERT_TRUE(view);
  const auto managed_index = last_view_index;
  const auto native_calls = resource->CallCount();
  EXPECT_THROW(Registry().UnRegisterViews(*resource), std::logic_error);
  EXPECT_THROW(
    Registry().UnRegisterView(*resource, view->view), std::logic_error);
  const std::array views { view->view };
  EXPECT_THROW(Registry().UnRegisterViews(*resource, views), std::logic_error);
  auto manual = std::make_shared<FakeResource>();
  Registry().Register(manual);
  EXPECT_THROW((void)Registry().UpdateView(*manual, managed_index, desc),
    std::logic_error);
  EXPECT_THROW((void)Registry().UpdateView(*resource, managed_index, desc),
    std::logic_error);
  EXPECT_THROW(
    Registry().Replace(*resource, manual, nullptr), std::logic_error);
  EXPECT_THROW(
    Registry().Replace(*manual, resource, nullptr), std::logic_error);
  for (int operation = 0; operation < 3; ++operation) {
    auto handle = allocator->AllocateRaw(desc.view_type, desc.visibility);
    if (operation == 0) {
      EXPECT_THROW(
        (void)Registry().RegisterView(*resource, std::move(handle), desc),
        std::logic_error);
    } else if (operation == 1) {
      EXPECT_THROW((void)Registry().RegisterView(
                     *resource, view->view, std::move(handle), desc),
        std::logic_error);
    } else {
      EXPECT_THROW((void)Registry().AcquireViewRegistration(
                     *resource, std::move(handle), desc),
        std::logic_error);
    }
  }
  EXPECT_EQ(resource->CallCount(), native_calls);
  EXPECT_EQ(Registry().Find(*resource, desc), view->view);
}

NOLINT_TEST_F(ManagedRegistryTest,
  EqualViewsPublishOnceAcrossThreadsAndHashCollisionsStayDistinct)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  auto a = Description(11);
  auto b = Description(22);
  a.force_hash_collision = b.force_hash_collision = true;
  std::barrier start(3);
  std::array<NativeView, 2> observed;
  std::array<std::jthread, 2> workers;
  for (size_t index = 0; index < workers.size(); ++index) {
    workers[index] = std::jthread([&, index] {
      start.arrive_and_wait();
      const auto result
        = Registry().AcquireManagedView<FakeResource>(*lease, a);
      if (result) {
        observed[index] = result->view;
      }
    });
  }
  start.arrive_and_wait();
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_TRUE(observed[0]->IsValid());
  EXPECT_EQ(observed[0], observed[1]);
  EXPECT_EQ(resource->CallCount(), 1U);
  const auto distinct = Registry().AcquireManagedView<FakeResource>(*lease, b);
  ASSERT_TRUE(distinct);
  EXPECT_NE(distinct->view, observed[0]);
  EXPECT_EQ(
    Registry().AcquireManagedView<FakeResource>(*lease, a)->view, observed[0]);
  EXPECT_EQ(resource->CallCount(), 2U);
}

NOLINT_TEST_F(
  ManagedRegistryTest, FailedNativeViewLeavesExistingPublishedViewsIntact)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  const auto original
    = Registry().AcquireManagedView<FakeResource>(*lease, Description(1));
  ASSERT_TRUE(original);
  resource->WithThrowingView(2);
  const auto failed
    = Registry().AcquireManagedView<FakeResource>(*lease, Description(2));
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), RegistrationError::kAllocationFailed);
  EXPECT_EQ(
    Registry().AcquireManagedView<FakeResource>(*lease, Description(1))->view,
    original->view);
  EXPECT_FALSE(Registry().Contains(*resource, Description(2)));
}

NOLINT_TEST_F(ManagedRegistryTest, WorkerReleaseQueuesOwnerThreadRetirement)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  std::jthread worker(
    [owner = std::move(*lease)]() mutable { owner = RegistrationLease {}; });
  worker.join();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 1U);
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
}

NOLINT_TEST_F(
  ManagedRegistryTest, ClosedRegistryAllowsLateCpuCleanupWithoutFacadeOrFrame)
{
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  ASSERT_TRUE(
    Registry().AcquireManagedView<FakeResource>(*lease, Description()));
  auto use = Registry().RetainUse(*lease);
  ASSERT_TRUE(use);
  const std::weak_ptr<FakeResource> weak_resource = resource;
  *lease = RegistrationLease {};
  resource.reset();
  graphics->Close();
  graphics.reset();
  EXPECT_FALSE(weak_resource.expired());
  std::jthread worker(
    [pin = std::move(*use)]() mutable { pin = RegistrationUse {}; });
  worker.join();
  EXPECT_TRUE(weak_resource.expired());
}
} // namespace

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(ManagedRegistryTest,
  AllocationFailureRollsBackRegistrationWithoutLosingExistingOwner)
{
  using oxygen::graphics::testing::HeapAllocationFailure;
  auto existing = Registry().RegisterManaged(resource);
  ASSERT_TRUE(existing);
  auto additional = std::make_shared<FakeResource>();
  const auto excluded_before = HeapAllocationFailure::ExcludedCount();
  bool reached_success = false;
  unsigned failures = 0;
  for (int allowed = 0; allowed < 64 && !reached_success; ++allowed) {
    std::expected<RegistrationLease, RegistrationError> result
      = std::unexpected(RegistrationError::kAllocationFailed);
    {
      HeapAllocationFailure fail(
        allowed, HeapAllocationFailure::DebugProxies::kExcludeBySize);
      result = Registry().RegisterManaged(additional);
    }
    if (result) {
      reached_success = true;
    } else {
      ++failures;
      EXPECT_EQ(result.error(), RegistrationError::kAllocationFailed);
      EXPECT_EQ(Registry().GetRegisteredResourceCount(), 1U);
    }
    EXPECT_TRUE(Registry().AcquireManaged(existing->Identity()));
  }
  EXPECT_TRUE(reached_success);
  EXPECT_GT(failures, 2U);
  EXPECT_GT(HeapAllocationFailure::ExcludedCount(), excluded_before);
  Registry().PollManagedRetirements();
}

NOLINT_TEST_F(
  ManagedRegistryTest, AllocationFailureRollsBackViewAndPreservesPublishedView)
{
  using oxygen::graphics::testing::HeapAllocationFailure;
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  const auto first_desc = Description(1);
  const auto first
    = Registry().AcquireManagedView<FakeResource>(*lease, first_desc);
  ASSERT_TRUE(first);
  bool reached_success = false;
  unsigned failures = 0;
  for (int allowed = 0; allowed < 64 && !reached_success; ++allowed) {
    std::expected<ManagedView, RegistrationError> result
      = std::unexpected(RegistrationError::kAllocationFailed);
    {
      HeapAllocationFailure fail(allowed);
      result
        = Registry().AcquireManagedView<FakeResource>(*lease, Description(2));
    }
    if (result) {
      reached_success = true;
    } else {
      ++failures;
      EXPECT_EQ(result.error(), RegistrationError::kAllocationFailed);
      EXPECT_FALSE(Registry().Contains(*resource, Description(2)));
      EXPECT_EQ(allocator
                  ->GetAllocatedDescriptorsCount(
                    first_desc.view_type, first_desc.visibility)
                  .get(),
        1U);
    }
    EXPECT_EQ(
      Registry().AcquireManagedView<FakeResource>(*lease, first_desc)->view,
      first->view);
  }
  EXPECT_TRUE(reached_success);
  EXPECT_GT(failures, 2U);
}

NOLINT_TEST_F(ManagedRegistryTest, OwnerUseAndDescriptorRetirementDoNotAllocate)
{
  using oxygen::graphics::testing::HeapAllocationFailure;
  auto lease = Registry().RegisterManaged(resource);
  ASSERT_TRUE(lease);
  ASSERT_TRUE(
    Registry().AcquireManagedView<FakeResource>(*lease, Description()));
  auto use = Registry().RetainUse(*lease);
  ASSERT_TRUE(use);
  const auto before = HeapAllocationFailure::RejectedCount();
  {
    HeapAllocationFailure fail;
    *lease = RegistrationLease {};
    *use = RegistrationUse {};
    Registry().PollManagedRetirements();
  }
  EXPECT_EQ(HeapAllocationFailure::RejectedCount(), before);
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
}
#endif
