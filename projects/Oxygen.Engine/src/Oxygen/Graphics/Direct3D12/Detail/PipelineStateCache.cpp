//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <ranges>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <variant>

#include <d3d12.h>
#include <wrl/client.h> // For Microsoft::WRL::ComPtr

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/VariantHelpers.h> // Added for Overloads
#include <Oxygen/Base/Windows/ComError.h>
#include <Oxygen/Graphics/Common/ObjectRelease.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Direct3D12/Detail/Converters.h>
#include <Oxygen/Graphics/Direct3D12/Detail/FormatUtils.h>
#include <Oxygen/Graphics/Direct3D12/Detail/PipelineStateCache.h>
#include <Oxygen/Graphics/Direct3D12/Detail/dx12_utils.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>

using oxygen::Overloads;
using oxygen::graphics::FramebufferLayoutDesc;
using oxygen::graphics::ShaderStageDesc;
using oxygen::graphics::d3d12::Graphics;
using oxygen::graphics::d3d12::detail::GetDxgiFormatMapping;
using oxygen::graphics::d3d12::detail::PipelineStateCache;
using oxygen::windows::ThrowOnFailed;
namespace dx = oxygen::graphics::d3d12::dx;

namespace {

auto LoadShaderBytecode(const Graphics* gfx, const ShaderStageDesc& desc)
  -> D3D12_SHADER_BYTECODE
{
  const auto shader = gfx->GetShader(desc.shader);
  if (!shader) {
    throw std::runtime_error("Shader not found: " + desc.shader);
  }
  return { shader->Data(), shader->Size() };
}

// Helper to setup framebuffer formats
void SetupFramebufferFormats(const FramebufferLayoutDesc& fb_layout,
  D3D12_GRAPHICS_PIPELINE_STATE_DESC& pso_desc)
{
  // Set render target formats
  pso_desc.NumRenderTargets
    = static_cast<UINT>(fb_layout.color_target_formats.size());
  for (UINT i = 0; i < pso_desc.NumRenderTargets; ++i) {
    const auto& format_mapping
      = GetDxgiFormatMapping(fb_layout.color_target_formats[i]);
    pso_desc.RTVFormats[i] = format_mapping.rtv_format;
  }

  // Set depth-stencil format if present
  if (fb_layout.depth_stencil_format) {
    const auto& format_mapping
      = GetDxgiFormatMapping(*fb_layout.depth_stencil_format);
    pso_desc.DSVFormat = format_mapping.rtv_format;
  }

  // Set sample description
  pso_desc.SampleDesc.Count = fb_layout.sample_count;
  pso_desc.SampleDesc.Quality = 0;
}

//! Helper to map a ShaderStageFlags mask to D3D12_SHADER_VISIBILITY for root
//! signature parameters.
/*!
 This function returns a specific D3D12_SHADER_VISIBILITY value only if exactly
 one graphics stage flag (vertex, pixel, geometry, hull, or domain) is set in
 the mask. If zero, more than one, or any non-graphics stage is set,
 D3D12_SHADER_VISIBILITY_ALL is returned.

 \param vis The ShaderStageFlags mask specifying intended shader visibility.
 \return The corresponding D3D12_SHADER_VISIBILITY value for root parameter
 creation.

 \note D3D12 only allows a single stage or ALL for root parameter visibility. If
       multiple graphics stages or any non-graphics stage are set, ALL is
       required by the API.
*/
auto ConvertShaderVisibility(const oxygen::graphics::ShaderStageFlags vis)
  -> D3D12_SHADER_VISIBILITY
{
  using oxygen::graphics::ShaderStageFlags;

  // Mask out only graphics stages
  auto graphics_mask = vis & ShaderStageFlags::kAllGraphics;
  // Count set bits in graphics_mask
  const uint32_t mask = static_cast<uint32_t>(graphics_mask);
  if (mask && (mask & (mask - 1)) == 0) {
    if (graphics_mask == ShaderStageFlags::kVertex) {
      return D3D12_SHADER_VISIBILITY_VERTEX;
    }
    if (graphics_mask == ShaderStageFlags::kPixel) {
      return D3D12_SHADER_VISIBILITY_PIXEL;
    }
    if (graphics_mask == ShaderStageFlags::kGeometry) {
      return D3D12_SHADER_VISIBILITY_GEOMETRY;
    }
    if (graphics_mask == ShaderStageFlags::kHull) {
      return D3D12_SHADER_VISIBILITY_HULL;
    }
    if (graphics_mask == ShaderStageFlags::kDomain) {
      return D3D12_SHADER_VISIBILITY_DOMAIN;
    }
  }
  // If zero, or more than one bit set, or any non-graphics stage is set, return
  // ALL
  return D3D12_SHADER_VISIBILITY_ALL;
}

//! Convert a ResourceViewType to a D3D12_DESCRIPTOR_RANGE_TYPE.
/*!
 The implementation uses a single large table for any CBV/SRV/UAV and another
 table for samplers. Both tables are optional.
*/
auto ConvertViewTypeToRangeType(
  const oxygen::graphics::ResourceViewType view_type)
  -> D3D12_DESCRIPTOR_RANGE_TYPE
{
  using oxygen::graphics::ResourceViewType;

  // ReSharper disable once CppDefaultCaseNotHandledInSwitchStatement
  // ReSharper disable once CppIncompleteSwitchStatement
  switch (view_type) { // NOLINT(clang-diagnostic-switch)
  case ResourceViewType::kTexture_SRV:
  case ResourceViewType::kTypedBuffer_SRV:
  case ResourceViewType::kStructuredBuffer_SRV:
  case ResourceViewType::kRawBuffer_SRV:
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  case ResourceViewType::kTexture_UAV:
  case ResourceViewType::kTypedBuffer_UAV:
  case ResourceViewType::kStructuredBuffer_UAV:
  case ResourceViewType::kRawBuffer_UAV:
  case ResourceViewType::kSamplerFeedbackTexture_UAV:
    return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  case ResourceViewType::kConstantBuffer:
    return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  case ResourceViewType::kSampler:
    return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;

  case ResourceViewType::kRayTracingAccelStructure:
  case ResourceViewType::kTexture_DSV:
  case ResourceViewType::kTexture_RTV:
    throw std::runtime_error("ResourceViewType not implemented yet");
  }
  throw std::runtime_error("Unsupported or invalid ResourceViewType");
}

} // namespace

namespace {

// Helper function to dump D3D12_ROOT_SIGNATURE_DESC for debugging
auto DumpRootSignatureDesc(const D3D12_ROOT_SIGNATURE_DESC& desc) -> std::string
{
  std::ostringstream oss;
  oss << "=== D3D12_ROOT_SIGNATURE_DESC Debug Dump ===\n";
  oss << "NumParameters: " << desc.NumParameters << "\n";
  oss << "NumStaticSamplers: " << desc.NumStaticSamplers << "\n";
  oss << "Flags: 0x" << std::hex << desc.Flags << std::dec;

  // Decode flags
  std::vector<std::string> flag_names;
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)
    flag_names.emplace_back("ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_VERTEX_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_HULL_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_DOMAIN_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_GEOMETRY_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_PIXEL_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_ALLOW_STREAM_OUTPUT)
    flag_names.emplace_back("ALLOW_STREAM_OUTPUT");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE)
    flag_names.emplace_back("LOCAL_ROOT_SIGNATURE");
  if (desc.Flags
    & D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_AMPLIFICATION_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS)
    flag_names.emplace_back("DENY_MESH_SHADER_ROOT_ACCESS");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)
    flag_names.emplace_back("CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED");
  if (desc.Flags & D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED)
    flag_names.emplace_back("SAMPLER_HEAP_DIRECTLY_INDEXED");

  if (!flag_names.empty()) {
    oss << " (";
    for (size_t i = 0; i < flag_names.size(); ++i) {
      if (i > 0)
        oss << " | ";
      oss << flag_names[i];
    }
    oss << ")";
  }
  oss << "\n\n";

  // Dump root parameters
  for (UINT i = 0; i < desc.NumParameters; ++i) {
    const auto& param = desc.pParameters[i];
    oss << "Root Parameter [" << i << "]:\n";
    oss << "  ParameterType: ";

    switch (param.ParameterType) {
    case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
      oss << "DESCRIPTOR_TABLE\n";
      oss << "  NumDescriptorRanges: "
          << param.DescriptorTable.NumDescriptorRanges << "\n";
      for (UINT j = 0; j < param.DescriptorTable.NumDescriptorRanges; ++j) {
        const auto& range = param.DescriptorTable.pDescriptorRanges[j];
        oss << "    Range [" << j << "]:\n";
        oss << "      RangeType: ";
        switch (range.RangeType) {
        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
          oss << "SRV";
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
          oss << "UAV";
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
          oss << "CBV";
          break;
        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
          oss << "SAMPLER";
          break;
        }
        oss << "\n";
        oss << "      NumDescriptors: " << range.NumDescriptors << "\n";
        oss << "      BaseShaderRegister: " << range.BaseShaderRegister << "\n";
        oss << "      RegisterSpace: " << range.RegisterSpace << "\n";
        oss << "      OffsetInDescriptorsFromTableStart: ";
        if (range.OffsetInDescriptorsFromTableStart
          == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) {
          oss << "APPEND";
        } else {
          oss << range.OffsetInDescriptorsFromTableStart;
        }
        oss << "\n";
      }
      break;
    case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
      oss << "32BIT_CONSTANTS\n";
      oss << "  ShaderRegister: " << param.Constants.ShaderRegister << "\n";
      oss << "  RegisterSpace: " << param.Constants.RegisterSpace << "\n";
      oss << "  Num32BitValues: " << param.Constants.Num32BitValues << "\n";
      break;
    case D3D12_ROOT_PARAMETER_TYPE_CBV:
      oss << "CBV\n";
      oss << "  ShaderRegister: " << param.Descriptor.ShaderRegister << "\n";
      oss << "  RegisterSpace: " << param.Descriptor.RegisterSpace << "\n";
      break;
    case D3D12_ROOT_PARAMETER_TYPE_SRV:
      oss << "SRV\n";
      oss << "  ShaderRegister: " << param.Descriptor.ShaderRegister << "\n";
      oss << "  RegisterSpace: " << param.Descriptor.RegisterSpace << "\n";
      break;
    case D3D12_ROOT_PARAMETER_TYPE_UAV:
      oss << "UAV\n";
      oss << "  ShaderRegister: " << param.Descriptor.ShaderRegister << "\n";
      oss << "  RegisterSpace: " << param.Descriptor.RegisterSpace << "\n";
      break;
    }

    oss << "  ShaderVisibility: ";
    switch (param.ShaderVisibility) {
    case D3D12_SHADER_VISIBILITY_ALL:
      oss << "ALL";
      break;
    case D3D12_SHADER_VISIBILITY_VERTEX:
      oss << "VERTEX";
      break;
    case D3D12_SHADER_VISIBILITY_HULL:
      oss << "HULL";
      break;
    case D3D12_SHADER_VISIBILITY_DOMAIN:
      oss << "DOMAIN";
      break;
    case D3D12_SHADER_VISIBILITY_GEOMETRY:
      oss << "GEOMETRY";
      break;
    case D3D12_SHADER_VISIBILITY_PIXEL:
      oss << "PIXEL";
      break;
    case D3D12_SHADER_VISIBILITY_AMPLIFICATION:
      oss << "AMPLIFICATION";
      break;
    case D3D12_SHADER_VISIBILITY_MESH:
      oss << "MESH";
      break;
    }
    oss << "\n\n";
  }

  // Dump static samplers if any
  if (desc.NumStaticSamplers > 0) {
    oss << "Static Samplers:\n";
    for (UINT i = 0; i < desc.NumStaticSamplers; ++i) {
      const auto& sampler = desc.pStaticSamplers[i];
      oss << "  Sampler [" << i << "]:\n";
      oss << "    Filter: " << sampler.Filter << "\n";
      oss << "    AddressU: " << sampler.AddressU << "\n";
      oss << "    AddressV: " << sampler.AddressV << "\n";
      oss << "    AddressW: " << sampler.AddressW << "\n";
      oss << "    ShaderRegister: " << sampler.ShaderRegister << "\n";
      oss << "    RegisterSpace: " << sampler.RegisterSpace << "\n";
      oss << "    ShaderVisibility: " << sampler.ShaderVisibility << "\n";
    }
    oss << "\n";
  }

  oss << "=== End Root Signature Dump ===\n";
  return oss.str();
}

// For each table type, collect all ranges and the root parameter index
struct TableInfo {
  std::vector<D3D12_DESCRIPTOR_RANGE> ranges;
  std::optional<size_t> root_param_index;
};

// Struct to hold the state of root signature processing
struct RootSignatureProcessingState {
  size_t bindings_count;
  std::vector<bool> param_filled;
  std::vector<D3D12_ROOT_PARAMETER> intermediate_root_params;
  std::vector<bool> is_slot_active_as_root_param;
  TableInfo sampler_table;
  TableInfo cbv_srv_uav_table;

  explicit RootSignatureProcessingState(const size_t num_bindings)
    : bindings_count(num_bindings)
    , param_filled(num_bindings, false)
    , intermediate_root_params(num_bindings)
    , is_slot_active_as_root_param(num_bindings, false)
  {
  }
};

void ProcessDescriptorTableBinding(
  const oxygen::graphics::DescriptorTableBinding& table_binding,
  const auto& item, // Assuming item has binding_slot_desc
  size_t original_idx, RootSignatureProcessingState& state)
{
  D3D12_DESCRIPTOR_RANGE range {
    .RangeType = ConvertViewTypeToRangeType(table_binding.view_type),
    .NumDescriptors
    = (table_binding.count == (std::numeric_limits<uint32_t>::max)())
      ? D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
      : table_binding.count,
    .BaseShaderRegister = item.binding_slot_desc.register_index,
    .RegisterSpace = item.binding_slot_desc.register_space,
    .OffsetInDescriptorsFromTableStart = table_binding.base_index,
  };

  if (range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER) {
    if (!state.sampler_table.root_param_index) {
      state.sampler_table.root_param_index = original_idx;
      state.is_slot_active_as_root_param[original_idx] = true;
    }
    state.sampler_table.ranges.push_back(range);
  } else { // CBV/SRV/UAV
    if (!state.cbv_srv_uav_table.root_param_index) {
      state.cbv_srv_uav_table.root_param_index = original_idx;
      state.is_slot_active_as_root_param[original_idx] = true;
    }
    state.cbv_srv_uav_table.ranges.push_back(range);
  }
}

// Implementation for ProcessPushConstantsBinding
void ProcessPushConstantsBinding(
  const oxygen::graphics::PushConstantsBinding& push, const auto& item,
  const size_t original_idx, RootSignatureProcessingState& state)
{
  D3D12_ROOT_PARAMETER param
    = { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants = { .ShaderRegister = item.binding_slot_desc.register_index,
          .RegisterSpace = item.binding_slot_desc.register_space,
          .Num32BitValues = push.size },
        .ShaderVisibility = ConvertShaderVisibility(item.visibility) };
  state.intermediate_root_params[original_idx] = param;
  state.is_slot_active_as_root_param[original_idx] = true;
}

// Implementation for ProcessDirectBufferBinding
void ProcessDirectBufferBinding(
  const oxygen::graphics::DirectBufferBinding& /*buf*/, // buf is unused
  const auto& item, const size_t original_idx,
  RootSignatureProcessingState& state)
{
  D3D12_ROOT_PARAMETER param = { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
    .Descriptor = { .ShaderRegister = item.binding_slot_desc.register_index,
      .RegisterSpace = item.binding_slot_desc.register_space },
    .ShaderVisibility = ConvertShaderVisibility(item.visibility) };
  state.intermediate_root_params[original_idx] = param;
  state.is_slot_active_as_root_param[original_idx] = true;
}

// Implementation for ProcessDirectTextureBinding
void ProcessDirectTextureBinding(
  const oxygen::graphics::DirectTextureBinding& /*tex*/, // tex is unused
  const auto& item, const size_t original_idx,
  RootSignatureProcessingState& state)
{
  D3D12_ROOT_PARAMETER param = { .ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
    .Descriptor = { .ShaderRegister = item.binding_slot_desc.register_index,
      .RegisterSpace = item.binding_slot_desc.register_space },
    .ShaderVisibility = ConvertShaderVisibility(item.visibility) };
  state.intermediate_root_params[original_idx] = param;
  state.is_slot_active_as_root_param[original_idx] = true;
}

// Helper function to check for overlapping descriptor ranges
void CheckDescriptorRangeOverlap(
  const std::vector<D3D12_DESCRIPTOR_RANGE>& ranges)
{
  // Only check for overlap if ranges are for the same type and register space
  for (size_t i = 0; i < ranges.size(); ++i) {
    for (size_t j = i + 1; j < ranges.size(); ++j) {
      const auto& a = ranges[i];
      const auto& b = ranges[j];
      if (a.RangeType != b.RangeType || a.RegisterSpace != b.RegisterSpace)
        continue;
      const UINT a_start = a.BaseShaderRegister;
      const UINT a_end
        = a.NumDescriptors == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
        ? UINT_MAX
        : a_start + a.NumDescriptors;
      const UINT b_start = b.BaseShaderRegister;
      const UINT b_end
        = b.NumDescriptors == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
        ? UINT_MAX
        : b_start + b.NumDescriptors;
      if (!(a_end <= b_start || b_end <= a_start)) {
        throw std::runtime_error("Overlapping descriptor ranges in the same "
                                 "table (type/register space)");
      }
    }
  }
}

template <typename T>
concept IsGraphicPipelineDesc
  = std::is_base_of_v<oxygen::graphics::GraphicsPipelineDesc, T>;

template <typename T>
concept IsComputePipelineDesc
  = std::is_base_of_v<oxygen::graphics::ComputePipelineDesc, T>;

template <typename T>
concept PipelineDescType = IsGraphicPipelineDesc<T> || IsComputePipelineDesc<T>;

template <PipelineDescType PipelineDesc>
auto CreateRootSignature(const PipelineDesc& desc, const Graphics* gfx)
  -> dx::IRootSignature*
{
  using oxygen::graphics::DescriptorTableBinding;
  using oxygen::graphics::DirectBufferBinding;
  using oxygen::graphics::DirectTextureBinding;
  using oxygen::graphics::PushConstantsBinding;
  using oxygen::graphics::ResourceViewType;

  RootSignatureProcessingState state(desc.RootBindings().size());

  for (const auto& item : desc.RootBindings()) {
    const size_t original_idx
      = static_cast<size_t>(item.GetRootParameterIndex());

    DCHECK_F(original_idx < state.bindings_count,
      "Root parameter index {} out of range [0, {})", original_idx,
      state.bindings_count);
    DCHECK_F(!state.param_filled[original_idx],
      "Duplicate root parameter index {} in pipeline description",
      original_idx);

    state.param_filled[original_idx] = true;

    std::visit(Overloads {
                 [&](const DescriptorTableBinding& table_binding) {
                   ProcessDescriptorTableBinding(
                     table_binding, item, original_idx, state);
                 },
                 [&](const PushConstantsBinding& push) {
                   ProcessPushConstantsBinding(push, item, original_idx, state);
                 },
                 [&](const DirectBufferBinding& buf) {
                   ProcessDirectBufferBinding(buf, item, original_idx, state);
                 },
                 [&](const DirectTextureBinding& tex) {
                   ProcessDirectTextureBinding(tex, item, original_idx, state);
                 },
               },
      item.data);
  }

  // Validate no overlap within each table's collected ranges
  if (state.cbv_srv_uav_table.root_param_index) {
    CheckDescriptorRangeOverlap(state.cbv_srv_uav_table.ranges);
  }
  if (state.sampler_table.root_param_index) {
    CheckDescriptorRangeOverlap(state.sampler_table.ranges);
  }

  // Finalize table parameters in intermediate_root_params at their defining
  // original_idx
  if (state.cbv_srv_uav_table.root_param_index) {
    size_t table_original_idx = *state.cbv_srv_uav_table.root_param_index;
    D3D12_ROOT_PARAMETER param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {
                .NumDescriptorRanges = static_cast<UINT>(state.cbv_srv_uav_table.ranges.size()),
                .pDescriptorRanges = state.cbv_srv_uav_table.ranges.data(),
            },
            .ShaderVisibility = ConvertShaderVisibility(desc.RootBindings()[table_original_idx].visibility),
        };
    state.intermediate_root_params[table_original_idx] = param;
  }
  if (state.sampler_table.root_param_index) {
    size_t table_original_idx = *state.sampler_table.root_param_index;
    D3D12_ROOT_PARAMETER param = {
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE,
            .DescriptorTable = {
                .NumDescriptorRanges = static_cast<UINT>(state.sampler_table.ranges.size()),
                .pDescriptorRanges = state.sampler_table.ranges.data(),
            },
            .ShaderVisibility = ConvertShaderVisibility(desc.RootBindings()[table_original_idx].visibility),
        };
    state.intermediate_root_params[table_original_idx] = param;
  }

  // Compact active root parameters into the final list for the descriptor
  std::vector<D3D12_ROOT_PARAMETER> final_params_for_desc;
  final_params_for_desc.reserve(state.bindings_count);
  for (size_t i = 0; i < state.bindings_count; ++i) {
    if (state.is_slot_active_as_root_param[i]) {
      final_params_for_desc.push_back(state.intermediate_root_params[i]);
    }
  }

#if !defined(NDEBUG)
  // Sanity check: all original binding indices must have been processed
  for (size_t i = 0; i < state.param_filled.size(); ++i) {
    DCHECK_F(state.param_filled[i],
      "Root parameter index {} was not processed in pipeline description", i);
  }
#endif // NDEBUG

  D3D12_ROOT_SIGNATURE_DESC root_sig_desc {
    .NumParameters = static_cast<UINT>(final_params_for_desc.size()),
    .pParameters = final_params_for_desc.data(),
    .NumStaticSamplers = 0,
    .pStaticSamplers = nullptr,
    .Flags = D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
      | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED,
  };
  if constexpr (IsGraphicPipelineDesc<PipelineDesc>) {
    // For graphics pipelines, allow input assembler input layout
    root_sig_desc.Flags
      |= D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  }

#if !defined(NDEBUG)
  // Verify both direct indexing flags are enabled per Section 3 contract
  const D3D12_ROOT_SIGNATURE_FLAGS kRequiredDirectIndexingFlags
    = static_cast<D3D12_ROOT_SIGNATURE_FLAGS>(
      D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED
      | D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED);
  DCHECK_F((root_sig_desc.Flags & kRequiredDirectIndexingFlags)
      == kRequiredDirectIndexingFlags,
    "Root signature missing required direct-indexing flags (CBV/SRV/UAV and "
    "Sampler). Flags: 0x{:X}",
    static_cast<unsigned>(root_sig_desc.Flags));
  // Debugging helper: dump the root signature descriptor
  LOG_F(2, "{}", DumpRootSignatureDesc(root_sig_desc));
#endif

  Microsoft::WRL::ComPtr<ID3DBlob> sig_blob, err_blob;
  HRESULT hr = D3D12SerializeRootSignature(
    &root_sig_desc, D3D_ROOT_SIGNATURE_VERSION_1_0, &sig_blob, &err_blob);
  if (FAILED(hr)) {
    std::string error_msg = "Failed to serialize root signature: ";
    if (err_blob) {
      error_msg += static_cast<const char*>(err_blob->GetBufferPointer());
    }
    throw std::runtime_error(error_msg);
  }

  dx::IRootSignature* root_sig { nullptr };
  auto* device = gfx->GetCurrentDevice();
  ThrowOnFailed(device->CreateRootSignature(0, sig_blob->GetBufferPointer(),
                  sig_blob->GetBufferSize(), IID_PPV_ARGS(&root_sig)),
    "Failed to create root signature");
  return root_sig;
}

} // namespace

PipelineStateCache::PipelineStateCache(Graphics* gfx)
  : gfx_(gfx)
{
  DCHECK_NOTNULL_F(gfx_);
}

PipelineStateCache::~PipelineStateCache()
{
  LOG_SCOPE_FUNCTION(INFO);

  for (auto& entry_tuple : graphics_pipelines_ | std::views::values) {
    [[maybe_unused]] auto& desc = std::get<0>(entry_tuple);
    DLOG_F(2, "pipeline state: {}", desc.GetName());
    auto& [pipeline_state, root_signature] = std::get<1>(entry_tuple);
    DLOG_F(2, " .. pipeline state release");
    ObjectRelease(pipeline_state);
    DLOG_F(2, " .. root signature release");
    ObjectRelease(root_signature);
  }
  DLOG_F(2, "graphics pipelines cleared");
  graphics_pipelines_.clear();

  for (auto& entry_tuple : compute_pipelines_ | std::views::values) {
    [[maybe_unused]] auto& desc = std::get<0>(entry_tuple);
    DLOG_F(2, "pipeline state: {}", desc.GetName());
    auto& [pipeline_state, root_signature] = std::get<1>(entry_tuple);
    DLOG_F(2, " .. pipeline state release");
    ObjectRelease(pipeline_state);
    DLOG_F(2, " .. root signature release");
    ObjectRelease(root_signature);
  }
  DLOG_F(2, "compute pipelines cleared");
  compute_pipelines_.clear();
}

auto PipelineStateCache::CreateRootSignature(
  const GraphicsPipelineDesc& desc) const -> dx::IRootSignature*
{
  return ::CreateRootSignature(desc, gfx_);
}

auto PipelineStateCache::CreateRootSignature(
  const ComputePipelineDesc& desc) const -> dx::IRootSignature*
{
  return ::CreateRootSignature(desc, gfx_);
}

//! Get or create a graphics pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateGraphicsPipeline(
  GraphicsPipelineDesc desc, size_t hash) -> Entry
{
  LOG_SCOPE_F(2, "Pipeline State");
  DLOG_F(2, "for descriptor {}, hash={}", desc.GetName(), hash);
  if (auto it = graphics_pipelines_.find(hash);
    it != graphics_pipelines_.end()) {
    const auto& [cached_desc, entry] = it->second;
    DLOG_F(2, "cache hit: pso=0x{:04X}, rs=0x{:04X}",
      reinterpret_cast<std::uintptr_t>(entry.pipeline_state),
      reinterpret_cast<std::uintptr_t>(entry.root_signature));
    return entry;
  }

  // Create the root signature
  auto* root_signature = CreateRootSignature(desc);
  if (root_signature == nullptr) {
    throw std::runtime_error(
      "failed to create bindless root signature for graphics pipeline");
  }
  auto rs_name = desc.GetName() + "_BindlessRS";
  NameObject(root_signature, rs_name);
  DLOG_F(2, "new root signature: 0x{:04X} ({})",
    reinterpret_cast<std::uintptr_t>(root_signature), rs_name);

  // Create new pipeline state
  dx::IPipelineState* pso = nullptr;

  // 2. Translate GraphicsPipelineDesc to D3D12_GRAPHICS_PIPELINE_STATE_DESC
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;

  // Set shader stages
  if (desc.VertexShader()) {
    const auto& shader_desc = desc.VertexShader().value();
    pso_desc.VS = LoadShaderBytecode(gfx_, shader_desc);
  }

  if (desc.PixelShader()) {
    const auto& shader_desc = desc.PixelShader().value();
    pso_desc.PS = LoadShaderBytecode(gfx_, shader_desc);
  }

  if (desc.GeometryShader()) {
    const auto& shader_desc = desc.GeometryShader().value();
    pso_desc.GS = LoadShaderBytecode(gfx_, shader_desc);
  }

  if (desc.HullShader()) {
    const auto& shader_desc = desc.HullShader().value();
    pso_desc.HS = LoadShaderBytecode(gfx_, shader_desc);
  }

  if (desc.DomainShader()) {
    const auto& shader_desc = desc.DomainShader().value();
    pso_desc.DS = LoadShaderBytecode(gfx_, shader_desc);
  }

  // Set up blend state
  D3D12_BLEND_DESC blend_desc = {};
  TranslateBlendState(desc.BlendState(), blend_desc);
  pso_desc.BlendState = blend_desc;
  pso_desc.SampleMask = UINT_MAX; // Sample all pixels

  // Set up rasterizer state
  D3D12_RASTERIZER_DESC raster_desc = {};
  TranslateRasterizerState(desc.RasterizerState(), raster_desc);
  pso_desc.RasterizerState = raster_desc;

  // Set up depth-stencil state
  D3D12_DEPTH_STENCIL_DESC depth_stencil_desc = {};
  TranslateDepthStencilState(desc.DepthStencilState(), depth_stencil_desc);
  pso_desc.DepthStencilState = depth_stencil_desc;
  // Set primitive topology type
  pso_desc.PrimitiveTopologyType
    = ConvertPrimitiveType(desc.PrimitiveTopology());

  // Set framebuffer layout
  const auto& fb_layout = desc.FramebufferLayout();
  SetupFramebufferFormats(fb_layout, pso_desc);

  // No input layout for bindless rendering (use structured/raw buffers instead)
  pso_desc.InputLayout = {
    .pInputElementDescs = nullptr,
    .NumElements = 0,
  };

  // Create the pipeline state object
  auto* device = gfx_->GetCurrentDevice();
  ThrowOnFailed(
    device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pso)),
    "Failed to create graphics pipeline state");

  auto pso_name = desc.GetName() + "_PSO";
  NameObject(pso, pso_name);
  DLOG_F(2, "new pso: 0x{:04X} ({})", reinterpret_cast<std::uintptr_t>(pso),
    pso_name);

  Entry entry {
    .pipeline_state = pso,
    .root_signature = root_signature,
  };
  graphics_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
  return entry;
}

//! Get or create a compute pipeline state object and root signature.
auto PipelineStateCache::GetOrCreateComputePipeline(
  ComputePipelineDesc desc, size_t hash) -> Entry
{
  if (const auto it = compute_pipelines_.find(hash);
    it != compute_pipelines_.end()) {
    const auto& [cached_desc, entry] = it->second;
    return entry;
  }

  // Create the root signature
  auto* root_signature = CreateRootSignature(desc);
  if (root_signature == nullptr) {
    throw std::runtime_error(
      "failed to create bindless root signature for graphics pipeline");
  }

  dx::IPipelineState* pso = nullptr;

  // 2. Create the compute pipeline state object
  D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
  pso_desc.pRootSignature = root_signature;

  // Set compute shader
  const auto& compute_shader_desc = desc.ComputeShader();
  pso_desc.CS = LoadShaderBytecode(gfx_, compute_shader_desc);
  // Create the pipeline state object
  auto* device = gfx_->GetCurrentDevice();
  ThrowOnFailed(
    device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pso)),
    "Failed to create compute pipeline state");

  Entry entry {
    .pipeline_state = pso,
    .root_signature = root_signature,
  };
  compute_pipelines_.emplace(hash, std::make_tuple(std::move(desc), entry));
  return entry;
}

//! Get the cached graphics pipeline description for a given hash.
auto PipelineStateCache::GetGraphicsPipelineDesc(const size_t hash) const
  -> const GraphicsPipelineDesc&
{
  return std::get<0>(graphics_pipelines_.at(hash));
}

//! Get the cached compute pipeline description for a given hash.
auto PipelineStateCache::GetComputePipelineDesc(const size_t hash) const
  -> const ComputePipelineDesc&
{
  return std::get<0>(compute_pipelines_.at(hash));
}
