//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <barrier>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DescriptorSegment.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>
#include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#include <Oxygen/Graphics/Common/Test/Mocks/MockGraphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics {
struct ResourceRegistryTestAccess {
  static auto SetNextRegistration(ResourceRegistry& registry, uint64_t value)
    -> void
  {
    std::scoped_lock lock(registry.state_->mutex);
    registry.state_->next_registration = value;
  }
};
} // namespace oxygen::graphics

namespace {
using oxygen::graphics::BackendIncarnationId;
using oxygen::graphics::DescriptorAllocationHandle;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ManagedBuffer;
using oxygen::graphics::ManagedTexture;
using oxygen::graphics::ManagedView;
using oxygen::graphics::NativeResource;
using oxygen::graphics::NativeView;
using oxygen::graphics::RegistrationError;
using oxygen::graphics::RegistrationLease;
using oxygen::graphics::RegistrationOwner;
using oxygen::graphics::RegistrationUse;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceRegistryTestAccess;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureSubResourceSet;
using oxygen::graphics::TextureViewDescription;
using oxygen::graphics::TextureViewRequest;
namespace detail = oxygen::graphics::detail;

constexpr std::uint32_t kCubeFaceCount = 6U;
constexpr BackendIncarnationId kTestBackend { 71U };
constexpr BackendIncarnationId kOtherBackend { 72U };
using oxygen::graphics::testing::FakeResource;
using oxygen::graphics::testing::MockGraphics;
using oxygen::graphics::testing::TestViewDesc;

// Keep mock-framework bookkeeping out of allocation-failure scopes.
class ManagedTestAllocator final : public detail::BaseDescriptorAllocator {
public:
  auto CopyDescriptor(const DescriptorAllocationHandle& /*source*/,
    const DescriptorAllocationHandle& /*destination*/) -> void override
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

class ManagedTestTexture final : public Texture {
public:
  ManagedTestTexture()
    : Texture("Managed texture")
  {
  }
  auto GetDescriptor() const -> const TextureDesc& override { return desc; }
  auto GetNativeResource() const -> NativeResource override
  {
    return { this, Texture::ClassTypeId() };
  }
  mutable std::vector<TextureViewDescription> created;
  std::optional<std::size_t> fail_view;
  TextureDesc desc {
    .width = 16U,
    .height = 16U,
    .array_size = kCubeFaceCount,
    .mip_levels = 2U,
    .format = oxygen::Format::kRGBA32Float,
    .texture_type = oxygen::TextureType::kTextureCube,
    .is_shader_resource = true,
    .is_uav = true,
  };

private:
  auto View(const DescriptorAllocationHandle& handle,
    const TextureViewDescription& description) const -> NativeView
  {
    const auto index = created.size();
    created.push_back(description);
    if (fail_view && index == *fail_view) {
      return {};
    }
    return { handle.GetBindlessHandle().get() + 1U, Texture::ClassTypeId() };
  }
  auto CreateShaderResourceView(const DescriptorAllocationHandle& handle,
    oxygen::Format format, oxygen::TextureType dimension,
    TextureSubResourceSet ranges) const -> NativeView override
  {
    return View(handle,
      {
        .view_type = ResourceViewType::kTexture_SRV,
        .format = format,
        .dimension = dimension,
        .sub_resources = ranges,
      });
  }
  auto CreateUnorderedAccessView(const DescriptorAllocationHandle& handle,
    oxygen::Format format, oxygen::TextureType dimension,
    TextureSubResourceSet ranges) const -> NativeView override
  {
    return View(handle,
      {
        .view_type = ResourceViewType::kTexture_UAV,
        .format = format,
        .dimension = dimension,
        .sub_resources = ranges,
      });
  }
  auto CreateRenderTargetView(const DescriptorAllocationHandle& handle,
    oxygen::Format format, TextureSubResourceSet ranges) const
    -> NativeView override
  {
    return View(handle,
      {
        .view_type = ResourceViewType::kTexture_RTV,
        .format = format,
        .sub_resources = ranges,
      });
  }
  auto CreateDepthStencilView(const DescriptorAllocationHandle& handle,
    oxygen::Format format, TextureSubResourceSet ranges, bool read_only) const
    -> NativeView override
  {
    return View(handle,
      {
        .view_type = ResourceViewType::kTexture_DSV,
        .format = format,
        .sub_resources = ranges,
        .is_read_only_dsv = read_only,
      });
  }
};

auto InitialTextureViews() -> std::array<TextureViewRequest, 3>
{
  using oxygen::Format;
  using oxygen::TextureType;
  return { TextureViewRequest { .description={ .view_type = ResourceViewType::kTexture_SRV,
                                  .format = Format::kRGBA32Float,
                                  .dimension = TextureType::kTextureCube, },
             .domain=oxygen::bindless::generated::kTexturesDomain, },
    TextureViewRequest { .description={ .view_type = ResourceViewType::kTexture_UAV,
                           .format = Format::kRGBA32Float,
                           .dimension = TextureType::kTexture2DArray,
                           .sub_resources = { .base_mip_level = 0U,
                             .num_mip_levels = 1U,
                             .base_array_slice = 0U,
                             .num_array_slices = kCubeFaceCount, }, },
      .domain={}, },
    TextureViewRequest { .description={ .view_type = ResourceViewType::kTexture_UAV,
                           .format = Format::kRGBA32Float,
                           .dimension = TextureType::kTexture2DArray,
                           .sub_resources = { .base_mip_level = 1U,
                             .num_mip_levels = 1U,
                             .base_array_slice = 0U,
                             .num_array_slices = kCubeFaceCount, }, },
      .domain={}, }, };
}

static_assert(!std::is_copy_constructible_v<ManagedTexture>);
static_assert(!std::is_copy_constructible_v<ManagedBuffer>);
static_assert(std::is_nothrow_move_constructible_v<ManagedTexture>);
static_assert(std::is_nothrow_move_constructible_v<ManagedBuffer>);

class ManagedRegistryTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    allocator_ = std::make_shared<ManagedTestAllocator>();
    graphics_ = std::make_shared<ManagedTestGraphics>(allocator_);
    graphics_->InstallBackendOwner(graphics_, kTestBackend, {});
    resource_ = std::make_shared<FakeResource>();
    resource_->WithViewBehavior(
      [this](const auto& handle, const auto& description) -> auto {
        last_view_index_ = handle.GetBindlessHandle();
        return NativeView { description.id + 1, FakeResource::ClassTypeId() };
      });
  }
  auto TearDown() -> void override
  {
    if (graphics_) {
      graphics_->Close();
    }
  }
  auto Registry() -> ResourceRegistry&
  {
    return graphics_->GetResourceRegistry();
  }
  auto Description(uint64_t id = 1) -> TestViewDesc
  {
    return {
      .view_type = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .id = id,
    };
  }
  auto Allocator() -> ManagedTestAllocator& { return *allocator_; }
  auto GraphicsOwner() -> std::shared_ptr<ManagedTestGraphics>&
  {
    return graphics_;
  }
  auto Resource() -> std::shared_ptr<FakeResource>& { return resource_; }
  [[nodiscard]] auto LastViewIndex() const -> oxygen::bindless::HeapIndex
  {
    return last_view_index_;
  }

private:
  std::shared_ptr<ManagedTestAllocator> allocator_;
  std::shared_ptr<ManagedTestGraphics> graphics_;
  std::shared_ptr<FakeResource> resource_;
  oxygen::bindless::HeapIndex last_view_index_ { 0 };
};

NOLINT_TEST_F(
  ManagedRegistryTest, ManagedViewCacheDistinguishesAllocationDomains)
{
  namespace domains = oxygen::bindless::generated;
  const auto registration = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(registration.has_value());
  auto description = Description();
  description.view_type = ResourceViewType::kTexture_SRV;
  description.visibility = DescriptorVisibility::kShaderVisible;
  const auto raw
    = Registry().AcquireManagedView<FakeResource>(*registration, description);
  const auto texture = Registry().AcquireManagedView<FakeResource>(
    *registration, description, domains::kTexturesDomain);
  const auto global = Registry().AcquireManagedView<FakeResource>(
    *registration, description, domains::kGlobalSrvDomain);
  ASSERT_TRUE(raw.has_value());
  ASSERT_TRUE(texture.has_value());
  ASSERT_TRUE(global.has_value());
  EXPECT_NE(raw->shader_visible_index, texture->shader_visible_index);
  EXPECT_NE(raw->shader_visible_index, global->shader_visible_index);
  EXPECT_NE(texture->shader_visible_index, global->shader_visible_index);
  EXPECT_EQ(Registry()
              .AcquireManagedView<FakeResource>(*registration, description)
              ->shader_visible_index,
    raw->shader_visible_index);
  EXPECT_EQ(Registry()
              .AcquireManagedView<FakeResource>(
                *registration, description, domains::kTexturesDomain)
              ->shader_visible_index,
    texture->shader_visible_index);
  EXPECT_EQ(Registry()
              .AcquireManagedView<FakeResource>(
                *registration, description, domains::kGlobalSrvDomain)
              ->shader_visible_index,
    global->shader_visible_index);
}

NOLINT_TEST_F(ManagedRegistryTest, BindlessManagedViewRejectsCpuOnlyVisibility)
{
  const auto registration = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(registration.has_value());
  EXPECT_FALSE(Registry()
      .AcquireManagedView<FakeResource>(*registration, Description(),
        oxygen::bindless::generated::kTexturesDomain)
      .has_value());
  EXPECT_TRUE(Registry()
      .AcquireManagedView<FakeResource>(*registration, Description())
      .has_value());
}

NOLINT_TEST_F(
  ManagedRegistryTest, LastOwnerClosesAcquisitionUntilEveryUseRetires)
{
  auto lease = Registry().RegisterManaged(Resource());
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
    Registry().RegisterManaged(Resource()).error(), RegistrationError::kClosed);
  *a = RegistrationUse {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 1U);
  *b = RegistrationUse {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
  EXPECT_EQ(Registry().AcquireManaged(identity).error(),
    RegistrationError::kStaleRegistration);
  auto replacement = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(replacement);
  EXPECT_NE(replacement->Identity(), identity);
}

NOLINT_TEST_F(
  ManagedRegistryTest, RegistrationIdsExhaustWithoutWrapOrNativeViewCreation)
{
  constexpr auto limit = std::numeric_limits<uint64_t>::max();
  ResourceRegistryTestAccess::SetNextRegistration(Registry(), limit - 1);
  auto last = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(last);
  EXPECT_EQ(last->Identity().registration.get(), limit - 1);
  *last = RegistrationLease {};
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().RegisterManaged(Resource()).error(),
    RegistrationError::kAllocationFailed);
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
  EXPECT_EQ(Resource()->CallCount(), 0U);
}

NOLINT_TEST_F(ManagedRegistryTest,
  InternalAllocationOwnerKeepsAcquisitionOpenWithoutFacadeCycle)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  const auto id = lease->Identity();
  auto owner = lease->AllocationOwner();
  *lease = RegistrationLease {};
  EXPECT_TRUE(Registry().AcquireManaged(id));
  const std::weak_ptr<oxygen::Graphics> weak = GraphicsOwner();
  GraphicsOwner()->Close();
  GraphicsOwner().reset();
  EXPECT_TRUE(weak.expired());
  owner = RegistrationOwner {};
}

NOLINT_TEST_F(ManagedRegistryTest, ManualAndManagedOwnershipCannotMix)
{
  Registry().Register(Resource());
  EXPECT_EQ(Registry().RegisterManaged(Resource()).error(),
    RegistrationError::kOwnershipConflict);
  Registry().UnRegisterResource(*Resource());
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  EXPECT_THROW(Registry().Register(Resource()), std::logic_error);
  EXPECT_THROW(
    (void)Registry().AcquireRegistration(Resource()), std::logic_error);
  EXPECT_THROW(Registry().UnRegisterResource(*Resource()), std::logic_error);
  auto wrong = lease->Identity();
  wrong.backend = kOtherBackend;
  EXPECT_EQ(
    Registry().AcquireManaged(wrong).error(), RegistrationError::kWrongBackend);
}

NOLINT_TEST_F(
  ManagedRegistryTest, DifferentWrappersCannotBypassNativeOwnershipMode)
{
  const NativeResource native { uint64_t { 991 }, FakeResource::ClassTypeId() };
  Resource()->WithNativeResource(native);
  auto alias = std::make_shared<FakeResource>();
  alias->WithNativeResource(native);
  Registry().Register(Resource());
  EXPECT_EQ(Registry().RegisterManaged(alias).error(),
    RegistrationError::kOwnershipConflict);
  Registry().UnRegisterResource(*Resource());
  auto lease = Registry().RegisterManaged(Resource());
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
  Resource()->WithNativeResource(native);
  auto alias = std::make_shared<FakeResource>();
  alias->WithNativeResource(native);
  unsigned forgotten = 0;
  Registry().SetResourceUnregisteredCallback(
    [&](const auto&) -> auto { ++forgotten; });
  Registry().Register(Resource());
  Registry().Register(alias);
  Registry().UnRegisterResource(*Resource());
  EXPECT_EQ(forgotten, 0U);
  Registry().UnRegisterResource(*alias);
  EXPECT_EQ(forgotten, 1U);
  Registry().SetResourceUnregisteredCallback({});
}

NOLINT_TEST_F(
  ManagedRegistryTest, EveryRawViewMutationRejectsBeforeNativeCreation)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  const auto desc = Description();
  auto view = Registry().AcquireManagedView<FakeResource>(*lease, desc);
  ASSERT_TRUE(view);
  const auto managed_index = LastViewIndex();
  const auto native_calls = Resource()->CallCount();
  EXPECT_THROW(Registry().UnRegisterViews(*Resource()), std::logic_error);
  EXPECT_THROW(
    Registry().UnRegisterView(*Resource(), view->view), std::logic_error);
  const std::array views { view->view };
  EXPECT_THROW(
    Registry().UnRegisterViews(*Resource(), views), std::logic_error);
  auto manual = std::make_shared<FakeResource>();
  Registry().Register(manual);
  EXPECT_THROW((void)Registry().UpdateView(*manual, managed_index, desc),
    std::logic_error);
  EXPECT_THROW((void)Registry().UpdateView(*Resource(), managed_index, desc),
    std::logic_error);
  EXPECT_THROW(
    Registry().Replace(*Resource(), manual, nullptr), std::logic_error);
  EXPECT_THROW(
    Registry().Replace(*manual, Resource(), nullptr), std::logic_error);
  for (int operation = 0; operation < 3; ++operation) {
    auto handle = Allocator().AllocateRaw(desc.view_type, desc.visibility);
    if (operation == 0) {
      EXPECT_THROW(
        (void)Registry().RegisterView(*Resource(), std::move(handle), desc),
        std::logic_error);
    } else if (operation == 1) {
      EXPECT_THROW((void)Registry().RegisterView(
                     *Resource(), view->view, std::move(handle), desc),
        std::logic_error);
    } else {
      EXPECT_THROW((void)Registry().AcquireViewRegistration(
                     *Resource(), std::move(handle), desc),
        std::logic_error);
    }
  }
  EXPECT_EQ(Resource()->CallCount(), native_calls);
  EXPECT_EQ(Registry().Find(*Resource(), desc), view->view);
}

NOLINT_TEST_F(ManagedRegistryTest,
  EqualViewsPublishOnceAcrossThreadsAndHashCollisionsStayDistinct)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  constexpr std::uint64_t kFirstCollidingId = 11U;
  constexpr std::uint64_t kSecondCollidingId = 22U;
  auto a = Description(kFirstCollidingId);
  auto b = Description(kSecondCollidingId);
  a.force_hash_collision = b.force_hash_collision = true;
  std::barrier start(3);
  std::array<NativeView, 2> observed;
  std::array<std::jthread, 2> workers;
  for (size_t index = 0; index < workers.size(); ++index) {
    workers.at(index) = std::jthread([&, index] -> void {
      start.arrive_and_wait();
      const auto result
        = Registry().AcquireManagedView<FakeResource>(*lease, a);
      if (result) {
        observed.at(index) = result->view;
      }
    });
  }
  start.arrive_and_wait();
  for (auto& worker : workers) {
    worker.join();
  }
  EXPECT_TRUE(observed.at(0)->IsValid());
  EXPECT_EQ(observed.at(0), observed.at(1));
  EXPECT_EQ(Resource()->CallCount(), 1U);
  const auto distinct = Registry().AcquireManagedView<FakeResource>(*lease, b);
  ASSERT_TRUE(distinct);
  EXPECT_NE(distinct->view, observed.at(0));
  EXPECT_EQ(Registry().AcquireManagedView<FakeResource>(*lease, a)->view,
    observed.at(0));
  EXPECT_EQ(Resource()->CallCount(), 2U);
}

NOLINT_TEST_F(
  ManagedRegistryTest, FailedNativeViewLeavesExistingPublishedViewsIntact)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  const auto original
    = Registry().AcquireManagedView<FakeResource>(*lease, Description(1));
  ASSERT_TRUE(original);
  Resource()->WithThrowingView(2);
  const auto failed
    = Registry().AcquireManagedView<FakeResource>(*lease, Description(2));
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), RegistrationError::kAllocationFailed);
  EXPECT_EQ(
    Registry().AcquireManagedView<FakeResource>(*lease, Description(1))->view,
    original->view);
  EXPECT_FALSE(Registry().Contains(*Resource(), Description(2)));
}

NOLINT_TEST_F(ManagedRegistryTest, WorkerReleaseQueuesOwnerThreadRetirement)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  std::jthread worker(
    [](RegistrationLease owner) -> void { owner = RegistrationLease {}; },
    std::move(*lease));
  worker.join();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 1U);
  Registry().PollManagedRetirements();
  EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
}

NOLINT_TEST_F(
  ManagedRegistryTest, ClosedRegistryAllowsLateCpuCleanupWithoutFacadeOrFrame)
{
  auto lease = Registry().RegisterManaged(Resource());
  ASSERT_TRUE(lease);
  ASSERT_TRUE(
    Registry().AcquireManagedView<FakeResource>(*lease, Description()));
  auto use = Registry().RetainUse(*lease);
  ASSERT_TRUE(use);
  const std::weak_ptr<FakeResource> weak_resource = Resource();
  *lease = RegistrationLease {};
  Resource().reset();
  GraphicsOwner()->Close();
  GraphicsOwner().reset();
  EXPECT_FALSE(weak_resource.expired());
  std::jthread worker(
    [](RegistrationUse pin) -> void { pin = RegistrationUse {}; },
    std::move(*use));
  worker.join();
  EXPECT_TRUE(weak_resource.expired());
}
} // namespace

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(ManagedRegistryTest,
  AllocationFailureRollsBackRegistrationWithoutLosingExistingOwner)
{
  using oxygen::graphics::testing::HeapAllocationFailure;
  auto existing = Registry().RegisterManaged(Resource());
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
  auto lease = Registry().RegisterManaged(Resource());
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
      EXPECT_FALSE(Registry().Contains(*Resource(), Description(2)));
      EXPECT_EQ(Allocator()
                  .GetAllocatedDescriptorsCount(
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
  auto lease = Registry().RegisterManaged(Resource());
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

NOLINT_TEST_F(
  ManagedRegistryTest, InitialTextureViewsPreserveDomainsAndSubresources)
{
  auto texture = std::make_shared<ManagedTestTexture>();
  const auto requests = InitialTextureViews();
  auto bundle = Registry().RegisterManagedTexture(texture, requests);
  ASSERT_TRUE(bundle);
  ASSERT_EQ(bundle->views.size(), requests.size());
  EXPECT_EQ(bundle->resource, texture);
  auto lease = Registry().AcquireManaged(bundle->registration.Identity());
  ASSERT_TRUE(lease);
  for (std::size_t i = 0; i < requests.size(); ++i) {
    auto cached = Registry().AcquireManagedView<Texture>(
      *lease, requests.at(i).description, requests.at(i).domain);
    ASSERT_TRUE(cached);
    EXPECT_EQ(
      cached->shader_visible_index, bundle->views.at(i).shader_visible_index);
    EXPECT_EQ(texture->created.at(i), requests.at(i).description);
  }
  EXPECT_NE(bundle->views.at(1).shader_visible_index,
    bundle->views.at(2).shader_visible_index);
  auto duplicate = Registry().RegisterManagedTexture(texture, requests);
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error(), RegistrationError::kOwnershipConflict);
}

NOLINT_TEST_F(ManagedRegistryTest, InitialViewFailuresUnwindAndPermitRetry)
{
  const auto requests = InitialTextureViews();
  for (std::size_t index = 0; index < requests.size(); ++index) {
    auto texture = std::make_shared<ManagedTestTexture>();
    texture->fail_view = index;
    auto failed = Registry().RegisterManagedTexture(texture, requests);
    ASSERT_FALSE(failed) << index;
    Registry().PollManagedRetirements();
    EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
    texture->fail_view.reset();
    texture->created.clear();
    {
      auto retry = Registry().RegisterManagedTexture(texture, requests);
      ASSERT_TRUE(retry) << index;
      EXPECT_EQ(retry->views.size(), requests.size());
    }
    Registry().PollManagedRetirements();
    EXPECT_EQ(Registry().GetRegisteredResourceCount(), 0U);
  }
}

NOLINT_TEST_F(ManagedRegistryTest, ConcurrentInitializersCannotShareAClaim)
{
  auto texture = std::make_shared<ManagedTestTexture>();
  const auto requests = InitialTextureViews();
  std::array<std::expected<ManagedTexture, RegistrationError>, 2> results {
    std::unexpected(RegistrationError::kStaleRegistration),
    std::unexpected(RegistrationError::kStaleRegistration),
  };
  std::barrier start(2);
  const auto initialize = [&](std::size_t index) -> void {
    start.arrive_and_wait();
    results.at(index) = Registry().RegisterManagedTexture(texture, requests);
  };
  std::jthread first(initialize, 0U);
  std::jthread second(initialize, 1U);
  first.join();
  second.join();
  ASSERT_NE(results.at(0).has_value(), results.at(1).has_value());
  const auto& rejected
    = results.at(0).has_value() ? results.at(1) : results.at(0);
  EXPECT_EQ(rejected.error(), RegistrationError::kOwnershipConflict);
  EXPECT_EQ(texture->created.size(), requests.size());
}

NOLINT_TEST_F(ManagedRegistryTest, InitialBundleDoesNotRetainBackendFacade)
{
  auto texture = std::make_shared<ManagedTestTexture>();
  auto bundle = Registry().RegisterManagedTexture(texture);
  ASSERT_TRUE(bundle);
  std::weak_ptr<MockGraphics> facade = GraphicsOwner();
  GraphicsOwner().reset();
  EXPECT_TRUE(facade.expired());
  EXPECT_EQ(bundle->resource, texture);
}

NOLINT_TEST_F(ManagedRegistryTest, InitialBundleRejectsNullAndClosedRegistry)
{
  auto missing = Registry().RegisterManagedTexture(nullptr);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(), RegistrationError::kStaleRegistration);
  auto texture = std::make_shared<ManagedTestTexture>();
  Registry().Close();
  auto closed = Registry().RegisterManagedTexture(texture);
  ASSERT_FALSE(closed);
  EXPECT_EQ(closed.error(), RegistrationError::kClosed);
}
