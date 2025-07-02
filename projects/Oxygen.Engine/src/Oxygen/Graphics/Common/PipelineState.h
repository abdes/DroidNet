//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Fill mode for polygon rasterization.
enum class FillMode : uint8_t {
  kSolid, //!< Solid fill for polygons.
  kWireFrame, //!< Wire-frame rendering.

  kMaxFillMode //!< Not a valid mode; sentinel for enum size.
};
OXYGEN_GFX_API auto to_string(FillMode mode) -> std::string;

//! Polygon face culling mode.
enum class CullMode : uint8_t {
  kNone = 0, //!< No culling.
  kFront = OXYGEN_FLAG(0), //!< Cull front faces.
  kBack = OXYGEN_FLAG(1), //!< Cull back faces.
  kFrontAndBack = kFront | kBack, //!< Cull both front and back faces.

  kMaxCullMode = OXYGEN_FLAG(2) //!< Not a valid mode; sentinel for enum size.
};
OXYGEN_DEFINE_FLAGS_OPERATORS(CullMode)
OXYGEN_GFX_API auto to_string(CullMode value) -> std::string;

//! Comparison operation for depth/stencil tests.
enum class CompareOp : uint8_t {
  kNever, //!< Never passes.
  kLess, //!< Passes if source < dest.
  kEqual, //!< Passes if source == dest.
  kLessOrEqual, //!< Passes if source <= dest.
  kGreater, //!< Passes if source > dest.
  kNotEqual, //!< Passes if source != dest.
  kGreaterOrEqual, //!< Passes if source >= dest.
  kAlways, //!< Always passes.

  kMaxCompareOp //!< Not a valid op; sentinel for enum size.
};
OXYGEN_GFX_API auto to_string(CompareOp value) -> std::string;

//! Blend factor for color blending.
enum class BlendFactor : uint8_t {
  kZero, //!< 0.0 blend factor.
  kOne, //!< 1.0 blend factor.
  kSrcColor, //!< Source color.
  kInvSrcColor, //!< 1 - source color.
  kSrcAlpha, //!< Source alpha.
  kInvSrcAlpha, //!< 1 - source alpha.
  kDestColor, //!< Destination color.
  kInvDestColor, //!< 1 - destination color.
  kDestAlpha, //!< Destination alpha.
  kInvDestAlpha, //!< 1 - destination alpha.
  kConstantColor, //!< Constant color blend factor
  kInvConstantColor, //!< Inverse constant color blend factor
  kSrc1Color, //!< Dual-source blend: color from second color attachment
  kInvSrc1Color, //!< Dual-source blend: inverse color from second color
                 //!< attachment
  kSrc1Alpha, //!< Dual-source blend: alpha from second color attachment
  kInvSrc1Alpha, //!< Dual-source blend: inverse alpha from second color
                 //!< attachment

  kMaxBlendValue //!< Not a valid value; sentinel for enum size.
};
OXYGEN_GFX_API auto to_string(BlendFactor value) -> std::string;

//! Blend operation for color blending.
enum class BlendOp : uint8_t {
  kAdd, //!< Add source and destination.
  kSubtract, //!< Subtract destination from source.
  kRevSubtract, //!< Subtract source from destination.
  kMin, //!< Minimum of source and destination.
  kMax, //!< Maximum of source and destination.

  kMaxBlendOp //!< Not a valid op; sentinel for enum size.
};
OXYGEN_GFX_API auto to_string(BlendOp value) -> std::string;

//! Color write mask for render targets.
enum class ColorWriteMask : uint8_t {
  kNone = 0, //!< No color channels.
  kR = OXYGEN_FLAG(0), //!< Red channel.
  kG = OXYGEN_FLAG(1), //!< Green channel.
  kB = OXYGEN_FLAG(2), //!< Blue channel.
  kA = OXYGEN_FLAG(3), //!< Alpha channel.
  kAll = kR | kG | kB | kA, //!< All color channels enabled.

  kMaxColorWriteMask = 0x10 //!< Not a valid mask; sentinel for enum size.
};
OXYGEN_DEFINE_FLAGS_OPERATORS(ColorWriteMask)
OXYGEN_GFX_API auto to_string(ColorWriteMask value) -> std::string;

//! Primitive topology for input assembly.
enum class PrimitiveType : uint8_t {
  kPointList, //!< Points.
  kLineList, //!< Lines.
  kLineStrip, //!< Line strips.
  kLineStripWithRestartEnable, //!< Line strips with primitive restart enabled.
  kTriangleList, //!< Triangles.
  kTriangleStrip, //!< Triangle strips.
  kTriangleStripWithRestartEnable, //!< Triangle strips with primitive restart
                                   //!< enabled.
  kPatchList, //!< Patches (tessellation).
  kLineListWithAdjacency, //!< Line list with adjacency information
  kLineStripWithAdjacency, //!< Line strip with adjacency information
  kTriangleListWithAdjacency, //!< Triangle list with adjacency information
  kTriangleStripWithAdjacency, //!< Triangle strip with adjacency information

  kMaxPrimitiveType //!< Not a valid type; sentinel for enum size.
};
OXYGEN_GFX_API auto to_string(PrimitiveType value) -> std::string;

//! Describes a single programmable shader stage.
struct ShaderStageDesc {
  std::string
    shader; //!< Unique string ID of the compiled shader (see ShaderManager).
  std::optional<std::string> entry_point_name {};

  auto operator==(const ShaderStageDesc&) const -> bool = default;
};

//! Configures how primitives are rasterized, including fill mode, culling,
//! depth bias, and multisampling options.
struct RasterizerStateDesc {
  FillMode fill_mode {
    FillMode::kSolid
  }; //!< Fill mode for polygons (solid or wire-frame).
  CullMode cull_mode { CullMode::kBack }; //!< Face culling mode for polygons.

  //!< True if front-facing polygons have counter-clockwise winding.
  /*!
   In graphics programming, counter-clockwise (CCW) winding order is the most
   commonly used convention to specify the order of vertices for a polygon.
   This means that when looking at a polygon from the front, the vertices are
   specified in a counter-clockwise order.
  */
  bool front_counter_clockwise { true };

  float depth_bias { 0.0f }; //!< Constant depth value added to each pixel.
  float depth_bias_clamp { 0.0f }; //!< Maximum depth bias value.
  float slope_scaled_depth_bias {
    0.0f
  }; //!< Depth bias scale factor for polygon slope.
  bool depth_clip_enable { true }; //!< Enable clipping based on depth.
  bool multisample_enable { false }; //!< Enable MSAA.
  bool antialiased_line_enable { false }; //!< Enable line antialiasing.

  auto operator==(const RasterizerStateDesc&) const -> bool = default;

  //! Static factory for no culling rasterizer state.
  static RasterizerStateDesc NoCulling()
  {
    return RasterizerStateDesc {
      .cull_mode = CullMode::kNone,
    };
  }

  //! Static factory for back-face culling rasterizer state.
  static RasterizerStateDesc BackFaceCulling()
  {
    return RasterizerStateDesc {
      .cull_mode = CullMode::kBack,
    };
  }

  //! Static factory for front-face culling rasterizer state.
  static RasterizerStateDesc FrontFaceCulling()
  {
    return RasterizerStateDesc {
      .cull_mode = CullMode::kFront,
    };
  }

  //! Static factory for wireframe rasterizer state, with no culling.
  static RasterizerStateDesc WireframeNoCulling()
  {
    return RasterizerStateDesc {
      .fill_mode = FillMode::kWireFrame,
      .cull_mode = CullMode::kNone,
    };
  }

  //! Static factory for wireframe rasterizer state, with back-face culling.
  static RasterizerStateDesc WireframeBackFaceCulling()
  {
    return RasterizerStateDesc {
      .fill_mode = FillMode::kWireFrame,
      .cull_mode = CullMode::kBack,
    };
  }

  //! Static factory for wireframe rasterizer state, with front-face culling.
  static RasterizerStateDesc WireframeFrontFaceCulling()
  {
    return RasterizerStateDesc {
      .fill_mode = FillMode::kWireFrame,
      .cull_mode = CullMode::kFront,
    };
  }
};

//! Controls depth buffer and stencil buffer operations, including testing,
//! writing, and comparison functions.
struct DepthStencilStateDesc {
  bool depth_test_enable { false }; //!< Enable depth testing.
  bool depth_write_enable { false }; //!< Enable writing to depth buffer.
  CompareOp depth_func {
    CompareOp::kLess
  }; //!< Comparison function for depth testing.
  bool stencil_enable { false }; //!< Enable stencil testing.
  uint8_t stencil_read_mask { 0xFF }; //!< Mask for reading from stencil buffer.
  uint8_t stencil_write_mask { 0xFF }; //!< Mask for writing to stencil buffer.

  auto operator==(const DepthStencilStateDesc&) const -> bool = default;

  //! Static factory for depth/stencil state with all operations disabled.
  static DepthStencilStateDesc Disabled()
  {
    return DepthStencilStateDesc { .depth_test_enable = false,
      .depth_write_enable = false,
      .stencil_enable = false };
  }
};

//! Defines color and alpha blending operations and write masks for a single
//! render target attachment.
struct BlendTargetDesc {
  bool blend_enable { false }; //!< Enable blending for this render target.
  BlendFactor src_blend { BlendFactor::kOne }; //!< Source color blend factor.
  BlendFactor dest_blend {
    BlendFactor::kZero
  }; //!< Destination color blend factor.
  BlendOp blend_op { BlendOp::kAdd }; //!< Color blend operation.
  BlendFactor src_blend_alpha {
    BlendFactor::kOne
  }; //!< Source alpha blend factor.
  BlendFactor dest_blend_alpha {
    BlendFactor::kZero
  }; //!< Destination alpha blend factor.
  BlendOp blend_op_alpha { BlendOp::kAdd }; //!< Alpha blend operation.
  std::optional<ColorWriteMask> write_mask {
    ColorWriteMask::kAll
  }; //!< Channel write mask.

  auto operator==(const BlendTargetDesc&) const -> bool = default;
};

//! Specifies the complete attachment layout for a framebuffer, including color
//! formats, depth/stencil format, and MSAA configuration.
struct FramebufferLayoutDesc {
  std::vector<Format>
    color_target_formats; //!< Array of color attachment formats, empty if using
                          //!< depth-only.
  std::optional<Format>
    depth_stencil_format {}; //!< Optional depth/stencil attachment format.
  uint32_t sample_count {
    1
  }; //!< Number of MSAA samples (1 for no multisampling).
  uint32_t sample_quality {
    0
  }; //!< MSAA quality level (0 = default/highest available).

  auto operator==(const FramebufferLayoutDesc&) const -> bool = default;
};

// -------------------------------------------------------------------------- //
// Root Binding and Descriptor Tables
// -------------------------------------------------------------------------- //

//! Describes a single binding slot in a backend-neutral way (register/binding
//! index and space/set).
struct BindingSlotDesc {
  uint32_t register_index; // bN/tN/uN or binding N
  uint32_t register_space; // space# (D3D12) or set# (Vulkan)

  auto operator==(const BindingSlotDesc&) const -> bool = default;
};

//! Descriptor table binding.
/*!
  The actual descriptor table can be determined from the resource view type.
  When a table contains views of multiple types, it is not necessary to create
  binding items for all of them. One of the view types is sufficient to
  represent the entire table.
*/
struct DescriptorTableBinding {
  ResourceViewType view_type = ResourceViewType::kNone;
  uint32_t base_index = 0;
  uint32_t count = std::numeric_limits<uint32_t>::max();
  auto operator==(const DescriptorTableBinding&) const -> bool = default;
};

//! Push constant data (only one single range per item).
struct PushConstantsBinding {
  uint32_t size
    = 0; //! The number of 32-bit integers in the push constant range.
  auto operator==(const PushConstantsBinding&) const -> bool = default;
};

//! Direct buffer binding: one descriptor handle only (no view description
//! needed)
struct DirectBufferBinding {
  // No handle needed at pipeline creation time
  auto operator==(const DirectBufferBinding&) const -> bool = default;
};

//! Direct texture binding: one descriptor handle only (no view description
//! needed)
struct DirectTextureBinding {
  // No handle needed at pipeline creation time
  auto operator==(const DirectTextureBinding&) const -> bool = default;
};

//! Root binding item for pipeline root signature or descriptor set layout.
struct RootBindingDesc {
  BindingSlotDesc binding_slot_desc {};
  ShaderStageFlags visibility = ShaderStageFlags::kAll;
  std::variant<PushConstantsBinding, DirectBufferBinding, DirectTextureBinding,
    DescriptorTableBinding>
    data;
};

//! Root binding item for pipeline root signature or descriptor set layout.
struct RootBindingItem {
  BindingSlotDesc binding_slot_desc {};
  ShaderStageFlags visibility = ShaderStageFlags::kAll;
  std::variant<PushConstantsBinding, DirectBufferBinding, DirectTextureBinding,
    DescriptorTableBinding>
    data;

  // ReSharper disable once CppNonExplicitConvertingConstructor
  RootBindingItem(const RootBindingDesc& desc)
    : binding_slot_desc(desc.binding_slot_desc)
    , visibility(desc.visibility)
    , data(desc.data)
  {
  }

  [[nodiscard]] auto GetRootParameterIndex() const noexcept -> uint32_t
  {
    return root_parameter_index_;
  }
  void SetRootParameterIndex(const uint32_t idx)
  {
    if (root_parameter_index_ != ~0u)
      throw std::logic_error(
        "RootBindingItem: root_parameter_index already set");
    root_parameter_index_ = idx;
  }

  auto operator==(const RootBindingItem&) const -> bool = default;

private:
  uint32_t root_parameter_index_ = ~0u;
};

// -------------------------------------------------------------------------- //
// Graphics Pipeline
// -------------------------------------------------------------------------- //

namespace detail {
  // Helper for root binding index allocation and mutual exclusion
  class RootBindingBuilderHelper {
  public:
    RootBindingBuilderHelper() = default;

    OXYGEN_GFX_API void SetRootBindings(std::vector<RootBindingItem>& dest,
      std::span<const RootBindingItem> bindings);

    OXYGEN_GFX_API void AddRootBinding(
      std::vector<RootBindingItem>& dest, const RootBindingItem& binding);

    OXYGEN_GFX_API void Reset();

  private:
    uint32_t next_root_param_index_ = 0;
    bool set_bindings_called_ = false;
  };
} // namespace detail

//! Describes a complete graphics pipeline state object.
/*!
 We exclusively use bindless rendering, so no input layout is needed.
 The pipeline state object is immutable after creation.
*/
class GraphicsPipelineDesc {
public:
  class Builder; //! Get vertex shader stage.

  ~GraphicsPipelineDesc() = default;

  OXYGEN_DEFAULT_COPYABLE(GraphicsPipelineDesc)
  OXYGEN_DEFAULT_MOVABLE(GraphicsPipelineDesc)

  auto operator==(const GraphicsPipelineDesc&) const -> bool = default;

  [[nodiscard]] constexpr auto VertexShader() const noexcept
    -> const std::optional<ShaderStageDesc>&
  {
    return vertex_shader_;
  }

  //! Get pixel/fragment shader stage.
  [[nodiscard]] constexpr auto PixelShader() const noexcept
    -> const std::optional<ShaderStageDesc>&
  {
    return pixel_shader_;
  }

  //! Get geometry shader stage.
  [[nodiscard]] constexpr auto GeometryShader() const noexcept
    -> const std::optional<ShaderStageDesc>&
  {
    return geometry_shader_;
  }

  //! Get hull/tessellation control shader stage.
  [[nodiscard]] constexpr auto HullShader() const noexcept
    -> const std::optional<ShaderStageDesc>&
  {
    return hull_shader_;
  }

  //! Get domain/tessellation evaluation shader stage.
  [[nodiscard]] constexpr auto DomainShader() const noexcept
    -> const std::optional<ShaderStageDesc>&
  {
    return domain_shader_;
  }

  //! Get primitive topology.
  [[nodiscard]] constexpr auto PrimitiveTopology() const noexcept
    -> PrimitiveType
  {
    return primitive_topology_;
  }

  //! Get rasterizer state.
  [[nodiscard]] constexpr auto RasterizerState() const noexcept
    -> const RasterizerStateDesc&
  {
    return rasterizer_state_;
  }

  //! Get depth/stencil state.
  [[nodiscard]] constexpr auto DepthStencilState() const noexcept
    -> const DepthStencilStateDesc&
  {
    return depth_stencil_state_;
  }

  //! Get blend state per render target.
  [[nodiscard]] constexpr auto BlendState() const noexcept
    -> const std::vector<BlendTargetDesc>&
  {
    return blend_state_;
  }

  //! Get framebuffer layout.
  [[nodiscard]] constexpr auto FramebufferLayout() const noexcept
    -> const FramebufferLayoutDesc&
  {
    return framebuffer_layout_;
  }

  //! Get debug name for this pipeline.
  [[nodiscard]] constexpr auto GetName() const noexcept -> const std::string&
  {
    return debug_name_;
  }

  // Add accessor for root bindings using std::span
  [[nodiscard]] constexpr auto RootBindings() const noexcept
    -> std::span<const RootBindingItem>
  {
    return { root_bindings_.data(), root_bindings_.size() };
  }

private:
  friend class Builder;
  GraphicsPipelineDesc() = default; // Only constructed by Builder.

  std::optional<ShaderStageDesc> vertex_shader_; //!< Vertex shader stage.
  std::optional<ShaderStageDesc>
    pixel_shader_; //!< Pixel/fragment shader stage.
  std::optional<ShaderStageDesc> geometry_shader_; //!< Geometry shader stage.
  std::optional<ShaderStageDesc>
    hull_shader_; //!< Hull/tessellation control shader.
  std::optional<ShaderStageDesc>
    domain_shader_; //!< Domain/tessellation evaluation shader.

  PrimitiveType primitive_topology_ {
    PrimitiveType::kTriangleList
  }; //!< Primitive topology.

  RasterizerStateDesc rasterizer_state_; //!< Rasterizer state.
  DepthStencilStateDesc depth_stencil_state_; //!< Depth/stencil state.
  std::vector<BlendTargetDesc> blend_state_; //!< Blend state per render target.

  FramebufferLayoutDesc framebuffer_layout_; //!< Framebuffer layout.

  std::string debug_name_ { "GraphicsPipeline" };

  std::vector<RootBindingItem> root_bindings_;
};

//! Builder for GraphicsPipelineDesc using modern C++20 patterns.
class GraphicsPipelineDesc::Builder {
public:
  Builder() = default;
  ~Builder() = default;

  OXYGEN_MAKE_NON_COPYABLE(Builder)
  OXYGEN_DEFAULT_MOVABLE(Builder)

  //! Set vertex shader stage.
  auto SetVertexShader(ShaderStageDesc shader) && -> Builder&&
  {
    desc_.vertex_shader_ = std::move(shader);
    return std::move(*this);
  }

  //! Set pixel/fragment shader stage.
  auto SetPixelShader(ShaderStageDesc shader) && -> Builder&&
  {
    desc_.pixel_shader_ = std::move(shader);
    return std::move(*this);
  }

  //! Set geometry shader stage.
  auto SetGeometryShader(ShaderStageDesc shader) && -> Builder&&
  {
    desc_.geometry_shader_ = std::move(shader);
    return std::move(*this);
  }

  //! Set hull/tessellation control shader stage.
  auto SetHullShader(ShaderStageDesc shader) && -> Builder&&
  {
    desc_.hull_shader_ = std::move(shader);
    return std::move(*this);
  }

  //! Set domain/tessellation evaluation shader stage.
  auto SetDomainShader(ShaderStageDesc shader) && -> Builder&&
  {
    desc_.domain_shader_ = std::move(shader);
    return std::move(*this);
  }

  //! Set primitive topology.
  constexpr auto SetPrimitiveTopology(const PrimitiveType type) && -> Builder&&
  {
    desc_.primitive_topology_ = type;
    return std::move(*this);
  } //! Set rasterizer state.
  auto SetRasterizerState(const RasterizerStateDesc& state) && -> Builder&&
  {
    static_assert(std::is_trivially_copyable_v<RasterizerStateDesc>);
    desc_.rasterizer_state_ = state;
    return std::move(*this);
  }

  //! Set depth/stencil state.
  auto SetDepthStencilState(const DepthStencilStateDesc& state) && -> Builder&&
  {
    static_assert(std::is_trivially_copyable_v<DepthStencilStateDesc>);
    desc_.depth_stencil_state_ = state;
    return std::move(*this);
  }

  //! Set blend state for all render targets.
  auto SetBlendState(std::vector<BlendTargetDesc> state) && -> Builder&&
  {
    desc_.blend_state_ = std::move(state);
    return std::move(*this);
  }

  //! Add blend state for a single render target.
  auto AddBlendTarget(const BlendTargetDesc& target) && -> Builder&&
  {
    static_assert(std::is_trivially_copyable_v<BlendTargetDesc>);
    desc_.blend_state_.push_back(target);
    return std::move(*this);
  }

  //! Set framebuffer layout.
  auto SetFramebufferLayout(FramebufferLayoutDesc layout) && -> Builder&&
  {
    desc_.framebuffer_layout_ = std::move(layout);
    return std::move(*this);
  }

  //! Set debug name for the pipeline.
  auto SetDebugName(std::string name) && -> Builder&&
  {
    desc_.debug_name_ = std::move(name);
    return std::move(*this);
  }

  //! Add support for root bindings
  //! Set root bindings for the pipeline. Mutually exclusive with
  //! AddRootBinding. You must call either SetRootBindings or AddRootBinding,
  //! not both.
  auto SetRootBindings(
    std::span<const RootBindingItem> bindings) && -> Builder&&
  {
    root_binding_helper_.SetRootBindings(desc_.root_bindings_, bindings);
    return std::move(*this);
  }
  //! Add a single root binding. Mutually exclusive with SetRootBindings.
  //! You must call either AddRootBinding or SetRootBindings, not both.
  auto AddRootBinding(const RootBindingItem& binding) && -> Builder&&
  {
    root_binding_helper_.AddRootBinding(desc_.root_bindings_, binding);
    return std::move(*this);
  }

  //! Build the immutable GraphicsPipelineDesc.
  /*!
   \throws std::runtime_error if required components are missing.
  */
  [[nodiscard]] auto Build() && -> GraphicsPipelineDesc
  {
    if (!desc_.vertex_shader_) {
      throw std::runtime_error("GraphicsPipelineDesc requires a vertex shader");
    }
    if (!desc_.pixel_shader_) {
      throw std::runtime_error("GraphicsPipelineDesc requires a pixel shader");
    }

    // Validate framebuffer layout
    const auto& [color_target_formats, depth_stencil_format, sample_count,
      sample_quality]
      = desc_.framebuffer_layout_;
    if (color_target_formats.empty() && !depth_stencil_format) {
      throw std::runtime_error("GraphicsPipelineDesc requires at least one "
                               "render target format or depth/stencil format");
    }
    if (sample_count < 1) {
      throw std::runtime_error(
        "GraphicsPipelineDesc sample count must be at least 1");
    }

    root_binding_helper_.Reset();
    return std::move(desc_);
  }

private:
  GraphicsPipelineDesc desc_;
  detail::RootBindingBuilderHelper root_binding_helper_;
};

// -------------------------------------------------------------------------- //
//! Compute Pipeline
// -------------------------------------------------------------------------- //

//! Describes a compute pipeline state object.
/*!
 The pipeline state object is immutable after creation.
*/
class ComputePipelineDesc {
public:
  class Builder; //! Get compute shader stage.

  ~ComputePipelineDesc() = default;

  OXYGEN_DEFAULT_COPYABLE(ComputePipelineDesc)
  OXYGEN_DEFAULT_MOVABLE(ComputePipelineDesc)

  auto operator==(const ComputePipelineDesc&) const -> bool = default;

  [[nodiscard]] constexpr auto ComputeShader() const noexcept
    -> const ShaderStageDesc&
  {
    return compute_shader_;
  }

  //! Get debug name for this compute pipeline.
  [[nodiscard]] constexpr auto GetName() const noexcept -> const std::string&
  {
    return debug_name_;
  }

  [[nodiscard]] constexpr auto RootBindings() const noexcept
    -> std::span<const RootBindingItem>
  {
    return std::span<const RootBindingItem>(
      root_bindings_.data(), root_bindings_.size());
  }

private:
  friend class Builder;
  explicit ComputePipelineDesc(ShaderStageDesc shader, std::string debug_name)
    : compute_shader_(std::move(shader))
    , debug_name_(std::move(debug_name))
  {
  }

  ShaderStageDesc compute_shader_;
  std::string debug_name_;
  std::vector<RootBindingItem> root_bindings_;
};

//! Builder for ComputePipelineDesc using modern C++20 patterns.
class ComputePipelineDesc::Builder {
public:
  Builder() = default;
  ~Builder() = default;

  OXYGEN_MAKE_NON_COPYABLE(Builder)
  OXYGEN_DEFAULT_MOVABLE(Builder)

  //! Set compute shader stage.
  auto SetComputeShader(ShaderStageDesc shader) && -> Builder&&
  {
    compute_shader_ = std::move(shader);
    has_compute_shader_ = true;
    return std::move(*this);
  }

  //! Set debug name for the compute pipeline.
  auto SetDebugName(std::string name) && -> Builder&&
  {
    debug_name_ = std::move(name);
    return std::move(*this);
  }

  //! Set root bindings for the compute pipeline. Mutually exclusive with
  //! AddRootBinding. You must call either SetRootBindings or AddRootBinding,
  //! not both.
  auto SetRootBindings(
    std::span<const RootBindingItem> bindings) && -> Builder&&
  {
    root_binding_helper_.SetRootBindings(root_bindings_, bindings);
    return std::move(*this);
  }
  //! Add a single root binding. Mutually exclusive with SetRootBindings.
  //! You must call either AddRootBinding or SetRootBindings, not both.
  auto AddRootBinding(const RootBindingItem& binding) && -> Builder&&
  {
    root_binding_helper_.AddRootBinding(root_bindings_, binding);
    return std::move(*this);
  }

  //! Build the immutable ComputePipelineDesc.
  /*!
   \throws std::runtime_error if compute shader is not set.
  */
  [[nodiscard]] auto Build() && -> ComputePipelineDesc
  {
    if (!has_compute_shader_) {
      throw std::runtime_error("ComputePipelineDesc requires a compute shader");
    }
    ComputePipelineDesc desc { std::move(compute_shader_),
      std::move(debug_name_) };
    if (!root_bindings_.empty()) {
      desc.root_bindings_ = std::move(root_bindings_);
    }

    root_binding_helper_.Reset();
    return desc;
  }

private:
  ShaderStageDesc compute_shader_;
  bool has_compute_shader_ = false;
  std::string debug_name_ { "ComputePipeline" };
  std::vector<RootBindingItem> root_bindings_;
  detail::RootBindingBuilderHelper root_binding_helper_;
};

OXYGEN_GFX_API auto HashGraphicsPipelineDesc(
  const GraphicsPipelineDesc& desc) noexcept -> size_t;
OXYGEN_GFX_API auto HashComputePipelineDesc(
  const ComputePipelineDesc& desc) noexcept -> size_t;

} // namespace oxygen::graphics

template <> struct std::hash<oxygen::graphics::GraphicsPipelineDesc> {
  auto operator()(
    const oxygen::graphics::GraphicsPipelineDesc& desc) const noexcept -> size_t
  {
    return oxygen::graphics::HashGraphicsPipelineDesc(desc);
  }
};

template <> struct std::hash<oxygen::graphics::ComputePipelineDesc> {
  auto operator()(
    const oxygen::graphics::ComputePipelineDesc& desc) const noexcept -> size_t
  {
    return oxygen::graphics::HashComputePipelineDesc(desc);
  }
};
