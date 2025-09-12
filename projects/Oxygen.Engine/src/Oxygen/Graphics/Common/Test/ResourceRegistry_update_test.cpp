//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Composition/Object.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/FixedDescriptorSegment.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Test/Fakes/FakeResource.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::NativeView;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::MockDescriptorAllocator;
using oxygen::graphics::detail::FixedDescriptorSegment;
using oxygen::graphics::testing::FakeResource;
using oxygen::graphics::testing::TestViewDesc;

namespace {

//===----------------------------------------------------------------------===//
// UpdateView Tests
//===----------------------------------------------------------------------===//

// ReSharper disable once CppRedundantQualifier
class ResourceRegistryUpdateViewTest : public ::testing::Test {
protected:
  std::shared_ptr<MockDescriptorAllocator> allocator_;
  FixedDescriptorSegment* last_segment_ { nullptr };

  std::unique_ptr<ResourceRegistry> registry_;

  std::shared_ptr<FakeResource> resource1_;
  std::shared_ptr<FakeResource> resource2_;

  auto SetUp() -> void override
  {
    // Arrange allocator to create deterministic segments and capture pointer
    allocator_ = std::make_shared<testing::NiceMock<MockDescriptorAllocator>>();
    allocator_->ext_segment_factory_ = [this](auto capacity, auto base_index,
                                         auto view_type, auto visibility) {
      auto seg = std::make_unique<FixedDescriptorSegment>(
        capacity, base_index, view_type, visibility);
      last_segment_ = seg.get();
      return seg;
    };

    registry_ = std::make_unique<ResourceRegistry>("UpdateView Test Registry");
    resource1_ = std::make_shared<FakeResource>();
    resource2_ = std::make_shared<FakeResource>();
    registry_->Register(resource1_);
    registry_->Register(resource2_);
  }

  auto TearDown() -> void override
  {
    if (registry_) {
      if (resource1_) {
        registry_->UnRegisterResource(*resource1_);
      }
      if (resource2_) {
        registry_->UnRegisterResource(*resource2_);
      }
    }
    resource1_.reset();
    resource2_.reset();
    registry_.reset();
    allocator_.reset();
  }

  struct RegisteredViewInfo {
    NativeView view;
    decltype(std::declval<DescriptorHandle>().GetBindlessHandle()) index;
  };

  // Helper: register a view and return both the native view and bindless index
  auto RegisterViewGetIndex(FakeResource& resource,
    const TestViewDesc& desc) const -> RegisteredViewInfo
  {
    DescriptorHandle descriptor
      = allocator_->Allocate(desc.view_type, desc.visibility);
    if (!descriptor.IsValid()) {
      ADD_FAILURE() << "failed to allocate descriptor";
      return {};
    }
    const auto index = descriptor.GetBindlessHandle();
    const auto view
      = registry_->RegisterView(resource, std::move(descriptor), desc);
    return RegisteredViewInfo { .view = view, .index = index };
  }
};

/*!\
 Update within same resource using an identical description must succeed, keep
 the bindless index stable, and leave cache/ownership consistent.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_SameResource_SameDesc_StableHandleAndCache)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 10,
  };
  const auto before
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc);
  ASSERT_TRUE(view->IsValid());

  // Act
  const bool updated = registry_->UpdateView(*resource1_, index, desc);

  // Assert
  EXPECT_TRUE(updated);
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  const auto found = registry_->Find(*resource1_, desc);
  EXPECT_TRUE(found->IsValid());
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_GT(after, before)
    << "descriptor count should remain allocated after update";
}

/*!\
 Update within same resource with a new description must switch cached view and
 retain the same bindless index; old description must not be contained.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_SameResource_NewDesc_SwitchesCache_KeepsIndex)
{
  // Arrange
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 21,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 22,
  };
  const auto before = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc1);
  ASSERT_TRUE(view->IsValid());

  // Act
  const bool updated = registry_->UpdateView(*resource1_, index, desc2);

  // Assert
  EXPECT_TRUE(updated);
  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_TRUE(registry_->Contains(*resource1_, desc2));
  const auto after = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  EXPECT_GT(after, before);
}

/*!\
 Transfer ownership to a different resource while keeping the same bindless
 index; destination gains cache entry, source loses it.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_TransferOwnership_DifferentResource_StableIndex)
{
  // Arrange
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 31,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 32,
  };
  const auto before = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc1);
  ASSERT_TRUE(view->IsValid());

  // Act
  const bool updated = registry_->UpdateView(*resource2_, index, desc2);

  // Assert
  EXPECT_TRUE(updated);
  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc2));
  // Old view unregister should be a safe no-op
  NOLINT_EXPECT_NO_THROW(registry_->UnRegisterView(*resource1_, view));
  const auto after = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  EXPECT_GT(after, before);
}

/*!\
 Destination resource not registered: UpdateView must return false and leave
 the original registration intact.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_DestNotRegistered_ReturnsFalse_NoSideEffects)
{
  // Arrange
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 41,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 42,
  };
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc1);
  ASSERT_TRUE(view->IsValid());
  const auto unregistered_dest = std::make_shared<FakeResource>();

  // Act
  const bool updated = registry_->UpdateView(*unregistered_dest, index, desc2);

  // Assert
  EXPECT_FALSE(updated);
  EXPECT_TRUE(registry_->Contains(*resource1_, desc1));
  EXPECT_FALSE(registry_->Contains(*unregistered_dest, desc2));
}

/*!\
 Unknown index: UpdateView must return false without changing registry state.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_UnknownIndex_ReturnsFalse_NoSideEffects)
{
  // Arrange
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 51,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 52,
  };
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc1);
  ASSERT_TRUE(view->IsValid());
  registry_->UnRegisterViews(*resource1_); // frees the index mapping

  // Act
  const bool updated = registry_->UpdateView(*resource1_, index, desc2);

  // Assert
  EXPECT_FALSE(updated);
  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_FALSE(registry_->Contains(*resource1_, desc2));
}

/*!\
 New view invalid: UpdateView must return false, release the descriptor, purge
 old cache, and leave index free.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_NewViewInvalid_ReleasesDescriptor_PurgesOldCache)
{
  // Arrange
  constexpr TestViewDesc desc1 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 61,
  };
  constexpr TestViewDesc desc2 {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 62,
  };
  const auto before = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc1);
  ASSERT_TRUE(view->IsValid());
  // Configure destination to always return invalid view
  resource2_->WithInvalidView();

  // Act
  const bool updated = registry_->UpdateView(*resource2_, index, desc2);

  // Assert
  EXPECT_FALSE(updated);
  EXPECT_FALSE(registry_->Contains(*resource1_, desc1));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc2));
  const auto after = allocator_->GetAllocatedDescriptorsCount(
    desc1.view_type, desc1.visibility);
  EXPECT_EQ(after, before) << "descriptor must be released on failure";
}

/*!\
 New view creation throws: UpdateView should propagate exception, release the
 owned descriptor, purge old cache, and leave index free.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_NewViewThrows_ReleasesDescriptorAndPurgesCache)
{
  // Arrange
  constexpr TestViewDesc desc_throw {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 71,
  };
  const auto before = allocator_->GetAllocatedDescriptorsCount(
    desc_throw.view_type, desc_throw.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc_throw);
  ASSERT_TRUE(view->IsValid());
  // Configure destination to throw on the same id
  resource2_->WithThrowingView(desc_throw.id);

  // Act + Assert
  EXPECT_THROW((void)registry_->UpdateView(*resource2_, index, desc_throw),
    std::runtime_error);

  // Post-conditions: no leaks; old cache purged; index free
  const auto after = allocator_->GetAllocatedDescriptorsCount(
    desc_throw.view_type, desc_throw.visibility);
  EXPECT_EQ(after, before) << "descriptor must be released on exception";
  EXPECT_FALSE(registry_->Contains(*resource1_, desc_throw));
  EXPECT_FALSE(registry_->Contains(*resource2_, desc_throw));
}

/*!\
 Repeated updates with identical description must be idempotent and keep the
 descriptor allocation stable.
*/
NOLINT_TEST_F(
  ResourceRegistryUpdateViewTest, Update_RepeatedSameUpdate_Idempotent)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 81,
  };
  const auto before
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc);
  ASSERT_TRUE(view->IsValid());

  // Act
  const bool updated1 = registry_->UpdateView(*resource1_, index, desc);
  const bool updated2 = registry_->UpdateView(*resource1_, index, desc);

  // Assert
  EXPECT_TRUE(updated1);
  EXPECT_TRUE(updated2);
  EXPECT_TRUE(registry_->Contains(*resource1_, desc));
  const auto after
    = allocator_->GetAllocatedDescriptorsCount(desc.view_type, desc.visibility);
  EXPECT_GT(after, before);
}

/*!\
 Transfer to a different resource with the same description: source cache is
 purged and destination gains the view at the same index.
*/
NOLINT_TEST_F(ResourceRegistryUpdateViewTest,
  Update_TransferSameDesc_PurgesOldOwnerCache_AddsNew)
{
  // Arrange
  constexpr TestViewDesc desc {
    .view_type = ResourceViewType::kConstantBuffer,
    .visibility = DescriptorVisibility::kShaderVisible,
    .id = 91,
  };
  const auto [view, index] = RegisterViewGetIndex(*resource1_, desc);
  ASSERT_TRUE(view->IsValid());

  // Act
  const bool updated = registry_->UpdateView(*resource2_, index, desc);

  // Assert
  EXPECT_TRUE(updated);
  EXPECT_FALSE(registry_->Contains(*resource1_, desc));
  EXPECT_TRUE(registry_->Contains(*resource2_, desc));
}

} // namespace
