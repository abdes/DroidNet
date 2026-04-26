//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/Internal/ViewportClamp.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/BasePass/BasePassModule.h>
#include <Oxygen/Vortex/Types/VelocityPublications.h>

namespace oxygen::vortex {

namespace {
  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;
  constexpr std::uint32_t kVelocityMergeThreadGroupSize = 8U;
  constexpr std::uint32_t kWireframePassConstantsStride
    = packing::kConstantBufferAlignment;

  struct alignas(packing::kShaderDataFieldAlignment) WireframePassConstants {
    float wire_color[4] { 1.0F, 1.0F, 1.0F, 1.0F };
    float apply_exposure_compensation { 0.0F };
    float padding[3] { 0.0F, 0.0F, 0.0F };
  };
  static_assert(
    sizeof(WireframePassConstants) % packing::kShaderDataFieldAlignment == 0U);

  auto RangeTypeToViewType(const bindless_d3d12::RangeType type)
    -> graphics::ResourceViewType
  {
    using graphics::ResourceViewType;

    switch (type) {
    case bindless_d3d12::RangeType::SRV:
      return ResourceViewType::kRawBuffer_SRV;
    case bindless_d3d12::RangeType::Sampler:
      return ResourceViewType::kSampler;
    case bindless_d3d12::RangeType::UAV:
      return ResourceViewType::kRawBuffer_UAV;
    default:
      return ResourceViewType::kNone;
    }
  }

  auto AddBooleanDefine(const bool enabled, std::string_view name,
    std::vector<graphics::ShaderDefine>& defines) -> void
  {
    if (enabled) {
      defines.push_back(graphics::ShaderDefine {
        .name = std::string(name),
        .value = std::string("1"),
      });
    }
  }

  auto MakeTextureSrvDesc(const graphics::Texture& texture)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_SRV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = texture.GetDescriptor().format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto MakeTextureUavDesc(const graphics::Texture& texture)
    -> graphics::TextureViewDescription
  {
    return {
      .view_type = graphics::ResourceViewType::kTexture_UAV,
      .visibility = graphics::DescriptorVisibility::kShaderVisible,
      .format = texture.GetDescriptor().format,
      .dimension = texture.GetDescriptor().texture_type,
      .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    };
  }

  auto RegisterTextureView(Graphics& gfx, graphics::Texture& texture,
    const graphics::TextureViewDescription& desc) -> std::uint32_t
  {
    auto& registry = gfx.GetResourceRegistry();
    if (const auto existing = registry.FindShaderVisibleIndex(texture, desc);
      existing.has_value()) {
      return existing->get();
    }

    auto& allocator = gfx.GetDescriptorAllocator();
    auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
    CHECK_F(handle.IsValid(),
      "BasePassModule: failed to allocate a {} descriptor for '{}'",
      graphics::to_string(desc.view_type), texture.GetName());

    const auto view = registry.RegisterView(texture, std::move(handle), desc);
    CHECK_F(view->IsValid(),
      "BasePassModule: failed to register a {} descriptor for '{}'",
      graphics::to_string(desc.view_type), texture.GetName());

    const auto index = registry.FindShaderVisibleIndex(texture, desc);
    CHECK_F(index.has_value(),
      "BasePassModule: {} descriptor registration for '{}' did not yield a "
      "shader-visible index",
      graphics::to_string(desc.view_type), texture.GetName());
    return index->get();
  }

  auto ResetStageTexture(
    Graphics& gfx, std::shared_ptr<graphics::Texture>& texture) -> void
  {
    if (!texture) {
      return;
    }

    auto& registry = gfx.GetResourceRegistry();
    if (registry.Contains(*texture)) {
      gfx.ForgetKnownResourceState(*texture);
      registry.UnRegisterResource(*texture);
    }
    gfx.RegisterDeferredRelease(std::move(texture));
  }

  auto NeedsStageTextureRebuild(
    const std::shared_ptr<graphics::Texture>& texture,
    const graphics::Texture& prototype, const bool render_target,
    const bool shader_resource, const bool uav) -> bool
  {
    if (!texture) {
      return true;
    }

    const auto& current_desc = texture->GetDescriptor();
    const auto& prototype_desc = prototype.GetDescriptor();
    return current_desc.width != prototype_desc.width
      || current_desc.height != prototype_desc.height
      || current_desc.format != prototype_desc.format
      || current_desc.sample_count != prototype_desc.sample_count
      || current_desc.is_render_target != render_target
      || current_desc.is_shader_resource != shader_resource
      || current_desc.is_uav != uav;
  }

  auto EnsureStageTexture(Graphics& gfx,
    std::shared_ptr<graphics::Texture>& texture, std::string_view debug_name,
    const graphics::Texture& prototype, const bool render_target,
    const bool shader_resource, const bool uav) -> graphics::Texture&
  {
    if (NeedsStageTextureRebuild(
          texture, prototype, render_target, shader_resource, uav)) {
      ResetStageTexture(gfx, texture);

      auto desc = prototype.GetDescriptor();
      desc.debug_name = std::string(debug_name);
      desc.is_render_target = render_target;
      desc.is_shader_resource = shader_resource;
      desc.is_uav = uav;
      desc.use_clear_value = render_target;
      desc.clear_value = graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F };
      desc.initial_state = render_target
        ? graphics::ResourceStates::kRenderTarget
        : (uav ? graphics::ResourceStates::kUnorderedAccess
               : (shader_resource ? graphics::ResourceStates::kShaderResource
                                  : graphics::ResourceStates::kCommon));
      texture = gfx.CreateTexture(desc);
    }

    CHECK_F(texture != nullptr,
      "BasePassModule failed to create stage texture '{}'", debug_name);
    auto& registry = gfx.GetResourceRegistry();
    static_cast<void>(registry.AcquireRegistration(texture));
    return *texture;
  }

  auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
  {
    std::vector<graphics::RootBindingItem> bindings;
    bindings.reserve(bindless_d3d12::kRootParamTableCount);

    for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
      ++index) {
      const auto& desc = bindless_d3d12::kRootParamTable.at(index);
      graphics::RootBindingDesc binding {};
      binding.binding_slot_desc.register_index = desc.shader_register;
      binding.binding_slot_desc.register_space = desc.register_space;
      binding.visibility = graphics::ShaderStageFlags::kAll;

      switch (desc.kind) {
      case bindless_d3d12::RootParamKind::DescriptorTable: {
        graphics::DescriptorTableBinding table {};
        if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
          const auto& range = desc.ranges.front();
          table.view_type = RangeTypeToViewType(
            static_cast<bindless_d3d12::RangeType>(range.range_type));
          table.base_index = range.base_register;
          table.count = range.num_descriptors
              == (std::numeric_limits<std::uint32_t>::max)()
            ? (std::numeric_limits<std::uint32_t>::max)()
            : range.num_descriptors;
        }
        binding.data = table;
        break;
      }
      case bindless_d3d12::RootParamKind::CBV:
        binding.data = graphics::DirectBufferBinding {};
        break;
      case bindless_d3d12::RootParamKind::RootConstants:
        binding.data
          = graphics::PushConstantsBinding { .size = desc.constants_count };
        break;
      }

      bindings.emplace_back(binding);
    }

    return bindings;
  }

  auto BuildBasePassFramebuffer(SceneTextures& scene_textures,
    const bool depth_read_only, const bool writes_velocity)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kNormal),
      .format = scene_textures.GetGBufferNormal().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kMaterial),
      .format = scene_textures.GetGBufferMaterial().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kBaseColor),
      .format = scene_textures.GetGBufferBaseColor().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kCustomData),
      .format = scene_textures.GetGBufferCustomData().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      desc.AddColorAttachment({
        .texture = scene_textures.GetVelocityResource(),
        .format = scene_textures.GetVelocity()->GetDescriptor().format,
      });
    }
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = depth_read_only,
    });
    return desc;
  }

  auto BuildBasePassColorClearFramebuffer(SceneTextures& scene_textures,
    const bool writes_velocity) -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kNormal),
      .format = scene_textures.GetGBufferNormal().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kMaterial),
      .format = scene_textures.GetGBufferMaterial().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kBaseColor),
      .format = scene_textures.GetGBufferBaseColor().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetGBufferResource(GBufferIndex::kCustomData),
      .format = scene_textures.GetGBufferCustomData().GetDescriptor().format,
    });
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      desc.AddColorAttachment({
        .texture = scene_textures.GetVelocityResource(),
        .format = scene_textures.GetVelocity()->GetDescriptor().format,
      });
    }
    return desc;
  }

  auto BuildWireframeFramebuffer(SceneTextures& scene_textures,
    const bool depth_read_only) -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = scene_textures.GetSceneColorResource(),
      .format = scene_textures.GetSceneColor().GetDescriptor().format,
    });
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = depth_read_only,
    });
    return desc;
  }

  auto BuildVelocityAuxFramebuffer(SceneTextures& scene_textures,
    const std::shared_ptr<graphics::Texture>& aux_texture)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = aux_texture,
      .format = aux_texture->GetDescriptor().format,
    });
    desc.SetDepthAttachment({
      .texture = scene_textures.GetSceneDepthResource(),
      .format = scene_textures.GetSceneDepth().GetDescriptor().format,
      .is_read_only = true,
    });
    return desc;
  }

  auto BuildVelocityAuxColorClearFramebuffer(
    const std::shared_ptr<graphics::Texture>& aux_texture)
    -> graphics::FramebufferDesc
  {
    auto desc = graphics::FramebufferDesc {};
    desc.AddColorAttachment({
      .texture = aux_texture,
      .format = aux_texture->GetDescriptor().format,
    });
    return desc;
  }

  auto NeedsVelocityAuxFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures,
    const std::shared_ptr<graphics::Texture>& aux_texture) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    return desc.color_attachments.size() != 1U
      || desc.color_attachments[0].texture.get() != aux_texture.get()
      || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get();
  }

  auto NeedsVelocityAuxColorClearFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const std::shared_ptr<graphics::Texture>& aux_texture) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    return desc.color_attachments.size() != 1U
      || desc.color_attachments[0].texture.get() != aux_texture.get()
      || desc.depth_attachment.texture != nullptr;
  }

  auto NeedsFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures, const bool writes_velocity) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    const auto expected_color_attachments
      = writes_velocity && scene_textures.GetVelocity() != nullptr ? 6U : 5U;
    if (desc.color_attachments.size() != expected_color_attachments
      || desc.depth_attachment.texture.get()
        != scene_textures.GetSceneDepthResource().get()) {
      return true;
    }

    const auto basic_mismatch = desc.color_attachments[0].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kNormal).get()
      || desc.color_attachments[1].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kMaterial).get()
      || desc.color_attachments[2].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kBaseColor).get()
      || desc.color_attachments[3].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kCustomData).get()
      || desc.color_attachments[4].texture.get()
        != scene_textures.GetSceneColorResource().get();
    if (basic_mismatch) {
      return true;
    }
    if (!writes_velocity || scene_textures.GetVelocity() == nullptr) {
      return false;
    }
    return desc.color_attachments[5].texture.get()
      != scene_textures.GetVelocityResource().get();
  }

  auto NeedsColorClearFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures, const bool writes_velocity) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    const auto expected_color_attachments
      = writes_velocity && scene_textures.GetVelocity() != nullptr ? 6U : 5U;
    if (desc.color_attachments.size() != expected_color_attachments
      || desc.depth_attachment.texture) {
      return true;
    }

    const auto basic_mismatch = desc.color_attachments[0].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kNormal).get()
      || desc.color_attachments[1].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kMaterial).get()
      || desc.color_attachments[2].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kBaseColor).get()
      || desc.color_attachments[3].texture.get()
        != scene_textures.GetGBufferResource(GBufferIndex::kCustomData).get()
      || desc.color_attachments[4].texture.get()
        != scene_textures.GetSceneColorResource().get();
    if (basic_mismatch) {
      return true;
    }
    if (!writes_velocity || scene_textures.GetVelocity() == nullptr) {
      return false;
    }
    return desc.color_attachments[5].texture.get()
      != scene_textures.GetVelocityResource().get();
  }

  auto NeedsWireframeFramebufferRebuild(
    const std::shared_ptr<graphics::Framebuffer>& framebuffer,
    const SceneTextures& scene_textures, const bool depth_read_only) -> bool
  {
    if (!framebuffer) {
      return true;
    }

    const auto& desc = framebuffer->GetDescriptor();
    return desc.color_attachments.size() != 1U
      || desc.color_attachments[0].texture.get()
      != scene_textures.GetSceneColorResource().get()
      || desc.depth_attachment.texture.get()
      != scene_textures.GetSceneDepthResource().get()
      || desc.depth_attachment.is_read_only != depth_read_only;
  }

  auto MakeWireframeBlendTarget(const bool overlay) -> graphics::BlendTargetDesc
  {
    if (!overlay) {
      return {
        .blend_enable = false,
        .write_mask = graphics::ColorWriteMask::kAll,
      };
    }
    return {
      .blend_enable = true,
      .src_blend = graphics::BlendFactor::kSrcAlpha,
      .dest_blend = graphics::BlendFactor::kInvSrcAlpha,
      .blend_op = graphics::BlendOp::kAdd,
      .src_blend_alpha = graphics::BlendFactor::kZero,
      .dest_blend_alpha = graphics::BlendFactor::kOne,
      .blend_op_alpha = graphics::BlendOp::kAdd,
      .write_mask = graphics::ColorWriteMask::kAll,
    };
  }

  auto BuildWireframePipelineDesc(const SceneTextures& scene_textures,
    const bool alpha_test, const bool reverse_z, const bool depth_write,
    const bool overlay) -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(alpha_test, "ALPHA_TEST", defines);

    const auto depth_state = graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = depth_write,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    };

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/BasePass/BasePassWireframe.hlsl",
        .entry_point = "BasePassGBufferVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Stages/BasePass/BasePassWireframe.hlsl",
        .entry_point = "BasePassWireframePS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::WireframeNoCulling())
      .SetDepthStencilState(depth_state)
      .SetBlendState({ MakeWireframeBlendTarget(overlay) })
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = std::vector<
          Format> { scene_textures.GetSceneColor().GetDescriptor().format },
        .depth_stencil_format
        = scene_textures.GetSceneDepth().GetDescriptor().format,
        .sample_count
        = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(overlay ? "Vortex.BasePass.WireframeOverlay"
                            : "Vortex.BasePass.Wireframe")
      .Build();
  }

  auto BuildBasePassPipelineDesc(const SceneTextures& scene_textures,
    const BasePassConfig& config, const bool alpha_test, const bool reverse_z,
    const bool writes_velocity) -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(alpha_test, "ALPHA_TEST", defines);
    AddBooleanDefine(writes_velocity, "HAS_VELOCITY", defines);

    auto blend_targets = std::vector<graphics::BlendTargetDesc>(
      writes_velocity && scene_textures.GetVelocity() != nullptr ? 6U : 5U);
    for (auto& blend_target : blend_targets) {
      blend_target.blend_enable = false;
      blend_target.write_mask = graphics::ColorWriteMask::kAll;
    }

    const auto depth_state = graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = !config.early_z_pass_done,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    };

    auto color_formats = std::vector<Format> {
      scene_textures.GetGBufferNormal().GetDescriptor().format,
      scene_textures.GetGBufferMaterial().GetDescriptor().format,
      scene_textures.GetGBufferBaseColor().GetDescriptor().format,
      scene_textures.GetGBufferCustomData().GetDescriptor().format,
      scene_textures.GetSceneColor().GetDescriptor().format,
    };
    if (writes_velocity && scene_textures.GetVelocity() != nullptr) {
      color_formats.push_back(
        scene_textures.GetVelocity()->GetDescriptor().format);
    }

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
        .entry_point = "BasePassGBufferVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Stages/BasePass/BasePassGBuffer.hlsl",
        .entry_point = "BasePassGBufferPS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(depth_state)
      .SetBlendState(std::move(blend_targets))
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = std::move(color_formats),
        .depth_stencil_format
        = scene_textures.GetSceneDepth().GetDescriptor().format,
        .sample_count
        = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(alpha_test ? "Vortex.BasePass.GBuffer.Masked"
                               : "Vortex.BasePass.GBuffer.Opaque")
      .Build();
  }

  auto BuildVelocityAuxPipelineDesc(const SceneTextures& scene_textures,
    const bool alpha_test, const bool reverse_z)
    -> graphics::GraphicsPipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    auto defines = std::vector<graphics::ShaderDefine> {};
    AddBooleanDefine(alpha_test, "ALPHA_TEST", defines);
    AddBooleanDefine(true, "USES_MOTION_VECTOR_WORLD_OFFSET", defines);

    auto blend_targets = std::vector<graphics::BlendTargetDesc>(1U);
    blend_targets.front().blend_enable = false;
    blend_targets.front().write_mask = graphics::ColorWriteMask::kAll;

    const auto depth_state = graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = false,
      .depth_func = reverse_z ? graphics::CompareOp::kGreaterOrEqual
                              : graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
    };

    return graphics::GraphicsPipelineDesc::Builder {}
      .SetVertexShader(graphics::ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Vortex/Stages/BasePass/BasePassVelocityAux.hlsl",
        .entry_point = "BasePassVelocityAuxVS",
        .defines = defines,
      })
      .SetPixelShader(graphics::ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Vortex/Stages/BasePass/BasePassVelocityAux.hlsl",
        .entry_point = "BasePassVelocityAuxPS",
        .defines = defines,
      })
      .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
      .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
      .SetDepthStencilState(depth_state)
      .SetBlendState(std::move(blend_targets))
      .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
        .color_target_formats = std::vector<
          Format> { scene_textures.GetVelocity()->GetDescriptor().format },
        .depth_stencil_format
        = scene_textures.GetSceneDepth().GetDescriptor().format,
        .sample_count
        = scene_textures.GetSceneColor().GetDescriptor().sample_count,
        .sample_quality
        = scene_textures.GetSceneColor().GetDescriptor().sample_quality,
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName(alpha_test ? "Vortex.BasePass.VelocityAux.Masked"
                               : "Vortex.BasePass.VelocityAux.Opaque")
      .Build();
  }

  auto BuildVelocityMergePipelineDesc() -> graphics::ComputePipelineDesc
  {
    auto root_bindings = BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder {}
      .SetComputeShader(graphics::ShaderRequest {
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/BasePass/BasePassVelocityMerge.hlsl",
        .entry_point = "BasePassVelocityMergeCS",
      })
      .SetRootBindings(std::span<const graphics::RootBindingItem>(
        root_bindings.data(), root_bindings.size()))
      .SetDebugName("Vortex.BasePass.VelocityMerge")
      .Build();
  }

  auto TryGetVelocityDrawMetadata(const PreparedSceneFrame& prepared_frame,
    const std::uint32_t draw_index) -> const VelocityDrawMetadata*
  {
    if (draw_index >= prepared_frame.velocity_draw_metadata.size()) {
      return nullptr;
    }
    return &prepared_frame.velocity_draw_metadata[draw_index];
  }

  auto TryGetCurrentMotionVectorStatusPublication(
    const PreparedSceneFrame& prepared_frame,
    const VelocityDrawMetadata& velocity_metadata)
    -> const MotionVectorStatusPublication*
  {
    if (!HasAnyVelocityDrawPublicationFlag(velocity_metadata.publication_flags,
          VelocityDrawPublicationFlagBits::kCurrentMotionVectorStatusValid)
      || velocity_metadata.current_motion_vector_status_index
        == kInvalidVelocityPublicationIndex
      || velocity_metadata.current_motion_vector_status_index
        >= prepared_frame.current_motion_vector_status_publications.size()) {
      return nullptr;
    }
    return &prepared_frame.current_motion_vector_status_publications
              [velocity_metadata.current_motion_vector_status_index];
  }

  auto TryGetPreviousMotionVectorStatusPublication(
    const PreparedSceneFrame& prepared_frame,
    const VelocityDrawMetadata& velocity_metadata)
    -> const MotionVectorStatusPublication*
  {
    if (!HasAnyVelocityDrawPublicationFlag(velocity_metadata.publication_flags,
          VelocityDrawPublicationFlagBits::kPreviousMotionVectorStatusValid)
      || velocity_metadata.previous_motion_vector_status_index
        == kInvalidVelocityPublicationIndex
      || velocity_metadata.previous_motion_vector_status_index
        >= prepared_frame.previous_motion_vector_status_publications.size()) {
      return nullptr;
    }
    return &prepared_frame.previous_motion_vector_status_publications
              [velocity_metadata.previous_motion_vector_status_index];
  }

  auto MotionVectorStatusUsesAuxiliaryPath(
    const MotionVectorStatusPublication* const publication) -> bool
  {
    return publication != nullptr
      && HasAnyMotionPublicationCapability(publication->capability_flags,
        MotionPublicationCapabilityBits::kUsesMotionVectorWorldOffset);
  }

  auto DrawRequiresMotionVectorWorldOffset(
    const PreparedSceneFrame& prepared_frame,
    const BasePassDrawCommand& draw_command) -> bool
  {
    const auto* velocity_metadata
      = TryGetVelocityDrawMetadata(prepared_frame, draw_command.draw_index);
    if (velocity_metadata == nullptr) {
      return false;
    }

    return MotionVectorStatusUsesAuxiliaryPath(
             TryGetCurrentMotionVectorStatusPublication(
               prepared_frame, *velocity_metadata))
      || MotionVectorStatusUsesAuxiliaryPath(
        TryGetPreviousMotionVectorStatusPublication(
          prepared_frame, *velocity_metadata));
  }

  auto IsMaskedDraw(const PreparedSceneFrame& prepared_frame,
    const BasePassDrawCommand& draw_command) -> bool
  {
    const auto metadata = prepared_frame.GetDrawMetadata();
    return draw_command.draw_index < metadata.size()
      && metadata[draw_command.draw_index].flags.IsSet(PassMaskBit::kMasked);
  }

  auto BeginPersistentWriteTarget(
    graphics::CommandRecorder& recorder, graphics::Texture& texture) -> void
  {
    if (!recorder.AdoptKnownResourceState(texture)) {
      auto initial = texture.GetDescriptor().initial_state;
      if (initial == graphics::ResourceStates::kUnknown
        || initial == graphics::ResourceStates::kUndefined) {
        initial = graphics::ResourceStates::kCommon;
      }
      recorder.BeginTrackingResourceState(texture, initial);
    }
  }

  auto BeginBasePassResourceTracking(graphics::CommandRecorder& recorder,
    SceneTextures& scene_textures, const BasePassConfig& config) -> void
  {
    BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferNormal());
    BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferMaterial());
    BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferBaseColor());
    BeginPersistentWriteTarget(recorder, scene_textures.GetGBufferCustomData());
    BeginPersistentWriteTarget(recorder, scene_textures.GetSceneColor());
    BeginPersistentWriteTarget(recorder, scene_textures.GetSceneDepth());
    if (scene_textures.GetVelocity() != nullptr) {
      BeginPersistentWriteTarget(recorder, *scene_textures.GetVelocity());
    }

    recorder.RequireResourceState(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceState(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceState(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceState(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceState(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceState(scene_textures.GetSceneDepth(),
      config.early_z_pass_done ? graphics::ResourceStates::kDepthRead
                               : graphics::ResourceStates::kDepthWrite);
    if (config.write_velocity && scene_textures.GetVelocity() != nullptr) {
      recorder.RequireResourceState(
        *scene_textures.GetVelocity(), graphics::ResourceStates::kRenderTarget);
    } else if (scene_textures.GetVelocity() != nullptr) {
      recorder.RequireResourceState(*scene_textures.GetVelocity(),
        graphics::ResourceStates::kShaderResource);
    }
  }

  auto TransitionBasePassFinalStates(
    graphics::CommandRecorder& recorder, SceneTextures& scene_textures) -> void
  {
    recorder.RequireResourceStateFinal(scene_textures.GetGBufferNormal(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceStateFinal(scene_textures.GetGBufferMaterial(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceStateFinal(scene_textures.GetGBufferBaseColor(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceStateFinal(scene_textures.GetGBufferCustomData(),
      graphics::ResourceStates::kShaderResource);
    recorder.RequireResourceStateFinal(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder.RequireResourceStateFinal(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    if (scene_textures.GetVelocity() != nullptr) {
      recorder.RequireResourceStateFinal(*scene_textures.GetVelocity(),
        graphics::ResourceStates::kShaderResource);
    }
  }

  auto SetViewportAndScissor(graphics::CommandRecorder& recorder,
    const RenderContext& ctx, const SceneTextures& scene_textures) -> void
  {
    const auto extent = scene_textures.GetExtent();
    if (ctx.current_view.resolved_view != nullptr) {
      const auto clamped
        = oxygen::vortex::internal::ResolveClampedViewportState(
          ctx.current_view.resolved_view->Viewport(),
          ctx.current_view.resolved_view->Scissor(), extent.x, extent.y);
      recorder.SetViewport(clamped.viewport);
      recorder.SetScissors(clamped.scissors);
      return;
    }

    recorder.SetViewport({
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(extent.x),
      .height = static_cast<float>(extent.y),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    recorder.SetScissors({
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(extent.x),
      .bottom = static_cast<std::int32_t>(extent.y),
    });
  }

} // namespace

BasePassModule::BasePassModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : renderer_(renderer)
  , mesh_processor_(std::make_unique<BasePassMeshProcessor>(renderer))
{
  static_cast<void>(scene_textures_config);
  wireframe_constants_indices_.fill(kInvalidShaderVisibleIndex);
}

BasePassModule::~BasePassModule()
{
  ReleaseWireframeConstantsBuffer();
  if (auto* gfx = renderer_.GetGraphics().get(); gfx != nullptr) {
    ResetStageTexture(*gfx, velocity_base_copy_);
    ResetStageTexture(*gfx, velocity_motion_vector_world_offset_);
  }
}

auto BasePassModule::EnsureWireframeConstantsBuffer(Graphics& gfx) -> void
{
  if (wireframe_constants_buffer_ == nullptr
    || wireframe_constants_mapped_ptr_ == nullptr
    || !wireframe_constants_indices_[0].IsValid()) {
    ReleaseWireframeConstantsBuffer();

    auto& registry = gfx.GetResourceRegistry();
    auto& allocator = gfx.GetDescriptorAllocator();
    const auto desc = graphics::BufferDesc {
      .size_bytes
      = kWireframePassConstantsStride * kWireframePassConstantsSlots,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = "Vortex.Stage20.Wireframe.PassConstants",
    };

    wireframe_constants_buffer_ = gfx.CreateBuffer(desc);
    CHECK_NOTNULL_F(wireframe_constants_buffer_.get(),
      "BasePassModule: failed to create wireframe constants buffer");
    wireframe_constants_buffer_->SetName(desc.debug_name);
    registry.Register(wireframe_constants_buffer_);

    wireframe_constants_mapped_ptr_ = static_cast<std::byte*>(
      wireframe_constants_buffer_->Map(0U, desc.size_bytes));
    CHECK_NOTNULL_F(wireframe_constants_mapped_ptr_,
      "BasePassModule: failed to map wireframe constants buffer");

    wireframe_constants_indices_.fill(kInvalidShaderVisibleIndex);
    for (std::size_t slot = 0U; slot < kWireframePassConstantsSlots; ++slot) {
      auto handle
        = allocator.AllocateRaw(graphics::ResourceViewType::kConstantBuffer,
          graphics::DescriptorVisibility::kShaderVisible);
      CHECK_F(handle.IsValid(),
        "BasePassModule: failed to allocate wireframe constants descriptor");
      wireframe_constants_indices_[slot]
        = allocator.GetShaderVisibleIndex(handle);

      const auto offset
        = static_cast<std::uint64_t>(slot * kWireframePassConstantsStride);
      const auto view_desc = graphics::BufferViewDescription {
        .view_type = graphics::ResourceViewType::kConstantBuffer,
        .visibility = graphics::DescriptorVisibility::kShaderVisible,
        .range = { offset, kWireframePassConstantsStride },
      };
      const auto view = registry.RegisterView(
        *wireframe_constants_buffer_, std::move(handle), view_desc);
      CHECK_F(view->IsValid(),
        "BasePassModule: failed to register wireframe constants descriptor");
    }
  }
}

auto BasePassModule::WriteWireframeConstants(
  Graphics& gfx, const RenderContext& ctx, const bool compensate_exposure)
  -> ShaderVisibleIndex
{
  EnsureWireframeConstantsBuffer(gfx);
  CHECK_NOTNULL_F(wireframe_constants_mapped_ptr_);

  const auto constants = WireframePassConstants {
    .wire_color = { ctx.wireframe_color.r, ctx.wireframe_color.g,
      ctx.wireframe_color.b, ctx.wireframe_color.a },
    .apply_exposure_compensation = compensate_exposure ? 1.0F : 0.0F,
  };
  const auto slot = wireframe_constants_slot_ % kWireframePassConstantsSlots;
  ++wireframe_constants_slot_;
  std::memcpy(
    wireframe_constants_mapped_ptr_ + (slot * kWireframePassConstantsStride),
    &constants, sizeof(constants));
  return wireframe_constants_indices_[slot];
}

auto BasePassModule::ReleaseWireframeConstantsBuffer() -> void
{
  if (wireframe_constants_buffer_ == nullptr) {
    wireframe_constants_mapped_ptr_ = nullptr;
    wireframe_constants_indices_.fill(kInvalidShaderVisibleIndex);
    wireframe_constants_slot_ = 0U;
    return;
  }

  if (wireframe_constants_buffer_->IsMapped()) {
    wireframe_constants_buffer_->UnMap();
  }

  if (auto gfx = renderer_.GetGraphics(); gfx != nullptr) {
    auto& registry = gfx->GetResourceRegistry();
    if (registry.Contains(*wireframe_constants_buffer_)) {
      registry.UnRegisterResource(*wireframe_constants_buffer_);
    }
    gfx->RegisterDeferredRelease(std::move(wireframe_constants_buffer_));
  } else {
    wireframe_constants_buffer_.reset();
  }

  wireframe_constants_mapped_ptr_ = nullptr;
  wireframe_constants_indices_.fill(kInvalidShaderVisibleIndex);
  wireframe_constants_slot_ = 0U;
}

auto BasePassModule::Execute(RenderContext& ctx, SceneTextures& scene_textures)
  -> BasePassExecutionResult
{
  last_execution_result_ = {};
  if (config_.shading_mode != ShadingMode::kDeferred) {
    return last_execution_result_;
  }

  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  if (!has_current_view_payload) {
    return last_execution_result_;
  }

  const auto writes_velocity
    = config_.write_velocity && scene_textures.GetVelocity() != nullptr;
  mesh_processor_->BuildDrawCommands(*ctx.current_view.prepared_frame,
    config_.shading_mode, writes_velocity,
    ctx.current_view.occlusion_results.get());
  last_execution_result_.draw_count
    = static_cast<std::uint32_t>(mesh_processor_->GetDrawCommands().size());
  last_execution_result_.occlusion_culled_draw_count
    = mesh_processor_->GetOcclusionCulledDrawCount();

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr || ctx.view_constants == nullptr) {
    return last_execution_result_;
  }
  const auto& prepared_frame = *ctx.current_view.prepared_frame;
  const auto requires_velocity_aux = writes_velocity
    && std::ranges::any_of(mesh_processor_->GetDrawCommands(),
      [&prepared_frame](const BasePassDrawCommand& draw_command) -> bool {
        return DrawRequiresMotionVectorWorldOffset(
          prepared_frame, draw_command);
      });

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "Vortex BasePass");
  if (!recorder) {
    return last_execution_result_;
  }
  graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage9.BasePass",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  last_execution_result_.completed_velocity_for_dynamic_geometry
    = !config_.write_velocity;
  last_execution_result_.wrote_velocity_target = writes_velocity;

  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  if (config_.render_mode == RenderMode::kWireframe) {
    const auto depth_read_only = false;
    if (NeedsWireframeFramebufferRebuild(
          wireframe_framebuffer_, scene_textures, depth_read_only)) {
      wireframe_framebuffer_ = gfx->CreateFramebuffer(
        BuildWireframeFramebuffer(scene_textures, depth_read_only));
    }

    BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneColor());
    BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneDepth());
    recorder->RequireResourceState(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder->RequireResourceState(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthWrite);
    recorder->FlushBarriers();

    graphics::GpuEventScope wire_scope(*recorder,
      "Vortex.Stage9.BasePass.Wireframe",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    recorder->ClearFramebuffer(
      *wireframe_framebuffer_, std::nullopt, reverse_z ? 0.0F : 1.0F, 0U);
    recorder->BindFrameBuffer(*wireframe_framebuffer_);
    SetViewportAndScissor(*recorder, ctx, scene_textures);
    const auto wireframe_constants_index
      = WriteWireframeConstants(*gfx, ctx, false);
    auto current_alpha_test = std::optional<bool> {};
    for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
      const auto alpha_test = IsMaskedDraw(prepared_frame, draw_command);
      if (!current_alpha_test.has_value()
        || current_alpha_test.value() != alpha_test) {
        recorder->SetPipelineState(BuildWireframePipelineDesc(
          scene_textures, alpha_test, reverse_z, true, false));
        recorder->SetGraphicsRootConstantBufferView(
          view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
        current_alpha_test = alpha_test;
      }

      recorder->SetGraphicsRoot32BitConstant(
        root_constants_param, draw_command.draw_index, 0U);
      recorder->SetGraphicsRoot32BitConstant(
        root_constants_param, wireframe_constants_index.get(), 1U);
      recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                             : draw_command.vertex_count,
        draw_command.instance_count, 0U, draw_command.start_instance);
    }

    recorder->RequireResourceStateFinal(
      scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
    recorder->RequireResourceStateFinal(
      scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
    last_execution_result_.completed_velocity_for_dynamic_geometry = true;
    last_execution_result_.wrote_velocity_target = false;
    return last_execution_result_;
  }

  last_execution_result_.published_base_pass_products = true;
  BeginBasePassResourceTracking(*recorder, scene_textures, config_);

  if (NeedsFramebufferRebuild(framebuffer_, scene_textures, writes_velocity)) {
    framebuffer_ = gfx->CreateFramebuffer(BuildBasePassFramebuffer(
      scene_textures, config_.early_z_pass_done, writes_velocity));
  }
  if (config_.early_z_pass_done
    && NeedsColorClearFramebufferRebuild(
      color_clear_framebuffer_, scene_textures, writes_velocity)) {
    color_clear_framebuffer_ = gfx->CreateFramebuffer(
      BuildBasePassColorClearFramebuffer(scene_textures, writes_velocity));
  }

  recorder->FlushBarriers();

  {
    graphics::GpuEventScope main_pass_scope(*recorder,
      "Vortex.Stage9.BasePass.MainPass",
      profiling::ProfileGranularity::kTelemetry,
      profiling::ProfileCategory::kPass);
    if (!config_.early_z_pass_done) {
      recorder->ClearFramebuffer(
        *framebuffer_, std::nullopt, reverse_z ? 0.0F : 1.0F, 0U);
    } else if (color_clear_framebuffer_) {
      recorder->ClearFramebuffer(*color_clear_framebuffer_);
    }
    recorder->BindFrameBuffer(*framebuffer_);
    SetViewportAndScissor(*recorder, ctx, scene_textures);
    auto current_alpha_test = std::optional<bool> {};
    for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
      const auto alpha_test = IsMaskedDraw(prepared_frame, draw_command);
      if (!current_alpha_test.has_value()
        || current_alpha_test.value() != alpha_test) {
        recorder->SetPipelineState(BuildBasePassPipelineDesc(
          scene_textures, config_, alpha_test, reverse_z, writes_velocity));
        recorder->SetGraphicsRootConstantBufferView(
          view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
        recorder->SetGraphicsRoot32BitConstant(
          root_constants_param, kInvalidShaderVisibleIndex.get(), 1U);
        current_alpha_test = alpha_test;
      }

      recorder->SetGraphicsRoot32BitConstant(
        root_constants_param, draw_command.draw_index, 0U);
      recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                             : draw_command.vertex_count,
        draw_command.instance_count, 0U, draw_command.start_instance);
    }
  }

  if (requires_velocity_aux) {
    auto& velocity_base_copy
      = EnsureStageTexture(*gfx, velocity_base_copy_, "VelocityBasePassCopy",
        *scene_textures.GetVelocity(), false, true, false);
    auto& velocity_aux = EnsureStageTexture(*gfx,
      velocity_motion_vector_world_offset_, "VelocityMotionVectorWorldOffset",
      *scene_textures.GetVelocity(), true, true, false);

    if (NeedsVelocityAuxFramebufferRebuild(velocity_aux_framebuffer_,
          scene_textures, velocity_motion_vector_world_offset_)) {
      velocity_aux_framebuffer_
        = gfx->CreateFramebuffer(BuildVelocityAuxFramebuffer(
          scene_textures, velocity_motion_vector_world_offset_));
    }
    if (NeedsVelocityAuxColorClearFramebufferRebuild(
          velocity_aux_color_clear_framebuffer_,
          velocity_motion_vector_world_offset_)) {
      velocity_aux_color_clear_framebuffer_
        = gfx->CreateFramebuffer(BuildVelocityAuxColorClearFramebuffer(
          velocity_motion_vector_world_offset_));
    }

    BeginPersistentWriteTarget(*recorder, velocity_base_copy);
    BeginPersistentWriteTarget(*recorder, velocity_aux);
    recorder->RequireResourceState(
      *scene_textures.GetVelocity(), graphics::ResourceStates::kCopySource);
    recorder->RequireResourceState(
      velocity_base_copy, graphics::ResourceStates::kCopyDest);
    recorder->RequireResourceState(
      velocity_aux, graphics::ResourceStates::kRenderTarget);
    recorder->FlushBarriers();
    recorder->CopyTexture(*scene_textures.GetVelocity(),
      graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture(), velocity_base_copy,
      graphics::TextureSlice {},
      graphics::TextureSubResourceSet::EntireTexture());

    {
      graphics::GpuEventScope velocity_aux_scope(*recorder,
        "Vortex.Stage9.BasePass.VelocityAux",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kPass);
      recorder->ClearFramebuffer(*velocity_aux_color_clear_framebuffer_,
        std::vector<std::optional<graphics::Color>> {
          graphics::Color { 0.0F, 0.0F, 0.0F, 0.0F },
        },
        std::nullopt, std::nullopt);
      recorder->BindFrameBuffer(*velocity_aux_framebuffer_);
      SetViewportAndScissor(*recorder, ctx, scene_textures);

      auto current_alpha_test = std::optional<bool> {};
      for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
        if (!DrawRequiresMotionVectorWorldOffset(
              prepared_frame, draw_command)) {
          continue;
        }

        const auto alpha_test = IsMaskedDraw(prepared_frame, draw_command);
        if (!current_alpha_test.has_value()
          || current_alpha_test.value() != alpha_test) {
          recorder->SetPipelineState(BuildVelocityAuxPipelineDesc(
            scene_textures, alpha_test, reverse_z));
          recorder->SetGraphicsRootConstantBufferView(
            view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
          recorder->SetGraphicsRoot32BitConstant(
            root_constants_param, kInvalidShaderVisibleIndex.get(), 1U);
          current_alpha_test = alpha_test;
        }

        recorder->SetGraphicsRoot32BitConstant(
          root_constants_param, draw_command.draw_index, 0U);
        recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                               : draw_command.vertex_count,
          draw_command.instance_count, 0U, draw_command.start_instance);
      }
    }

    const auto velocity_base_copy_srv = RegisterTextureView(
      *gfx, velocity_base_copy, MakeTextureSrvDesc(velocity_base_copy));
    const auto velocity_aux_srv = RegisterTextureView(
      *gfx, velocity_aux, MakeTextureSrvDesc(velocity_aux));

    recorder->RequireResourceState(
      velocity_base_copy, graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(
      velocity_aux, graphics::ResourceStates::kShaderResource);
    recorder->RequireResourceState(*scene_textures.GetVelocity(),
      graphics::ResourceStates::kUnorderedAccess);
    recorder->FlushBarriers();

    {
      graphics::GpuEventScope velocity_merge_scope(*recorder,
        "Vortex.Stage9.BasePass.VelocityMerge",
        profiling::ProfileGranularity::kTelemetry,
        profiling::ProfileCategory::kPass);
      recorder->SetPipelineState(BuildVelocityMergePipelineDesc());
      recorder->SetComputeRootConstantBufferView(
        view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
      recorder->SetComputeRoot32BitConstant(
        root_constants_param, velocity_base_copy_srv, 0U);
      recorder->SetComputeRoot32BitConstant(
        root_constants_param, velocity_aux_srv, 1U);
      const auto extent = scene_textures.GetExtent();
      recorder->Dispatch((extent.x + (kVelocityMergeThreadGroupSize - 1U))
          / kVelocityMergeThreadGroupSize,
        (extent.y + (kVelocityMergeThreadGroupSize - 1U))
          / kVelocityMergeThreadGroupSize,
        1U);
    }
  }

  last_execution_result_.completed_velocity_for_dynamic_geometry
    = !config_.write_velocity || writes_velocity;
  TransitionBasePassFinalStates(*recorder, scene_textures);

  last_execution_result_.published_base_pass_products = true;
  return last_execution_result_;
}

auto BasePassModule::ExecuteWireframeOverlay(
  RenderContext& ctx, SceneTextures& scene_textures) -> std::uint32_t
{
  if (config_.shading_mode != ShadingMode::kDeferred) {
    return 0U;
  }
  const auto has_current_view_payload = mesh_processor_ != nullptr
    && ctx.current_view.prepared_frame != nullptr
    && ctx.current_view.prepared_frame->IsValid();
  if (!has_current_view_payload) {
    return 0U;
  }

  auto* gfx = renderer_.GetGraphics().get();
  if (gfx == nullptr || ctx.view_constants == nullptr) {
    return 0U;
  }

  const auto& prepared_frame = *ctx.current_view.prepared_frame;
  mesh_processor_->BuildDrawCommands(prepared_frame, config_.shading_mode,
    false, ctx.current_view.occlusion_results.get());
  const auto draw_count
    = static_cast<std::uint32_t>(mesh_processor_->GetDrawCommands().size());
  if (draw_count == 0U) {
    return 0U;
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder
    = gfx->AcquireCommandRecorder(queue_key, "Vortex WireframeOverlay");
  if (!recorder) {
    return 0U;
  }

  graphics::GpuEventScope overlay_scope(*recorder,
    "Vortex.Stage20.WireframeOverlay",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);

  constexpr auto depth_read_only = true;
  if (NeedsWireframeFramebufferRebuild(
        wireframe_framebuffer_, scene_textures, depth_read_only)) {
    wireframe_framebuffer_ = gfx->CreateFramebuffer(
      BuildWireframeFramebuffer(scene_textures, depth_read_only));
  }

  BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneColor());
  BeginPersistentWriteTarget(*recorder, scene_textures.GetSceneDepth());
  recorder->RequireResourceState(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceState(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  recorder->FlushBarriers();
  recorder->BindFrameBuffer(*wireframe_framebuffer_);
  SetViewportAndScissor(*recorder, ctx, scene_textures);

  const auto reverse_z = ctx.current_view.resolved_view == nullptr
    || ctx.current_view.resolved_view->ReverseZ();
  const auto wireframe_constants_index
    = WriteWireframeConstants(*gfx, ctx, true);
  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);
  auto current_alpha_test = std::optional<bool> {};
  for (const auto& draw_command : mesh_processor_->GetDrawCommands()) {
    const auto alpha_test = IsMaskedDraw(prepared_frame, draw_command);
    if (!current_alpha_test.has_value()
      || current_alpha_test.value() != alpha_test) {
      recorder->SetPipelineState(BuildWireframePipelineDesc(
        scene_textures, alpha_test, reverse_z, false, true));
      recorder->SetGraphicsRootConstantBufferView(
        view_constants_param, ctx.view_constants->GetGPUVirtualAddress());
      current_alpha_test = alpha_test;
    }

    recorder->SetGraphicsRoot32BitConstant(
      root_constants_param, draw_command.draw_index, 0U);
    recorder->SetGraphicsRoot32BitConstant(
      root_constants_param, wireframe_constants_index.get(), 1U);
    recorder->Draw(draw_command.is_indexed ? draw_command.index_count
                                           : draw_command.vertex_count,
      draw_command.instance_count, 0U, draw_command.start_instance);
  }

  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneColor(), graphics::ResourceStates::kRenderTarget);
  recorder->RequireResourceStateFinal(
    scene_textures.GetSceneDepth(), graphics::ResourceStates::kDepthRead);
  return draw_count;
}

void BasePassModule::SetConfig(const BasePassConfig& config)
{
  config_ = config;
}

auto BasePassModule::HasPublishedBasePassProducts() const -> bool
{
  return last_execution_result_.published_base_pass_products;
}

auto BasePassModule::HasCompletedVelocityForDynamicGeometry() const -> bool
{
  return last_execution_result_.completed_velocity_for_dynamic_geometry;
}

auto BasePassModule::GetLastExecutionResult() const
  -> const BasePassExecutionResult&
{
  return last_execution_result_;
}

} // namespace oxygen::vortex
