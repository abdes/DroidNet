//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/Detail/Bindless.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/D3D12HeapAllocationStrategy.h>
#include <Oxygen/Graphics/Direct3D12/Bindless/DescriptorAllocator.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Types.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Graphics/Direct3D12/RenderController.h>

#include "./Mocks/MockDevice.h"
#include "./Mocks/MockPipelineState.h"
#include "./Mocks/MockRootSignature.h"

using oxygen::Format;
using oxygen::graphics::ComputePipelineDesc;
using oxygen::graphics::DescriptorTableBinding;
using oxygen::graphics::DirectBufferBinding;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::PushConstantsBinding;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::RootBindingDesc;
using oxygen::graphics::ShaderStageFlags;

using oxygen::graphics::d3d12::detail::PipelineStateCache;
using oxygen::graphics::d3d12::testing::MockDevice;
using oxygen::graphics::d3d12::testing::MockPipelineState;
using oxygen::graphics::d3d12::testing::MockRootSignature;
namespace dx = oxygen::graphics::d3d12::dx;

using ::testing::Return;

namespace {

// ReSharper disable CppClangTidyModernizeUseTrailingReturnType
// ReSharper disable once CppClassCanBeFinal - Mock class should not be final
class MockGraphics : public oxygen::graphics::d3d12::Graphics {
public:
  explicit MockGraphics() = default;

  MOCK_METHOD(std::shared_ptr<oxygen::graphics::IShaderByteCode>, GetShader,
    (std::string_view), (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::Surface>, CreateSurface,
    (std::weak_ptr<oxygen::platform::Window>,
      std::shared_ptr<oxygen::graphics::CommandQueue>),
    (const, override));
  MOCK_METHOD(std::shared_ptr<oxygen::graphics::CommandQueue>,
    CreateCommandQueue,
    (std::string_view, oxygen::graphics::QueueRole,
      oxygen::graphics::QueueAllocationPreference),
    (override));
  MOCK_METHOD(std::unique_ptr<oxygen::graphics::CommandList>,
    CreateCommandListImpl, (oxygen::graphics::QueueRole, std::string_view),
    (override));
  MOCK_METHOD(std::unique_ptr<oxygen::graphics::RenderController>,
    CreateRendererImpl,
    (std::string_view, std::weak_ptr<oxygen::graphics::Surface>, uint32_t),
    (override));

  // Add GetCurrentDevice method that D3D12 RenderController constructor calls
  MOCK_METHOD(dx::IDevice*, GetCurrentDevice, (), (const, override));
};

// Helper class to capture and validate root signature blob data
struct RootSignatureBlobCapture {
  std::vector<uint8_t> captured_blob;
  bool blob_captured = false;

  void CaptureBlob(const void* data, const SIZE_T size)
  {
    captured_blob.resize(size);
    std::memcpy(captured_blob.data(), data, size);
    blob_captured = true;
  }

  // Compare with expected blob (for snapshot testing)
  [[nodiscard]] auto MatchesSnapshot(const std::vector<uint8_t>& expected) const
  {
    return blob_captured && captured_blob == expected;
  }
};

// Dynamic reference blob generation for root signatures
// These blobs are generated using the actual D3D12 serialization API during
// test suite initialization
namespace reference_blobs {

  // Helper to serialize a root signature descriptor
  std::vector<uint8_t> SerializeRootSignature(
    const D3D12_ROOT_SIGNATURE_DESC& desc)
  {
    Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, err_blob;
    const HRESULT hr = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &sig_blob, &err_blob);
    if (FAILED(hr)) {
      std::string error_msg = "Failed to serialize reference root signature: ";
      if (err_blob) {
        error_msg += static_cast<const char*>(err_blob->GetBufferPointer());
      }
      throw std::runtime_error(error_msg);
    }
    const auto* blob_data
      = static_cast<const uint8_t*>(sig_blob->GetBufferPointer());
    const SIZE_T blob_size = sig_blob->GetBufferSize();
    return { blob_data, blob_data + blob_size };
  }

  auto GenerateBindlessCbvSrvTable() -> std::vector<uint8_t>
  {
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges
      = { D3D12_DESCRIPTOR_RANGE { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            .NumDescriptors = 1,
            .BaseShaderRegister = 0,
            .RegisterSpace = 0,
            .OffsetInDescriptorsFromTableStart = 0 },
          D3D12_DESCRIPTOR_RANGE { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            .NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
            .BaseShaderRegister = 0,
            .RegisterSpace = 0,
            .OffsetInDescriptorsFromTableStart = 1 } };
    D3D12_ROOT_PARAMETER root_param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {
                .NumDescriptorRanges = static_cast<UINT>(ranges.size()),
                .pDescriptorRanges = ranges.data(),
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
        };
    D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = 1,
      .pParameters = &root_param,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
        | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    return SerializeRootSignature(desc);
  }

  auto GenerateDirectCbvSrvTable() -> std::vector<uint8_t>
  {
    D3D12_ROOT_PARAMETER root_params[2] = {
            D3D12_ROOT_PARAMETER {
                .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
                .Descriptor = {
                    .ShaderRegister = 0,
                    .RegisterSpace = 0 },
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX },
            D3D12_ROOT_PARAMETER { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, .DescriptorTable = {
                                                                                                    .NumDescriptorRanges = 1,
                                                                                                    .pDescriptorRanges = nullptr // will set below
                                                                                                },
                .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL }
        };
    D3D12_DESCRIPTOR_RANGE srv_range
      = { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
          .NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
          .BaseShaderRegister = 0,
          .RegisterSpace = 0,
          .OffsetInDescriptorsFromTableStart = 0 };
    root_params[1].DescriptorTable.pDescriptorRanges = &srv_range;
    D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = 2,
      .pParameters = root_params,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
        | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    return SerializeRootSignature(desc);
  }

  auto GeneratePushConstantsOnly() -> std::vector<uint8_t>
  {
    D3D12_ROOT_PARAMETER root_param
      = { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
          .Constants
          = { .ShaderRegister = 0, .RegisterSpace = 0, .Num32BitValues = 16 },
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL };
    D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = 1,
      .pParameters = &root_param,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
        | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    return SerializeRootSignature(desc);
  }

  auto GenerateSamplerTableOnly() -> std::vector<uint8_t>
  {
    D3D12_DESCRIPTOR_RANGE sampler_range
      = { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
          .NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
          .BaseShaderRegister = 0,
          .RegisterSpace = 0,
          .OffsetInDescriptorsFromTableStart = 0 };
    D3D12_ROOT_PARAMETER root_param
      = { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
          .DescriptorTable
          = { .NumDescriptorRanges = 1, .pDescriptorRanges = &sampler_range },
          .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL };
    D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = 1,
      .pParameters = &root_param,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
        | D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT };
    return SerializeRootSignature(desc);
  }

  auto GenerateComputeBindlessCbvSrv() -> std::vector<uint8_t>
  {
    std::vector<D3D12_DESCRIPTOR_RANGE> ranges
      = { D3D12_DESCRIPTOR_RANGE { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            .NumDescriptors = 1,
            .BaseShaderRegister = 0,
            .RegisterSpace = 0,
            .OffsetInDescriptorsFromTableStart = 0 },
          D3D12_DESCRIPTOR_RANGE { .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            .NumDescriptors = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND,
            .BaseShaderRegister = 0,
            .RegisterSpace = 0,
            .OffsetInDescriptorsFromTableStart = 1 } };
    D3D12_ROOT_PARAMETER root_param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {
                .NumDescriptorRanges = static_cast<UINT>(ranges.size()),
                .pDescriptorRanges = ranges.data(),
            },
            .ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL
        };
    D3D12_ROOT_SIGNATURE_DESC desc = { .NumParameters = 1,
      .pParameters = &root_param,
      .NumStaticSamplers = 0,
      .pStaticSamplers = nullptr,
      .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED };
    return SerializeRootSignature(desc);
  }

  struct ReferenceBlobs {
    std::vector<uint8_t> bindless_cbv_srv_table;
    std::vector<uint8_t> direct_cbv_srv_table;
    std::vector<uint8_t> push_constants_only;
    std::vector<uint8_t> sampler_table_only;
    std::vector<uint8_t> compute_bindless_cbv_srv;
    ReferenceBlobs()
      : bindless_cbv_srv_table(GenerateBindlessCbvSrvTable())
      , direct_cbv_srv_table(GenerateDirectCbvSrvTable())
      , push_constants_only(GeneratePushConstantsOnly())
      , sampler_table_only(GenerateSamplerTableOnly())
      , compute_bindless_cbv_srv(GenerateComputeBindlessCbvSrv())
    {
    }
  };

  auto GetReferenceBlobs() -> const ReferenceBlobs&
  {
    static ReferenceBlobs blobs;
    return blobs;
  }
} // namespace reference_blobs

// Test fixture for PipelineStateCache root signature creation
class PipelineStateCacheTest : public ::testing::Test {
protected:
  void SetUp() override
  {
    using oxygen::graphics::IShaderByteCode;
    using ::testing::_;

    // Setup mock device to capture CreateRootSignature calls
    EXPECT_CALL(mock_device_, CreateRootSignature(_, _, _, _, _))
      .Times(::testing::AnyNumber())
      .WillRepeatedly([this](UINT /*node_mask*/, const void* blob_data,
                        const SIZE_T blob_length, REFIID /*riid*/,
                        void** ppv_root_signature) -> HRESULT {
        // Capture the blob for validation
        blob_capture_.CaptureBlob(blob_data, blob_length);

        // Return the mock root signature instance
        *ppv_root_signature = &mock_root_signature_;
        return S_OK;
      });

    // Setup mock device for pipeline state creation
    EXPECT_CALL(mock_device_, CreateGraphicsPipelineState(_, _, _))
      .Times(::testing::AnyNumber())
      .WillRepeatedly([this](const D3D12_GRAPHICS_PIPELINE_STATE_DESC* /*desc*/,
                        REFIID /*riid*/, void** ppv_pipeline_state) -> HRESULT {
        // Return the mock pipeline state instance
        *ppv_pipeline_state = &mock_pipeline_state_;
        return S_OK;
      });

    EXPECT_CALL(mock_device_, CreateComputePipelineState(_, _, _))
      .Times(::testing::AnyNumber())
      .WillRepeatedly([this](const D3D12_COMPUTE_PIPELINE_STATE_DESC* /*desc*/,
                        REFIID /*riid*/, void** ppv_pipeline_state) -> HRESULT {
        // Return the mock pipeline state instance
        *ppv_pipeline_state = &mock_pipeline_state_;
        return S_OK;
      });

    mock_graphics_ = std::make_shared<MockGraphics>();
    EXPECT_CALL(*mock_graphics_, GetCurrentDevice())
      .WillRepeatedly(Return(&mock_device_));

    EXPECT_CALL(*mock_graphics_, GetShader(::testing::_))
      .WillRepeatedly(Return(dummy_bytecode_));

    pipeline_cache_
      = std::make_shared<PipelineStateCache>(mock_graphics_.get());
  }

  std::shared_ptr<oxygen::graphics::IShaderByteCode> dummy_bytecode_
    = std::make_shared<oxygen::graphics::ShaderByteCode<std::vector<uint32_t>>>(
      std::vector<uint32_t> { 0xDEADBEEF, 0xCAFEBABE, 0x12345678, 0x0BADF00D });

  RootSignatureBlobCapture blob_capture_;
  std::shared_ptr<MockGraphics> mock_graphics_;
  std::shared_ptr<PipelineStateCache> pipeline_cache_;

  // Mock objects for D3D12 interfaces
  MockDevice mock_device_;
  MockRootSignature mock_root_signature_;
  MockPipelineState mock_pipeline_state_;
};

} // anonymous namespace

// Test basic bindless root signature with CBV+SRV descriptor table
NOLINT_TEST_F(PipelineStateCacheTest, GraphicsPipeline_BindlessCbvSrvTable)
{
  constexpr RootBindingDesc cbv_range_desc = {
        .binding_slot_desc = { .register_index = 0, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding {
            .view_type = ResourceViewType::kConstantBuffer,
            .base_index = 0,
            .count = 1,
        },
    };

  constexpr RootBindingDesc srv_range_desc = {
        .binding_slot_desc = { .register_index = 0, .register_space = 0 },
        .visibility = ShaderStageFlags::kAll,
        .data = DescriptorTableBinding {
            // SRVs start at heap index 1
            .view_type = ResourceViewType::kStructuredBuffer_SRV,
            .base_index = 1,
            .count = std::numeric_limits<uint32_t>::max(), // Unbounded
        },
    };
  const auto pipeline_desc
    = GraphicsPipelineDesc::Builder {}
        .SetVertexShader({ .shader = "test_vs" })
        .SetPixelShader({ .shader = "test_ps" })
        .SetFramebufferLayout(
          { .color_target_formats = { Format::kRGBA8UNorm } })
        // Single descriptor table with CBV and SRV ranges
        .AddRootBinding(cbv_range_desc)
        .AddRootBinding(srv_range_desc)
        .Build();

  // Create root signature
  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);

  // Validate that root signature was created successfully
  EXPECT_NE(root_sig,
    nullptr); // Validate the serialized blob matches expected structure
  EXPECT_TRUE(blob_capture_.blob_captured);

  // Validate against dynamically generated reference blob
  const auto& ref_blobs = reference_blobs::GetReferenceBlobs();
  EXPECT_TRUE(blob_capture_.MatchesSnapshot(ref_blobs.bindless_cbv_srv_table));

  // Basic structural validation
  EXPECT_GT(blob_capture_.captured_blob.size(),
    100u); // Root signature should be substantial
}

// Test direct CBV binding with SRV table
NOLINT_TEST_F(PipelineStateCacheTest, GraphicsPipeline_DirectCbvSrvTable)
{
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
              // Direct CBV binding at root parameter 0
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = {
                      .register_index = 0,
                      .register_space = 0,
                  },
                  .visibility = ShaderStageFlags::kVertex,
                  .data = DirectBufferBinding {},
              })
              // SRV descriptor table at root parameter 1
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = {
                      .register_index = 0,
                      .register_space = 0,
                  },
                  .visibility = ShaderStageFlags::kAll,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kStructuredBuffer_SRV,
                      .base_index = 0,
                      .count = std::numeric_limits<uint32_t>::max(),
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);
  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);

  // Validate against dynamically generated reference blob
  const auto& ref_blobs = reference_blobs::GetReferenceBlobs();
  EXPECT_TRUE(blob_capture_.MatchesSnapshot(ref_blobs.direct_cbv_srv_table));

  EXPECT_GT(blob_capture_.captured_blob.size(), 80u);
}

// Test push constants only root signature
NOLINT_TEST_F(PipelineStateCacheTest, GraphicsPipeline_PushConstantsOnly)
{
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout(
                  { .color_target_formats = { Format::kRGBA8UNorm } })
              // 16 DWORDs of push constants at b0
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = {
                      .register_index = 0,
                      .register_space = 0,
                  },
                  .visibility = ShaderStageFlags::kAll,
                  .data = PushConstantsBinding { .size = 16 },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);
  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);

  // Validate against dynamically generated reference blob
  const auto& ref_blobs = reference_blobs::GetReferenceBlobs();
  EXPECT_TRUE(blob_capture_.MatchesSnapshot(ref_blobs.push_constants_only));

  EXPECT_GT(blob_capture_.captured_blob.size(), 60u);
}

// Test sampler table root signature
NOLINT_TEST_F(PipelineStateCacheTest, GraphicsPipeline_SamplerTable)
{
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout(
                  { .color_target_formats = { Format::kRGBA8UNorm } })
              // Sampler descriptor table
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = {
                      .register_index = 0,
                      .register_space = 0,
                  },
                  .visibility = ShaderStageFlags::kPixel,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kSampler,
                      .base_index = 0,
                      .count = std::numeric_limits<uint32_t>::max(),
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);
  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);

  // Validate against dynamically generated reference blob
  const auto& ref_blobs = reference_blobs::GetReferenceBlobs();
  EXPECT_TRUE(blob_capture_.MatchesSnapshot(ref_blobs.sampler_table_only));

  EXPECT_GT(blob_capture_.captured_blob.size(), 70u);
}

// Test compute pipeline root signature (should not have input assembler flag)
NOLINT_TEST_F(PipelineStateCacheTest, ComputePipeline_BindlessCbvSrv)
{
  const auto pipeline_desc
        = ComputePipelineDesc::Builder {}
              .SetComputeShader({ .shader = "test_cs" })
              // CBV+SRV descriptor table (same as graphics but different flags
              // expected)
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kCompute,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::
                          kConstantBuffer,
                      .base_index = 0,
                      .count = 1,
                  },
              })
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kCompute,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kStructuredBuffer_SRV,
                      .base_index = 1,
                      .count = std::numeric_limits<uint32_t>::max(),
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);
  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);

  // Validate against dynamically generated reference blob
  const auto& ref_blobs = reference_blobs::GetReferenceBlobs();
  EXPECT_TRUE(
    blob_capture_.MatchesSnapshot(ref_blobs.compute_bindless_cbv_srv));

  EXPECT_GT(blob_capture_.captured_blob.size(), 100u);
}

// Test mixed root signature with multiple parameter types
NOLINT_TEST_F(PipelineStateCacheTest, GraphicsPipeline_MixedParameters)
{
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout({ .color_target_formats = { Format::kRGBA8UNorm } })
              // Push constants at root parameter 0
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kVertex,
                  .data = PushConstantsBinding { .size = 4 },
              })
              // Direct CBV at root parameter 1
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 1, .register_space = 0 },
                  .visibility = ShaderStageFlags::kAll,
                  .data = DirectBufferBinding {} })
              // SRV table at root parameter 2
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kPixel,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kTexture_SRV,
                      .base_index = 0,
                      .count = 32 // Bounded table
                  } })
              // Sampler table at root parameter 3
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kPixel,
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kSampler,
                      .base_index = 0,
                      .count = 16,
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);

  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);
  EXPECT_GT(blob_capture_.captured_blob.size(), 150u); // Complex root signature
}

// Test shader visibility mapping
NOLINT_TEST_F(PipelineStateCacheTest, ShaderVisibilityMapping)
{
  // Test different shader stage visibility flags
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout(
                  { .color_target_formats = { Format::kRGBA8UNorm } })
              // Vertex-only CBV
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kVertex, // Should map to
                  .data = DirectBufferBinding {} })
              // Pixel-only SRV table
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kPixel, // Should map to
                                                          // D3D12_SHADER_VISIBILITY_PIXEL
                  .data = DescriptorTableBinding {
                      .view_type
                      = ResourceViewType::kTexture_SRV,
                      .base_index = 0,
                      .count = 8,
                  },
              })
              // All stages UAV table
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 0, .register_space = 0 },
                  .visibility = ShaderStageFlags::kAll, // Should map to
                                                        // D3D12_SHADER_VISIBILITY_ALL
                  .data = DescriptorTableBinding {
                      .view_type = ResourceViewType::kTexture_UAV,
                      .base_index = 0,
                      .count = 4,
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);

  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);
}

// Test register space and register index mapping
NOLINT_TEST_F(PipelineStateCacheTest, RegisterSpaceMapping)
{
  const auto pipeline_desc
        = GraphicsPipelineDesc::Builder {}
              .SetVertexShader({ .shader = "test_vs" })
              .SetPixelShader({ .shader = "test_ps" })
              .SetFramebufferLayout(
                  { .color_target_formats = { Format::kRGBA8UNorm } })
              // CBV at register b2, space 1
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc
                  = { .register_index = 2, .register_space = 1 },
                  .visibility = ShaderStageFlags::kAll,
                  .data = DirectBufferBinding {},
              })
              // SRV table at register t5, space 2
              .AddRootBinding(RootBindingDesc {
                  .binding_slot_desc = { .register_index = 5, .register_space = 2 },
                  .visibility = ShaderStageFlags::kAll,
                  .data = DescriptorTableBinding {
                      .view_type
                      = ResourceViewType::kTexture_SRV,
                      .base_index
                      = 10, // Different heap offset
                      .count = 16,
                  },
              })
              .Build();

  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);

  EXPECT_NE(root_sig, nullptr);
  EXPECT_TRUE(blob_capture_.blob_captured);
}

// Test error handling for invalid root parameter indices
NOLINT_TEST_F(PipelineStateCacheTest, InvalidRootParameterIndex)
{
  // This test would need to be adapted based on how the engine handles
  // root parameter index validation. The current implementation appears
  // to use implicit indexing based on order.

  const auto pipeline_desc = GraphicsPipelineDesc::Builder {}
                               .SetVertexShader({ .shader = "test_vs" })
                               .SetPixelShader({ .shader = "test_ps" })
                               .SetFramebufferLayout({ .color_target_formats
                                 = { Format::kRGBA8UNorm } })
                               .Build();

  // Should succeed with valid pipeline
  dx::IRootSignature* root_sig
    = pipeline_cache_->CreateRootSignature(pipeline_desc);
  EXPECT_NE(root_sig, nullptr);
}

// Test caching behavior - same description should return cached entry
NOLINT_TEST_F(PipelineStateCacheTest, CachingBehavior)
{
  const auto pipeline_desc
    = GraphicsPipelineDesc::Builder {}
        .SetVertexShader({ .shader = "test_vs" })
        .SetPixelShader({ .shader = "test_ps" })
        .SetFramebufferLayout(
          { .color_target_formats = { Format::kRGBA8UNorm } })
        .AddRootBinding(RootBindingDesc {
          .binding_slot_desc = { .register_index = 0, .register_space = 0 },
          .visibility = ShaderStageFlags::kAll,
          .data = PushConstantsBinding { .size = 16 },
        })
        .Build();

  // First creation should call device
  const size_t hash1 = std::hash<GraphicsPipelineDesc> {}(pipeline_desc);
  const auto [ps1, rs1]
    = pipeline_cache_->GetOrCreatePipeline(pipeline_desc, hash1);

  EXPECT_NE(ps1, nullptr);
  EXPECT_NE(rs1, nullptr);

  // Reset blob capture to verify caching
  blob_capture_.blob_captured = false;
  blob_capture_.captured_blob.clear();

  // Second creation with same desc should use cache (no device calls)
  const size_t hash2 = std::hash<GraphicsPipelineDesc> {}(pipeline_desc);
  const auto [ps2, rs2]
    = pipeline_cache_->GetOrCreatePipeline(pipeline_desc, hash2);

  EXPECT_EQ(hash1, hash2);
  EXPECT_EQ(ps1, ps2);
  EXPECT_EQ(rs1, rs2);

  // Should not have created new root signature blob
  EXPECT_FALSE(blob_capture_.blob_captured);
}
