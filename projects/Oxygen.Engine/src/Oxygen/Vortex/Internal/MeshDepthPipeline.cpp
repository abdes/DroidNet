//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/MeshDepthPipeline.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>

namespace oxygen::vortex::internal {
namespace {
  auto AddBooleanDefine(const bool enabled, std::string_view name,
    std::vector<graphics::ShaderDefine>& defines) -> void
  {
    if (enabled) {
      defines.push_back(
        graphics::ShaderDefine { .name = std::string(name), .value = "1" });
    }
  }

  struct MeshDepthShaders {
    std::string_view source_path;
    std::string_view vertex_entry;
    std::string_view pixel_entry;
  };

  auto BuildDepthPipeline(const MeshDepthShaders& shaders,
    const graphics::TextureDesc& depth, const Format velocity_format,
    const MeshRasterState raster_state, const bool reverse_z,
    std::string_view debug_name) -> graphics::GraphicsPipelineDesc
  {
    const auto writes_velocity = velocity_format != Format::kUnknown;
    auto root_bindings = BuildVortexRootBindings();

    auto defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(writes_velocity, "HAS_VELOCITY", defines);
    AddBooleanDefine(raster_state.alpha_test, "ALPHA_TEST", defines);

    auto blend_targets = std::vector<graphics::BlendTargetDesc> {};
    auto color_formats = std::vector<Format> {};
    if (writes_velocity) {
      blend_targets.push_back(graphics::BlendTargetDesc {
        .blend_enable = false,
        .write_mask = graphics::ColorWriteMask::kAll,
      });
      color_formats.push_back(velocity_format);
    }

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = std::string(shaders.source_path),
        .entry_point = std::string(shaders.vertex_entry),
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = std::string(shaders.source_path),
        .entry_point = std::string(shaders.pixel_entry),
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(raster_state.Rasterizer())
      .SetDepthStencilState(graphics::DepthStencilStateDesc {
        .depth_test_enable = true,
        .depth_write_enable = true,
        .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                                : graphics::CompareOp::kLessOrEqual,
        .stencil_enable = false,
      })
      .SetBlendState(std::move(blend_targets))
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = std::move(color_formats),
        .depth_stencil_format = depth.format,
        .sample_count = depth.sample_count,
        .sample_quality = depth.sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(std::string(debug_name))
      .Build();
  }

} // namespace

auto BuildMeshDepthPipeline(const graphics::TextureDesc& depth,
  const Format velocity_format, const MeshRasterState raster_state,
  const bool reverse_z) -> graphics::GraphicsPipelineDesc
{
  const auto writes_velocity = velocity_format != Format::kUnknown;

  const auto* const masked_debug_name = writes_velocity
    ? "Vortex.DepthPrepass.MaskedVelocity"
    : "Vortex.DepthPrepass.Masked";
  const auto* const opaque_debug_name = writes_velocity
    ? "Vortex.DepthPrepass.OpaqueVelocity"
    : "Vortex.DepthPrepass.Opaque";
  const auto* const debug_name
    = raster_state.alpha_test ? masked_debug_name : opaque_debug_name;
  return BuildDepthPipeline(
    MeshDepthShaders {
      .source_path = "Vortex/Stages/DepthPrepass/DepthPrepass.hlsl",
      .vertex_entry = "DepthPrepassVS",
      .pixel_entry = "DepthPrepassPS",
    },
    depth, velocity_format, raster_state, reverse_z, debug_name);
}

auto BuildContactShadowCasterDepthPipeline(const graphics::TextureDesc& depth,
  const MeshRasterState raster_state, const bool reverse_z)
  -> graphics::GraphicsPipelineDesc
{
  return BuildDepthPipeline(
    MeshDepthShaders {
      .source_path = "Vortex/Services/Shadows/ContactShadowCasterDepth.hlsl",
      .vertex_entry = "VortexContactShadowCasterDepthVS",
      .pixel_entry = "VortexContactShadowCasterDepthPS",
    },
    depth, Format::kUnknown, raster_state, reverse_z,
    raster_state.alpha_test ? "Vortex.ContactShadowCasterDepth.Masked"
                            : "Vortex.ContactShadowCasterDepth.Opaque");
}

} // namespace oxygen::vortex::internal
