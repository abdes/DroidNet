//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/Types/Format.h>
#include <Oxygen/Graphics/Common/api_export.h>

namespace oxygen::graphics {

//! Fill mode for polygon rasterization.
enum class FillMode {
    kSolid, //!< Solid fill for polygons.
    kWireframe, //!< Wireframe rendering.

    kMaxFillMode //!< Not a valid mode; sentinel for enum size.
};
OXYGEN_GFX_API std::string to_string(FillMode mode);

//! Polygon face culling mode.
enum class CullMode : uint8_t {
    kNone = 0, //!< No culling.
    kFront = OXYGEN_FLAG(0), //!< Cull front faces.
    kBack = OXYGEN_FLAG(1), //!< Cull back faces.
    kFrontAndBack = kFront | kBack, //!< Cull both front and back faces.

    kMaxCullMode = OXYGEN_FLAG(2) //!< Not a valid mode; sentinel for enum size.
};
OXYGEN_DEFINE_FLAGS_OPERATORS(CullMode)
OXYGEN_GFX_API std::string to_string(CullMode mode);

//! Comparison operation for depth/stencil tests.
enum class CompareOp {
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
OXYGEN_GFX_API std::string to_string(CompareOp op);

//! Blend factor for color blending.
enum class BlendFactor {
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
    kInvSrc1Color, //!< Dual-source blend: inverse color from second color attachment
    kSrc1Alpha, //!< Dual-source blend: alpha from second color attachment
    kInvSrc1Alpha, //!< Dual-source blend: inverse alpha from second color attachment

    kMaxBlendValue //!< Not a valid value; sentinel for enum size.
};
OXYGEN_GFX_API std::string to_string(BlendFactor v);

//! Blend operation for color blending.
enum class BlendOp {
    kAdd, //!< Add source and destination.
    kSubtract, //!< Subtract destination from source.
    kRevSubtract, //!< Subtract source from destination.
    kMin, //!< Minimum of source and destination.
    kMax, //!< Maximum of source and destination.

    kMaxBlendOp //!< Not a valid op; sentinel for enum size.
};
OXYGEN_GFX_API std::string to_string(BlendOp op);

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
OXYGEN_GFX_API std::string to_string(ColorWriteMask mask);

//! Primitive topology for input assembly.
enum class PrimitiveType {
    kPointList, //!< Points.
    kLineList, //!< Lines.
    kLineStrip, //!< Line strips.
    kLineStripWithRestartEnable, //!< Line strips with primitive restart enabled.
    kTriangleList, //!< Triangles.
    kTriangleStrip, //!< Triangle strips.
    kTriangleStripWithRestartEnable, //!< Triangle strips with primitive restart enabled.
    kPatchList, //!< Patches (tessellation).
    kLineListWithAdjacency, //!< Line list with adjacency information
    kLineStripWithAdjacency, //!< Line strip with adjacency information
    kTriangleListWithAdjacency, //!< Triangle list with adjacency information
    kTriangleStripWithAdjacency, //!< Triangle strip with adjacency information

    kMaxPrimitiveType //!< Not a valid type; sentinel for enum size.
};
OXYGEN_GFX_API std::string to_string(PrimitiveType t);

//! Describes a single programmable shader stage.
struct ShaderStageDesc {
    std::string shader; //!< Unique string ID of the compiled shader (see ShaderManager).
    std::optional<std::string> entry_point_name; //!< Optional: entry point for multi-entry shaders.
};

//! Configures how primitives are rasterized, including fill mode, culling, depth bias, and multisampling options.
struct RasterizerStateDesc {
    FillMode fill_mode { FillMode::kSolid }; //!< Fill mode for polygons (solid or wireframe).
    CullMode cull_mode { CullMode::kBack }; //!< Face culling mode for polygons.
    bool front_counter_clockwise { false }; //!< True if front-facing polygons have counter-clockwise winding.
    float depth_bias { 0.0f }; //!< Constant depth value added to each pixel.
    float depth_bias_clamp { 0.0f }; //!< Maximum depth bias value.
    float slope_scaled_depth_bias { 0.0f }; //!< Depth bias scale factor for polygon slope.
    bool depth_clip_enable { true }; //!< Enable clipping based on depth.
    bool multisample_enable { false }; //!< Enable MSAA.
    bool antialiased_line_enable { false }; //!< Enable line antialiasing.
};

//! Controls depth buffer and stencil buffer operations, including testing, writing, and comparison functions.
struct DepthStencilStateDesc {
    bool depth_test_enable { false }; //!< Enable depth testing.
    bool depth_write_enable { false }; //!< Enable writing to depth buffer.
    CompareOp depth_func { CompareOp::kLess }; //!< Comparison function for depth testing.
    bool stencil_enable { false }; //!< Enable stencil testing.
    uint8_t stencil_read_mask { 0xFF }; //!< Mask for reading from stencil buffer.
    uint8_t stencil_write_mask { 0xFF }; //!< Mask for writing to stencil buffer.
    // Stencil ops/funcs can be extended here.
};

//! Defines color and alpha blending operations and write masks for a single render target attachment.
struct BlendTargetDesc {
    bool blend_enable { false }; //!< Enable blending for this render target.
    BlendFactor src_blend { BlendFactor::kOne }; //!< Source color blend factor.
    BlendFactor dest_blend { BlendFactor::kZero }; //!< Destination color blend factor.
    BlendOp blend_op { BlendOp::kAdd }; //!< Color blend operation.
    BlendFactor src_blend_alpha { BlendFactor::kOne }; //!< Source alpha blend factor.
    BlendFactor dest_blend_alpha { BlendFactor::kZero }; //!< Destination alpha blend factor.
    BlendOp blend_op_alpha { BlendOp::kAdd }; //!< Alpha blend operation.
    std::optional<ColorWriteMask> write_mask { ColorWriteMask::kAll }; //!< Channel write mask.
};

//! Specifies the complete attachment layout for a framebuffer, including color formats, depth/stencil format, and MSAA configuration.
struct FramebufferLayoutDesc {
    std::vector<Format> color_target_formats; //!< Array of color attachment formats, empty if using depth-only.
    std::optional<Format> depth_stencil_format; //!< Optional depth/stencil attachment format.
    uint32_t sample_count { 1 }; //!< Number of MSAA samples (1 for no multisampling).
    // Optional: shading rate attachment, etc.
};

// -------------------------------------------------------------------------- //
//! Shader Pipeline
// -------------------------------------------------------------------------- //

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

    [[nodiscard]] constexpr auto VertexShader() const noexcept -> const std::optional<ShaderStageDesc>&
    {
        return vertex_shader_;
    }

    //! Get pixel/fragment shader stage.
    [[nodiscard]] constexpr auto PixelShader() const noexcept -> const std::optional<ShaderStageDesc>&
    {
        return pixel_shader_;
    }

    //! Get geometry shader stage.
    [[nodiscard]] constexpr auto GeometryShader() const noexcept -> const std::optional<ShaderStageDesc>&
    {
        return geometry_shader_;
    }

    //! Get hull/tessellation control shader stage.
    [[nodiscard]] constexpr auto HullShader() const noexcept -> const std::optional<ShaderStageDesc>&
    {
        return hull_shader_;
    }

    //! Get domain/tessellation evaluation shader stage.
    [[nodiscard]] constexpr auto DomainShader() const noexcept -> const std::optional<ShaderStageDesc>&
    {
        return domain_shader_;
    }

    //! Get primitive topology.
    [[nodiscard]] constexpr auto PrimitiveTopology() const noexcept -> PrimitiveType
    {
        return primitive_topology_;
    }

    //! Get rasterizer state.
    [[nodiscard]] constexpr auto RasterizerState() const noexcept -> const RasterizerStateDesc&
    {
        return rasterizer_state_;
    }

    //! Get depth/stencil state.
    [[nodiscard]] constexpr auto DepthStencilState() const noexcept -> const DepthStencilStateDesc&
    {
        return depth_stencil_state_;
    }

    //! Get blend state per render target.
    [[nodiscard]] constexpr auto BlendState() const noexcept -> const std::vector<BlendTargetDesc>&
    {
        return blend_state_;
    }

    //! Get framebuffer layout.
    [[nodiscard]] constexpr auto FramebufferLayout() const noexcept -> const FramebufferLayoutDesc&
    {
        return framebuffer_layout_;
    }

private:
    friend class Builder;
    GraphicsPipelineDesc() = default; // Only constructed by Builder.

    std::optional<ShaderStageDesc> vertex_shader_; //!< Vertex shader stage.
    std::optional<ShaderStageDesc> pixel_shader_; //!< Pixel/fragment shader stage.
    std::optional<ShaderStageDesc> geometry_shader_; //!< Geometry shader stage.
    std::optional<ShaderStageDesc> hull_shader_; //!< Hull/tessellation control shader.
    std::optional<ShaderStageDesc> domain_shader_; //!< Domain/tessellation evaluation shader.

    PrimitiveType primitive_topology_ { PrimitiveType::kTriangleList }; //!< Primitive topology.

    RasterizerStateDesc rasterizer_state_; //!< Rasterizer state.
    DepthStencilStateDesc depth_stencil_state_; //!< Depth/stencil state.
    std::vector<BlendTargetDesc> blend_state_; //!< Blend state per render target.

    FramebufferLayoutDesc framebuffer_layout_; //!< Framebuffer layout.

    // TODO: allow to include an opaque handle to a pipeline cache.
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
    constexpr auto SetPrimitiveTopology(PrimitiveType type) && -> Builder&&
    {
        desc_.primitive_topology_ = type;
        return std::move(*this);
    } //! Set rasterizer state.
    auto SetRasterizerState(RasterizerStateDesc state) && -> Builder&&
    {
        desc_.rasterizer_state_ = std::move(state);
        return std::move(*this);
    }

    //! Set depth/stencil state.
    auto SetDepthStencilState(DepthStencilStateDesc state) && -> Builder&&
    {
        desc_.depth_stencil_state_ = std::move(state);
        return std::move(*this);
    }

    //! Set blend state for all render targets.
    auto SetBlendState(std::vector<BlendTargetDesc> state) && -> Builder&&
    {
        desc_.blend_state_ = std::move(state);
        return std::move(*this);
    }

    //! Add blend state for a single render target.
    auto AddBlendTarget(BlendTargetDesc target) && -> Builder&&
    {
        desc_.blend_state_.push_back(std::move(target));
        return std::move(*this);
    }

    //! Set framebuffer layout.
    auto SetFramebufferLayout(FramebufferLayoutDesc layout) && -> Builder&&
    {
        desc_.framebuffer_layout_ = std::move(layout);
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
        const auto& fb = desc_.framebuffer_layout_;
        if (fb.color_target_formats.empty() && !fb.depth_stencil_format) {
            throw std::runtime_error("GraphicsPipelineDesc requires at least one render target format or depth/stencil format");
        }
        if (fb.sample_count < 1) {
            throw std::runtime_error("GraphicsPipelineDesc sample count must be at least 1");
        }

        return std::move(desc_);
    }

private:
    GraphicsPipelineDesc desc_;
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

    [[nodiscard]] constexpr auto ComputeShader() const noexcept -> const ShaderStageDesc&
    {
        return compute_shader_;
    }

private:
    friend class Builder;
    explicit ComputePipelineDesc(ShaderStageDesc shader)
        : compute_shader_(std::move(shader))
    {
    }

    ShaderStageDesc compute_shader_;
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

    //! Build the immutable ComputePipelineDesc.
    /*!
     \throws std::runtime_error if compute shader is not set.
    */
    [[nodiscard]] auto Build() && -> ComputePipelineDesc
    {
        if (!has_compute_shader_) {
            throw std::runtime_error("ComputePipelineDesc requires a compute shader");
        }
        return ComputePipelineDesc { std::move(compute_shader_) };
    }

private:
    ShaderStageDesc compute_shader_;
    bool has_compute_shader_ = false;
};

} // namespace oxygen::graphics
