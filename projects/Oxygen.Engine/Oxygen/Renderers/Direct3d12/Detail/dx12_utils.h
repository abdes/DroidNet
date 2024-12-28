//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>
#include <d3d12.h>

#include "Oxygen/Base/logging.h"
#include "Oxygen/Base/Windows/ComError.h"

using oxygen::windows::ThrowOnFailed;

namespace oxygen::renderer::d3d12 {

  inline auto ToNarrow(const std::wstring& wide_string) -> std::string
  {
    if (wide_string.empty()) return {};

    const int size_needed = WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      nullptr,
      0,
      nullptr,
      nullptr);
    std::string utf8_string(size_needed, 0);

    WideCharToMultiByte(
      CP_UTF8,
      0,
      wide_string.data(),
      static_cast<int>(wide_string.size()),
      utf8_string.data(),
      size_needed,
      nullptr,
      nullptr);

    return utf8_string;
  }

  inline void NameObject(ID3D12Object* const object, const std::wstring& name)
  {
#ifdef _DEBUG
    ThrowOnFailed(object->SetName(name.c_str()));
    LOG_F(1, "+D3D12 named object created: {}", ToNarrow(name));
#endif
  }

  //////////////////////////////////////////////////////////////////////////////


    /// <summary>
    /// Align by rounding up. Will result in a multiple of 'alignment'
    /// that is greater than or equal to 'size'
    /// </summary>
  template<uint64_t alignment>
  [[nodiscard]] constexpr uint64_t
    align_size_up(const uint64_t size)
  {
    static_assert(alignment, "Alignment must be non-zero");
    constexpr uint64_t mask{ alignment - 1 };
    static_assert(!(alignment & mask), "Alignment must be a power of 2.");
    return ((size + mask) & ~mask);
  }

  /// <summary>
  /// Align by rounding down. Will result in a multiple of 'alignment'
  /// that is less than or equal to 'size'
  /// </summary>
  /// <param name="size"></param>
  /// <returns></returns>
  template<uint64_t alignment>
  [[nodiscard]] constexpr uint64_t
    align_size_down(const uint64_t size)
  {
    static_assert(alignment, "Alignment must be non-zero");
    constexpr uint64_t mask{ alignment - 1 };
    static_assert(!(alignment & mask), "Alignment must be a power of 2.");
    return (size & ~mask);
  }

  /// <summary>
  /// Align by rounding up. Will result in a multiple of 'alignment'
  /// that is greater than or equal to 'size'
  /// </summary>
  [[nodiscard]] constexpr uint64_t
    align_size_up(const uint64_t size, const uint64_t alignment)
  {
    assert(alignment && "Alignment must be non-zero");
    const uint64_t mask{ alignment - 1 };
    assert(!(alignment & mask) && "Alignment must be a power of 2.");
    return ((size + mask) & ~mask);
  }

  /// <summary>
  /// Align by rounding down. Will result in a multiple of 'alignment'
  /// that is less than or equal to 'size'
  /// </summary>
  /// <param name="size"></param>
  /// <param name="alignment"></param>
  /// <returns></returns>
  [[nodiscard]] constexpr uint64_t
    align_size_down(const uint64_t size, const uint64_t alignment)
  {
    assert(alignment && "Alignment must be non-zero");
    const uint64_t mask{ alignment - 1 };
    assert(!(alignment & mask) && "Alignment must be a power of 2.");
    return (size & ~mask);
  }

  constexpr struct
  {
    D3D12_HEAP_PROPERTIES default_heap_props
    {
      .Type = D3D12_HEAP_TYPE_DEFAULT,
      .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
      .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
      .CreationNodeMask = 0,
      .VisibleNodeMask = 0,
    };

    D3D12_HEAP_PROPERTIES upload_heap
    {
        D3D12_HEAP_TYPE_UPLOAD,						// type
        D3D12_CPU_PAGE_PROPERTY_UNKNOWN,			// CPUPageProperty
        D3D12_MEMORY_POOL_UNKNOWN,					// MemoryPoolPreference
        0,											// CreationNodeMask
        0											// VisibleNodeMask
    };
  } kHeapProperties;

  constexpr struct
  {
    D3D12_RASTERIZER_DESC no_cull
    {
        D3D12_FILL_MODE_SOLID,						// FillMode
        D3D12_CULL_MODE_NONE,						// CullMode
        1,											// FrontCounterClockwise
        0,											// DepthBias
        0,											// DepthBiasClamp
        0,											// SlopeScaledDepthBias
        1,											// DepthClipEnable
        0,											// MultisampleEnable
        0,											// AntialiasedLineEnable
        0,											// ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF	// ConservativeRaster
    };

    D3D12_RASTERIZER_DESC backface_cull
    {
        D3D12_FILL_MODE_SOLID,						// FillMode
        D3D12_CULL_MODE_BACK,						// CullMode
        1,											// FrontCounterClockwise
        0,											// DepthBias
        0,											// DepthBiasClamp
        0,											// SlopeScaledDepthBias
        1,											// DepthClipEnable
        0,											// MultisampleEnable
        0,											// AntialiasedLineEnable
        0,											// ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF	// ConservativeRaster
    };

    D3D12_RASTERIZER_DESC frontface_cull
    {
        D3D12_FILL_MODE_SOLID,						// FillMode
        D3D12_CULL_MODE_FRONT,						// CullMode
        1,											// FrontCounterClockwise
        0,											// DepthBias
        0,											// DepthBiasClamp
        0,											// SlopeScaledDepthBias
        1,											// DepthClipEnable
        0,											// MultisampleEnable
        0,											// AntialiasedLineEnable
        0,											// ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF	// ConservativeRaster
    };

    D3D12_RASTERIZER_DESC wireframe
    {
        D3D12_FILL_MODE_WIREFRAME,					// FillMode
        D3D12_CULL_MODE_NONE,						// CullMode
        1,											// FrontCounterClockwise
        0,											// DepthBias
        0,											// DepthBiasClamp
        0,											// SlopeScaledDepthBias
        1,											// DepthClipEnable
        0,											// MultisampleEnable
        0,											// AntialiasedLineEnable
        0,											// ForcedSampleCount
        D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF	// ConservativeRaster
    };
  } kRasterizerState;

  constexpr struct
  {
    D3D12_DEPTH_STENCIL_DESC1 disabled
    {
        0,											// DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,				// DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS_EQUAL,			// DepthFunc
        0,											// StencilEnable
        0,											// StencilReadMask
        0,											// StencilWriteMask
        {},											// FrontFace
        {},											// BackFace
        0											// DepthBoundsTestEnable
    };

    D3D12_DEPTH_STENCIL_DESC1 enabled
    {
        1,											// DepthEnable
        D3D12_DEPTH_WRITE_MASK_ALL,					// DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS_EQUAL,			// DepthFunc
        0,											// StencilEnable
        0,											// StencilReadMask
        0,											// StencilWriteMask
        {},											// FrontFace
        {},											// BackFace
        0											// DepthBoundsTestEnable
    };

    D3D12_DEPTH_STENCIL_DESC1 enabled_readonly
    {
        1,											// DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,				// DepthWriteMask
        D3D12_COMPARISON_FUNC_LESS_EQUAL,			// DepthFunc
        0,											// StencilEnable
        0,											// StencilReadMask
        0,											// StencilWriteMask
        {},											// FrontFace
        {},											// BackFace
        0											// DepthBoundsTestEnable
    };

    D3D12_DEPTH_STENCIL_DESC1 reversed
    {
        1,											// DepthEnable
        D3D12_DEPTH_WRITE_MASK_ALL,					// DepthWriteMask
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,		// DepthFunc
        0,											// StencilEnable
        0,											// StencilReadMask
        0,											// StencilWriteMask
        {},											// FrontFace
        {},											// BackFace
        0											// DepthBoundsTestEnable
    };

    D3D12_DEPTH_STENCIL_DESC1 reversed_readonly
    {
        1,											// DepthEnable
        D3D12_DEPTH_WRITE_MASK_ZERO,				// DepthWriteMask
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,		// DepthFunc
        0,											// StencilEnable
        0,											// StencilReadMask
        0,											// StencilWriteMask
        {},											// FrontFace
        {},											// BackFace
        0											// DepthBoundsTestEnable
    };
  } kDepthState;

  constexpr struct
  {
    D3D12_BLEND_DESC disabled{
       0,											// AlphaToCoverageEnable
       0,											// IndependentBlendEnable
       {
           {
               0,									// BlendEnable
               0,									// LogicOpEnable
               D3D12_BLEND_SRC_ALPHA,				// SrcBlend
               D3D12_BLEND_INV_SRC_ALPHA,			// DestBlend
               D3D12_BLEND_OP_ADD,					// BlendOp
               D3D12_BLEND_ONE,					// SrcBlendAlpha
               D3D12_BLEND_ONE,					// DestBlendAlpha
               D3D12_BLEND_OP_ADD,					// BlendOpAlpha
               D3D12_LOGIC_OP_NOOP,				// LogicOp
               D3D12_COLOR_WRITE_ENABLE_ALL		// RenderTargetWriteMask
           },
           {}, {}, {}, {}, {}, {}, {}
       }
    };

    D3D12_BLEND_DESC alpha_blend{
       0,											// AlphaToCoverageEnable
       0,											// IndependentBlendEnable
       {
           {
               1,									// BlendEnable
               0,									// LogicOpEnable
               D3D12_BLEND_SRC_ALPHA,				// SrcBlend
               D3D12_BLEND_INV_SRC_ALPHA,			// DestBlend
               D3D12_BLEND_OP_ADD,					// BlendOp
               D3D12_BLEND_ONE,					// SrcBlendAlpha
               D3D12_BLEND_ONE,					// DestBlendAlpha
               D3D12_BLEND_OP_ADD,					// BlendOpAlpha
               D3D12_LOGIC_OP_NOOP,				// LogicOp
               D3D12_COLOR_WRITE_ENABLE_ALL		// RenderTargetWriteMask
           },
           {}, {}, {}, {}, {}, {}, {}
       }
    };

    D3D12_BLEND_DESC additive{
       0,											// AlphaToCoverageEnable
       0,											// IndependentBlendEnable
       {
           {
               1,									// BlendEnable
               0,									// LogicOpEnable
               D3D12_BLEND_ONE,					// SrcBlend
               D3D12_BLEND_ONE,					// DestBlend
               D3D12_BLEND_OP_ADD,					// BlendOp
               D3D12_BLEND_ONE,					// SrcBlendAlpha
               D3D12_BLEND_ONE,					// DestBlendAlpha
               D3D12_BLEND_OP_ADD,					// BlendOpAlpha
               D3D12_LOGIC_OP_NOOP,				// LogicOp
               D3D12_COLOR_WRITE_ENABLE_ALL		// RenderTargetWriteMask
           },
           {}, {}, {}, {}, {}, {}, {}
       }
    };

    D3D12_BLEND_DESC premultiplied{
       0,											// AlphaToCoverageEnable
       0,											// IndependentBlendEnable
       {
           {
               0,									// BlendEnable
               0,									// LogicOpEnable
               D3D12_BLEND_ONE,					// SrcBlend
               D3D12_BLEND_INV_SRC_ALPHA,			// DestBlend
               D3D12_BLEND_OP_ADD,					// BlendOp
               D3D12_BLEND_ONE,					// SrcBlendAlpha
               D3D12_BLEND_ONE,					// DestBlendAlpha
               D3D12_BLEND_OP_ADD,					// BlendOpAlpha
               D3D12_LOGIC_OP_NOOP,				// LogicOp
               D3D12_COLOR_WRITE_ENABLE_ALL		// RenderTargetWriteMask
           },
           {}, {}, {}, {}, {}, {}, {}
       }
    };
  } kBlendState;

  constexpr uint64_t
    align_size_for_constant_buffer(const uint64_t size)
  {
    return align_size_up<D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT>(size);
  }

  constexpr uint64_t
    align_size_for_texture(const uint64_t size)
  {
    return align_size_up<D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT>(size);
  }

  struct D3d12DescriptorRange : public D3D12_DESCRIPTOR_RANGE1
  {
    constexpr explicit D3d12DescriptorRange(const D3D12_DESCRIPTOR_RANGE_TYPE range_type,
                                            const uint32_t descriptor_count, const uint32_t shader_register, const uint32_t space = 0,
                                            const D3D12_DESCRIPTOR_RANGE_FLAGS flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE,
                                            const uint32_t offset_from_table_start = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND)
      : D3D12_DESCRIPTOR_RANGE1{ range_type, descriptor_count, shader_register, space, flags, offset_from_table_start }
    {
    }
  };

  struct D3d12RootParameter : public D3D12_ROOT_PARAMETER1
  {
    constexpr void as_constants(
      const uint32_t num_constants,
      const D3D12_SHADER_VISIBILITY visibility,
      const uint32_t shader_register,
      const uint32_t space = 0)
    {
      ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
      ShaderVisibility = visibility;
      Constants.Num32BitValues = num_constants;
      Constants.ShaderRegister = shader_register;
      Constants.RegisterSpace = space;
    }

    constexpr void as_cbv(
      const D3D12_SHADER_VISIBILITY visibility,
      const uint32_t shader_register,
      const uint32_t space = 0,
      const D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
    {
      as_descriptor(D3D12_ROOT_PARAMETER_TYPE_CBV, visibility, shader_register, space, flags);
    }

    constexpr void as_srv(
      const D3D12_SHADER_VISIBILITY visibility,
      const uint32_t shader_register,
      const uint32_t space = 0,
      const D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
    {
      as_descriptor(D3D12_ROOT_PARAMETER_TYPE_SRV, visibility, shader_register, space, flags);
    }

    constexpr void as_uav(
      const D3D12_SHADER_VISIBILITY visibility,
      const uint32_t shader_register,
      const uint32_t space = 0,
      const D3D12_ROOT_DESCRIPTOR_FLAGS flags = D3D12_ROOT_DESCRIPTOR_FLAG_NONE)
    {
      as_descriptor(D3D12_ROOT_PARAMETER_TYPE_UAV, visibility, shader_register, space, flags);
    }

    constexpr void as_descriptor_table(
      const D3D12_SHADER_VISIBILITY visibility,
      const D3d12DescriptorRange* ranges,
      const uint32_t range_count)
    {
      ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
      ShaderVisibility = visibility;
      DescriptorTable.NumDescriptorRanges = range_count;
      DescriptorTable.pDescriptorRanges = ranges;
    }

  private:
    constexpr void as_descriptor(
      const D3D12_ROOT_PARAMETER_TYPE type,
      const D3D12_SHADER_VISIBILITY visibility,
      const uint32_t shader_register, const uint32_t space,
      const D3D12_ROOT_DESCRIPTOR_FLAGS flags)
    {
      ParameterType = type;
      ShaderVisibility = visibility;
      Descriptor.ShaderRegister = shader_register;
      Descriptor.RegisterSpace = space;
      Descriptor.Flags = flags;
    }
  };

  struct D3d12RootSignatureDesc;
  ID3D12RootSignature* create_root_signature(const D3d12RootSignatureDesc& desc);

  // Maximum 64 DWORDs (uint32_t's) divided up amongst all root parameters:
  // Root constants = 1 DWORD per 32-bit constant
  // Root descriptor (CBV, SRV, UAV) = 2 DWORDs each
  // Descriptor table pointer = 1 DWORD
  // Static samplers = 0 DWORDs (compiled into shader)
  struct D3d12RootSignatureDesc : public D3D12_ROOT_SIGNATURE_DESC1
  {
    constexpr static D3D12_ROOT_SIGNATURE_FLAGS default_flags{
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED
    };

    constexpr explicit D3d12RootSignatureDesc(const D3d12RootParameter* parameters, const uint32_t parameter_count,
                                              const D3D12_ROOT_SIGNATURE_FLAGS flags = default_flags,
                                              const D3D12_STATIC_SAMPLER_DESC* static_samplers = nullptr, const uint32_t sampler_count = 0)
      : D3D12_ROOT_SIGNATURE_DESC1{ parameter_count, parameters, sampler_count, static_samplers, flags }
    {
    }

    [[nodiscard]] ID3D12RootSignature* create() const
    {
      // TODO: Implement
      return create_root_signature(*this);
    }

  };

#pragma warning(push)
#pragma warning(disable : 4324) // disable padding warning
  template<D3D12_PIPELINE_STATE_SUBOBJECT_TYPE Type, typename T>
  class alignas(void*) D3D12PipelineStateSubObject
  {
  public:
    D3D12PipelineStateSubObject() = default;
    constexpr explicit D3D12PipelineStateSubObject(T sub_object) : type_{ Type }, sub_object_{ sub_object } {}
    D3D12PipelineStateSubObject& operator=(const T& sub_object) { sub_object_ = sub_object; return *this; }
  private:
    D3D12_PIPELINE_STATE_SUBOBJECT_TYPE type_{ Type };
    T sub_object_{};
  };
#pragma warning(pop)

  // Pipeline State SubObject (PSS) macro
#define PSS(name, ...) using D3D12PipelineStateSubObject_##name = D3D12PipelineStateSubObject<__VA_ARGS__>;

  PSS(root_signature, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE, ID3D12RootSignature*);
  PSS(vs, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS, D3D12_SHADER_BYTECODE);
  PSS(ps, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS, D3D12_SHADER_BYTECODE);
  PSS(ds, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS, D3D12_SHADER_BYTECODE);
  PSS(hs, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS, D3D12_SHADER_BYTECODE);
  PSS(gs, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS, D3D12_SHADER_BYTECODE);
  PSS(cs, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS, D3D12_SHADER_BYTECODE);
  PSS(stream_output, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT, D3D12_STREAM_OUTPUT_DESC);
  PSS(blend, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND, D3D12_BLEND_DESC);
  PSS(sample_mask, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK, uint32_t);
  PSS(rasterizer, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER, D3D12_RASTERIZER_DESC);
  PSS(depth_stencil, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL, D3D12_DEPTH_STENCIL_DESC);
  PSS(input_layer, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT, D3D12_INPUT_LAYOUT_DESC);
  PSS(ib_strip_cut_value, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE, D3D12_INDEX_BUFFER_STRIP_CUT_VALUE);
  PSS(primitive_topology, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY, D3D12_PRIMITIVE_TOPOLOGY_TYPE);
  PSS(render_target_formats, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS, D3D12_RT_FORMAT_ARRAY);
  PSS(depth_stencil_format, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT, DXGI_FORMAT);
  PSS(sample_desc, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC, DXGI_SAMPLE_DESC);
  PSS(node_mask, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK, uint32_t);
  PSS(cached_pso, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO, D3D12_CACHED_PIPELINE_STATE);
  PSS(flags, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS, D3D12_PIPELINE_STATE_FLAGS);
  PSS(depth_stencil1, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1, D3D12_DEPTH_STENCIL_DESC1);
  PSS(view_instancing, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING, D3D12_VIEW_INSTANCING_DESC);
  PSS(as, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS, D3D12_SHADER_BYTECODE);
  PSS(ms, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS, D3D12_SHADER_BYTECODE);

#undef PSS

  struct D3d12PipelineStateSubObjectStream
  {
    D3D12PipelineStateSubObject_root_signature			root_signature{ nullptr };
    D3D12PipelineStateSubObject_vs						vs{};
    D3D12PipelineStateSubObject_ps						ps{};
    D3D12PipelineStateSubObject_ds						ds{};
    D3D12PipelineStateSubObject_hs						hs{};
    D3D12PipelineStateSubObject_gs						gs{};
    D3D12PipelineStateSubObject_cs						cs{};
    D3D12PipelineStateSubObject_stream_output			stream_output{};
    D3D12PipelineStateSubObject_blend					blend{ kBlendState.disabled };
    D3D12PipelineStateSubObject_sample_mask				sample_mask{ UINT_MAX };
    D3D12PipelineStateSubObject_rasterizer				rasterizer{ kRasterizerState.no_cull };
    D3D12PipelineStateSubObject_input_layer				input_layer{};
    D3D12PipelineStateSubObject_ib_strip_cut_value		ib_strip_cut_value{};
    D3D12PipelineStateSubObject_primitive_topology		primitive_topology{};
    D3D12PipelineStateSubObject_render_target_formats	render_target_formats{};
    D3D12PipelineStateSubObject_depth_stencil_format		depth_stencil_format{};
    D3D12PipelineStateSubObject_sample_desc				sample_desc{ {.Count = 1, .Quality = 0} };
    D3D12PipelineStateSubObject_node_mask				node_mask{};
    D3D12PipelineStateSubObject_cached_pso				cached_pso{};
    D3D12PipelineStateSubObject_flags					flags{};
    D3D12PipelineStateSubObject_depth_stencil1			depth_stencil1{ kDepthState.disabled };
    D3D12PipelineStateSubObject_view_instancing			view_instancing{};
    D3D12PipelineStateSubObject_as						as{};
    D3D12PipelineStateSubObject_ms						ms{};
  };

  ID3D12PipelineState* create_pipeline_state(const D3D12_PIPELINE_STATE_STREAM_DESC& desc);
  ID3D12PipelineState* create_pipeline_state(void* stream, uint64_t stream_size);

}  // namespace oxygen::renderer::d3d12
