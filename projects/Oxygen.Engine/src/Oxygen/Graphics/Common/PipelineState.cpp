//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Graphics/Common/PipelineState.h>

namespace {
using oxygen::HashCombine;
using oxygen::graphics::RootBindingItem;

inline void HashRootBindings(size_t& seed, std::span<const RootBindingItem> bindings)
{
    for (const auto& binding : bindings) {
        HashCombine(seed, binding.binding_slot_desc.register_index);
        HashCombine(seed, binding.binding_slot_desc.register_space);
        HashCombine(seed, static_cast<int>(binding.visibility));
        HashCombine(seed, binding.GetRootParameterIndex()); // Hash the root parameter index
        std::visit(
            [&seed]<typename BindingType>(const BindingType& item) {
                using T = std::decay_t<BindingType>;
                if constexpr (std::is_same_v<T, oxygen::graphics::PushConstantsBinding>) {
                    HashCombine(seed, item.size);
                } else if constexpr (std::is_same_v<T, oxygen::graphics::DirectBufferBinding>) {
                    (void)0; // Nothing to hash here
                } else if constexpr (std::is_same_v<T, oxygen::graphics::DirectTextureBinding>) {
                    (void)1; // Nothing to hash here
                } else if constexpr (std::is_same_v<T, oxygen::graphics::DescriptorTableBinding>) {
                    HashCombine(seed, static_cast<int>(item.view_type));
                    HashCombine(seed, item.base_index);
                    HashCombine(seed, item.count);
                }
            },
            binding.data);
    }
}
} // anonymous namespace

auto oxygen::graphics::HashGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) noexcept -> size_t
{
    size_t seed = 0;
    // Hash all relevant fields
    if (desc.VertexShader()) {
        HashCombine(seed, desc.VertexShader()->shader);
        if (desc.VertexShader()->entry_point_name) {
            HashCombine(seed, *desc.VertexShader()->entry_point_name);
        }
    }
    if (desc.PixelShader()) {
        HashCombine(seed, desc.PixelShader()->shader);
        if (desc.PixelShader()->entry_point_name) {
            HashCombine(seed, *desc.PixelShader()->entry_point_name);
        }
    }
    if (desc.GeometryShader()) {
        HashCombine(seed, desc.GeometryShader()->shader);
        if (desc.GeometryShader()->entry_point_name) {
            HashCombine(seed, *desc.GeometryShader()->entry_point_name);
        }
    }
    if (desc.HullShader()) {
        HashCombine(seed, desc.HullShader()->shader);
        if (desc.HullShader()->entry_point_name) {
            HashCombine(seed, *desc.HullShader()->entry_point_name);
        }
    }
    if (desc.DomainShader()) {
        HashCombine(seed, desc.DomainShader()->shader);
        if (desc.DomainShader()->entry_point_name) {
            HashCombine(seed, *desc.DomainShader()->entry_point_name);
        }
    }
    HashCombine(seed, static_cast<int>(desc.PrimitiveTopology()));
    // RasterizerStateDesc
    HashCombine(seed, static_cast<int>(desc.RasterizerState().fill_mode));
    HashCombine(seed, static_cast<int>(desc.RasterizerState().cull_mode));
    HashCombine(seed, desc.RasterizerState().front_counter_clockwise);
    HashCombine(seed, desc.RasterizerState().depth_bias);
    HashCombine(seed, desc.RasterizerState().depth_bias_clamp);
    HashCombine(seed, desc.RasterizerState().slope_scaled_depth_bias);
    HashCombine(seed, desc.RasterizerState().depth_clip_enable);
    HashCombine(seed, desc.RasterizerState().multisample_enable);
    HashCombine(seed, desc.RasterizerState().antialiased_line_enable);
    // DepthStencilStateDesc
    HashCombine(seed, desc.DepthStencilState().depth_test_enable);
    HashCombine(seed, desc.DepthStencilState().depth_write_enable);
    HashCombine(seed, static_cast<int>(desc.DepthStencilState().depth_func));
    HashCombine(seed, desc.DepthStencilState().stencil_enable);
    HashCombine(seed, desc.DepthStencilState().stencil_read_mask);
    HashCombine(seed, desc.DepthStencilState().stencil_write_mask);
    // BlendState
    for (const auto& blend : desc.BlendState()) {
        HashCombine(seed, blend.blend_enable);
        HashCombine(seed, static_cast<int>(blend.src_blend));
        HashCombine(seed, static_cast<int>(blend.dest_blend));
        HashCombine(seed, static_cast<int>(blend.blend_op));
        HashCombine(seed, static_cast<int>(blend.src_blend_alpha));
        HashCombine(seed, static_cast<int>(blend.dest_blend_alpha));
        HashCombine(seed, static_cast<int>(blend.blend_op_alpha));
        if (blend.write_mask) {
            HashCombine(seed, static_cast<int>(*blend.write_mask));
        }
    }
    // FramebufferLayoutDesc
    for (const auto& fmt : desc.FramebufferLayout().color_target_formats) {
        HashCombine(seed, static_cast<int>(fmt));
    }
    if (desc.FramebufferLayout().depth_stencil_format) {
        HashCombine(seed, static_cast<int>(*desc.FramebufferLayout().depth_stencil_format));
    }
    HashCombine(seed, desc.FramebufferLayout().sample_count);
    HashCombine(seed, desc.FramebufferLayout().sample_quality);
    // --- Hash root bindings ---
    HashRootBindings(seed, desc.RootBindings());
    return seed;
}

auto oxygen::graphics::HashComputePipelineDesc(const ComputePipelineDesc& desc) noexcept -> size_t
{
    size_t seed = 0;
    HashCombine(seed, desc.ComputeShader().shader);
    if (desc.ComputeShader().entry_point_name) {
        HashCombine(seed, *desc.ComputeShader().entry_point_name);
    }
    // --- Hash root bindings ---
    HashRootBindings(seed, desc.RootBindings());
    return seed;
}

namespace oxygen::graphics::detail {

void RootBindingBuilderHelper::SetRootBindings(
    std::vector<RootBindingItem>& dest,
    std::span<const RootBindingItem> bindings)
{
    if (set_bindings_called_)
        throw std::logic_error("SetRootBindings already called");
    if (!dest.empty())
        throw std::logic_error("Cannot call SetRootBindings after AddRootBinding");
    dest.assign(bindings.begin(), bindings.end());
    for (size_t i = 0; i < dest.size(); ++i) {
        dest[i].SetRootParameterIndex(static_cast<uint32_t>(i));
    }
    next_root_param_index_ = static_cast<uint32_t>(dest.size());
    set_bindings_called_ = true;
}

void RootBindingBuilderHelper::AddRootBinding(
    std::vector<RootBindingItem>& dest,
    const RootBindingItem& binding)
{
    if (set_bindings_called_)
        throw std::logic_error("Cannot call AddRootBinding after SetRootBindings");
    RootBindingItem item = binding;
    item.SetRootParameterIndex(next_root_param_index_++);
    dest.push_back(item);
}

void RootBindingBuilderHelper::Reset()
{
    next_root_param_index_ = 0;
    set_bindings_called_ = false;
}

} // namespace oxygen::graphics::detail
