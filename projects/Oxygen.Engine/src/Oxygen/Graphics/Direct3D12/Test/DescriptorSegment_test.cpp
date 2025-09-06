//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <d3d12.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorSegment.h>

#include "./Mocks/MockDescriptorHeap.h"
#include "./Mocks/MockDevice.h"

using oxygen::graphics::DescriptorHandle;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::d3d12::DescriptorSegment;

using oxygen::graphics::d3d12::testing::MockDescriptorHeap;
using oxygen::graphics::d3d12::testing::MockDevice;

using testing::DoAll;
using testing::Return;
using testing::SetArgPointee;

namespace b = oxygen::bindless;

// Helper: FakeDescriptorHandle for testing
class FakeDescriptorHandle : public DescriptorHandle {
public:
  explicit FakeDescriptorHandle(const b::Handle index,
    const ResourceViewType view_type = ResourceViewType::kNone,
    const DescriptorVisibility visibility = DescriptorVisibility::kNone)
    : DescriptorHandle(index, view_type, visibility)
  {
  }
};

class GoodHeapTest : public testing::Test {
protected:
  MockDevice device_;
  MockDescriptorHeap heap_;

  auto SetUp() -> void override
  {
    using testing::_;
    ON_CALL(device_, CreateDescriptorHeap(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(&heap_), Return(S_OK)));
  }
};

class NoHeapTest : public testing::Test {
protected:
  MockDevice device_;

  auto SetUp() -> void override
  {
    using testing::_;
    // Simulate failure to create heap by returning an error code.
    ON_CALL(device_, CreateDescriptorHeap(_, _, _))
      .WillByDefault(DoAll(SetArgPointee<2>(nullptr), Return(E_OUTOFMEMORY)));
  }
};

NOLINT_TEST_F(GoodHeapTest, ConstructorCreatesHeapAndSetsHandles)
{
  using testing::_;

  // Setup heap desc
  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 8,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    .NodeMask = 0,
  };

  // Setup handles
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 1234 };
  constexpr D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = { 5678 };

  // Expect CreateDescriptorHeap to be called
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);

  // Mock device methods
  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc.Type))
    .WillOnce(Return(32));

  // Mock heap methods
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetGPUDescriptorHandleForHeapStart())
    .WillOnce(Return(gpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc));

  // Construct DescriptorSegment
  const DescriptorSegment segment(&device_,
    b::Capacity { heap_desc.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);

  // Validate IsShaderVisible
  EXPECT_TRUE(segment.IsShaderVisible());
  // Validate heap pointer
  EXPECT_EQ(segment.GetHeap(), &heap_);
  // Validate heap type
  EXPECT_EQ(segment.GetHeapType(), heap_desc.Type);

  // Validate CPU handle
  const FakeDescriptorHandle handle(b::Handle { 0 },
    ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible); // index 0
  EXPECT_EQ(segment.GetCpuHandle(handle).ptr, cpu_handle.ptr);

  // Validate GPU handle
  EXPECT_EQ(segment.GetGpuHandle(handle).ptr, gpu_handle.ptr);
}

NOLINT_TEST_F(GoodHeapTest, ConstructorWithDebugNameSetsDebugName)
{
  using testing::_;

  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    .NodeMask = 0,
  };

  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 1111 };
  constexpr D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = { 2222 };

  const auto* debug_name = "TestHeap";

  // Expect CreateDescriptorHeap to be called
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);

  EXPECT_CALL(heap_, SetPrivateData(_, _, _))
    .WillOnce([&](const GUID& /*guid*/, UINT /*size*/, const void* data) {
      // Check that the data is a wide string matching expectedWName
      const auto name_data = static_cast<const char*>(data);
      EXPECT_THAT(name_data, ::testing::StrEq(debug_name));
      return S_OK;
    });

  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc.Type))
    .WillOnce(Return(16));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetGPUDescriptorHandleForHeapStart())
    .WillOnce(Return(gpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc));

  DescriptorSegment segment(&device_, b::Capacity { heap_desc.NumDescriptors },
    b::Handle(0), ResourceViewType::kTexture_SRV,
    DescriptorVisibility::kShaderVisible, "TestHeap");
  // Only test that construction with debug name does not throw or crash.
  // There is no way to query the debug name from the heap mock.
}

NOLINT_TEST_F(GoodHeapTest, GetGpuHandleThrowsIfNotShaderVisible)
{
  using testing::_;

  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 4,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };

  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 3333 };

  // Expect CreateDescriptorHeap to be called
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);

  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc.Type))
    .WillOnce(Return(8));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc));

  const DescriptorSegment segment(&device_,
    b::Capacity { heap_desc.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
  const FakeDescriptorHandle handle(b::Handle { 0 });
  NOLINT_EXPECT_THROW([[maybe_unused]] auto gh = segment.GetGpuHandle(handle),
    std::runtime_error);
}

NOLINT_TEST_F(NoHeapTest, ConstructorThrowsWhenHeapAllocationFails)
{
  using testing::_;

  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);

  NOLINT_EXPECT_THROW(
    DescriptorSegment(&device_, b::Capacity { 4 }, b::Handle(0),
      ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible),
    std::runtime_error);
}

NOLINT_TEST_F(NoHeapTest, ConstructorWithDebugNameThrowsWhenHeapAllocationFails)
{
  using testing::_;

  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);

  NOLINT_EXPECT_THROW(DescriptorSegment(&device_, b::Capacity { 8 },
                        b::Handle(0), ResourceViewType::kTexture_SRV,
                        DescriptorVisibility::kShaderVisible, "DebugNameTest"),
    std::runtime_error);
}

NOLINT_TEST_F(GoodHeapTest, GetGpuDescriptorTableStartThrowsIfNotShaderVisible)
{
  using testing::_;

  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 4,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 4444 };
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);
  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc.Type))
    .WillOnce(Return(8));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc));
  const DescriptorSegment segment(&device_,
    b::Capacity { heap_desc.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
  NOLINT_EXPECT_THROW(
    [[maybe_unused]] auto h = segment.GetGpuDescriptorTableStart(),
    std::runtime_error);
}

NOLINT_TEST_F(GoodHeapTest, GetCpuDescriptorTableStartReturnsCpuHandle)
{
  using testing::_;

  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 4,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 5555 };
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);
  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc.Type))
    .WillOnce(Return(8));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc));
  const DescriptorSegment segment(&device_,
    b::Capacity { heap_desc.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
  EXPECT_EQ(segment.GetCpuDescriptorTableStart().ptr, cpu_handle.ptr);
}

NOLINT_TEST_F(GoodHeapTest, IsShaderVisibleReflectsHeapFlags)
{
  using testing::_;

  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc_visible = {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    .NodeMask = 0,
  };
  constexpr D3D12_DESCRIPTOR_HEAP_DESC heap_desc_cpu = {
    .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    .NumDescriptors = 2,
    .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    .NodeMask = 0,
  };
  constexpr D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = { 6666 };
  constexpr D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = { 7777 };
  // Shader visible
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);
  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc_visible.Type))
    .WillOnce(Return(8));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetGPUDescriptorHandleForHeapStart())
    .WillOnce(Return(gpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc_visible));
  const DescriptorSegment segment_visible(&device_,
    b::Capacity { heap_desc_visible.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  EXPECT_TRUE(segment_visible.IsShaderVisible());
  // CPU only
  EXPECT_CALL(device_, CreateDescriptorHeap(_, _, _)).Times(1);
  EXPECT_CALL(device_, GetDescriptorHandleIncrementSize(heap_desc_cpu.Type))
    .WillOnce(Return(8));
  EXPECT_CALL(heap_, GetCPUDescriptorHandleForHeapStart())
    .WillOnce(Return(cpu_handle));
  EXPECT_CALL(heap_, GetDesc()).WillRepeatedly(Return(heap_desc_cpu));
  const DescriptorSegment segment_cpu(&device_,
    b::Capacity { heap_desc_cpu.NumDescriptors }, b::Handle(0),
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kCpuOnly);
  EXPECT_FALSE(segment_cpu.IsShaderVisible());
}
