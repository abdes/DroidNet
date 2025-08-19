//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Detail/BaseDescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Test/Bindless/Mocks/MockDescriptorHeapSegment.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::bindless::testing::MockDescriptorHeapSegment;
using oxygen::graphics::d3d12::D3D12HeapAllocationStrategy;
using oxygen::graphics::detail::DescriptorHeapSegment;
namespace dx = oxygen::graphics::d3d12::dx;

using oxygen::kInvalidBindlessHandle;
namespace b = oxygen::bindless;

namespace {

// Minimal test allocator that uses BaseDescriptorAllocator with a D3D12
// strategy
class TestD3D12Allocator
  : public oxygen::graphics::detail::BaseDescriptorAllocator {
public:
  using Base = oxygen::graphics::detail::BaseDescriptorAllocator;

  explicit TestD3D12Allocator(dx::IDevice* device = nullptr)
    : Base(std::make_shared<D3D12HeapAllocationStrategy>(device))
  {
  }

  explicit TestD3D12Allocator(
    std::shared_ptr<const oxygen::graphics::DescriptorAllocationStrategy>
      strategy)
    : Base(std::move(strategy))
  {
  }

  void CopyDescriptor(const DescriptorHandle&, const DescriptorHandle&) override
  {
    // Not needed for these tests
  }

protected:
  auto CreateHeapSegment(const b::Capacity capacity, const b::Handle base_index,
    const ResourceViewType view_type, const DescriptorVisibility visibility)
    -> std::unique_ptr<DescriptorHeapSegment> override
  {
    // Use a simple mock heap segment from D3D12 tests
    auto seg
      = std::make_unique<::testing::NiceMock<MockDescriptorHeapSegment>>();
    using ::testing::Return;
    EXPECT_CALL(*seg, GetViewType()).WillRepeatedly(Return(view_type));
    EXPECT_CALL(*seg, GetVisibility()).WillRepeatedly(Return(visibility));
    EXPECT_CALL(*seg, GetBaseIndex()).WillRepeatedly(Return(base_index));
    EXPECT_CALL(*seg, GetCapacity()).WillRepeatedly(Return(capacity));
    EXPECT_CALL(*seg, GetAllocatedCount())
      .WillRepeatedly(Return(b::Count { 0 }));
    EXPECT_CALL(*seg, GetAvailableCount())
      .WillRepeatedly(Return(b::Count { capacity.get() }));
    EXPECT_CALL(*seg, Allocate()).WillOnce(Return(base_index));
    EXPECT_CALL(*seg, Release(base_index)).WillOnce(Return(true));
    return seg;
  }
};

// -------------------- Tests --------------------

NOLINT_TEST(D3D12DomainTest, GetDomainBaseIndexMatchesStrategy)
{
  TestD3D12Allocator alloc { nullptr };
  auto strat = std::make_shared<D3D12HeapAllocationStrategy>(nullptr);

  const std::pair<ResourceViewType, DescriptorVisibility> domains[] = {
    { ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible },
    { ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly },
    { ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly },
  };

  for (const auto& [type, vis] : domains) {
    const auto base_allocator = alloc.GetDomainBaseIndex(type, vis);
    const auto base_strategy = strat->GetHeapBaseIndex(type, vis);
    EXPECT_EQ(base_allocator, base_strategy);
  }
}

NOLINT_TEST(D3D12DomainTest, ReserveWithinCapacityAndAllocate)
{
  TestD3D12Allocator alloc { nullptr };

  // CBV_SRV_UAV shader-visible should allow reservation > 0
  constexpr auto kType = ResourceViewType::kTexture_SRV;
  constexpr auto kVis = DescriptorVisibility::kShaderVisible;
  const auto reserved = alloc.Reserve(kType, kVis, b::Count { 1 });
  ASSERT_TRUE(reserved.has_value());
  auto handle = alloc.Allocate(kType, kVis);
  EXPECT_TRUE(handle.IsValid());
  EXPECT_EQ(handle.GetIndex(), reserved.value());
  alloc.Release(handle);
  EXPECT_FALSE(handle.IsValid());
}

NOLINT_TEST(D3D12DomainTest, RTVAndDSVShaderVisibleReservationFails)
{
  TestD3D12Allocator alloc { nullptr };
  EXPECT_FALSE(alloc
      .Reserve(ResourceViewType::kTexture_RTV,
        DescriptorVisibility::kShaderVisible, b::Count { 1 })
      .has_value());
  EXPECT_FALSE(alloc
      .Reserve(ResourceViewType::kTexture_DSV,
        DescriptorVisibility::kShaderVisible, b::Count { 1 })
      .has_value());
}

NOLINT_TEST(D3D12DomainTest, DomainBaseIndices_UniqueAcrossGpuVisibleHeaps)
{
  TestD3D12Allocator alloc { nullptr };

  const auto base_cbv_srv_uav_gpu = alloc.GetDomainBaseIndex(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto base_sampler_gpu = alloc.GetDomainBaseIndex(
    ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible);

  EXPECT_NE(base_cbv_srv_uav_gpu, kInvalidBindlessHandle);
  EXPECT_NE(base_sampler_gpu, kInvalidBindlessHandle);
  EXPECT_NE(base_cbv_srv_uav_gpu, base_sampler_gpu)
    << "Two GPU-visible domains must not share the same base index";
}

NOLINT_TEST(D3D12DomainTest, DomainBaseIndices_CpuOnlyValidAndDeterministic)
{
  TestD3D12Allocator alloc { nullptr };

  const std::pair<ResourceViewType, DescriptorVisibility> cpu_domains[] = {
    { ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly },
    { ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly },
    { ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly },
    { ResourceViewType::kSampler, DescriptorVisibility::kCpuOnly },
  };

  for (const auto& [type, vis] : cpu_domains) {
    const auto b1 = alloc.GetDomainBaseIndex(type, vis);
    const auto b2 = alloc.GetDomainBaseIndex(type, vis);
    EXPECT_NE(b1, kInvalidBindlessHandle);
    EXPECT_EQ(b1, b2) << "Base index must be stable for the same domain";
  }
}

// Provider test: ensure custom JSON base_index is honored
NOLINT_TEST(D3D12DomainTest, ProviderConfiguredBaseIndexHonored)
{
  // Minimal JSON overriding two heaps' base_index values
  const char* kJson = R"JSON(
  {
    "heaps": {
      "CBV_SRV_UAV:gpu": {
        "capacity": 10,
        "shader_visible": true,
        "allow_growth": false,
        "growth_factor": 0.0,
        "max_growth_iterations": 0,
        "base_index": 12345
      },
      "SAMPLER:gpu": {
        "capacity": 8,
        "shader_visible": true,
        "allow_growth": false,
        "growth_factor": 0.0,
        "max_growth_iterations": 0,
        "base_index": 20000
      }
    }
  }
)JSON";

  struct TestProvider final : D3D12HeapAllocationStrategy::ConfigProvider {
    explicit TestProvider(std::string j)
      : json(std::move(j))
    {
    }
    [[nodiscard]] std::string_view GetJson() const noexcept override
    {
      return json;
    }
    std::string json;
  } provider { std::string { kJson } };

  // Construct strategy with custom provider
  D3D12HeapAllocationStrategy strat { nullptr, provider };

  // Verify the strategy reports the configured base indices
  EXPECT_EQ(strat.GetHeapBaseIndex(ResourceViewType::kTexture_SRV,
              DescriptorVisibility::kShaderVisible),
    b::Handle { 12345u });
  EXPECT_EQ(strat.GetHeapBaseIndex(
              ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible),
    b::Handle { 20000u });

  // Verify allocator exposes the same values via GetDomainBaseIndex
  auto strat_ptr
    = std::make_shared<D3D12HeapAllocationStrategy>(nullptr, provider);
  TestD3D12Allocator alloc { strat_ptr };
  EXPECT_EQ(alloc.GetDomainBaseIndex(ResourceViewType::kTexture_SRV,
              DescriptorVisibility::kShaderVisible),
    b::Handle { 12345u });
  EXPECT_EQ(alloc.GetDomainBaseIndex(
              ResourceViewType::kSampler, DescriptorVisibility::kShaderVisible),
    b::Handle { 20000u });
}

NOLINT_TEST(D3D12DomainTest, ReserveExceedingCapacityFails)
{
  TestD3D12Allocator alloc { nullptr };

  // Query capacity via the strategy to craft an over-capacity request.
  D3D12HeapAllocationStrategy strat { nullptr };
  const auto& key_cbv = strat.GetHeapKey(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  const auto cap = strat.GetHeapDescription(key_cbv).shader_visible_capacity;

  // Request more than capacity
  const auto reserved = alloc.Reserve(ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible, b::Count { cap.get() + 1 });
  EXPECT_FALSE(reserved.has_value());
}

} // namespace
