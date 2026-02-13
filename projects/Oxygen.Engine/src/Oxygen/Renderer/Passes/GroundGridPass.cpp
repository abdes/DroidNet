//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <utility>

#include <glm/glm.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/Passes/GroundGridPass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

using oxygen::engine::GroundGridPass;
using oxygen::engine::GroundGridPassConfig;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::DescriptorVisibility;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceRegistry;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

namespace {
struct alignas(16) GroundGridPassConstants {
  glm::mat4 inv_view_proj { 1.0F };
  glm::vec4 grid_params0 { 0.0F, 1.0F, 10.0F, 0.02F };
  glm::vec4 grid_params1 { 0.04F, 0.06F, 0.0F, 0.0F };
  glm::vec4 grid_params2 { 1000.0F, 0.0F, 0.0F, 0.0F };
  glm::vec4 grid_params3 { 1.0F, 32.0F, 1e-4F, 0.0F };
  uint32_t depth_srv_index { oxygen::kInvalidShaderVisibleIndex.get() };
  uint32_t flags { 0U };
  glm::vec2 pad0 { 0.0F, 0.0F };
  glm::vec4 minor_color { 0.35F, 0.35F, 0.35F, 1.0F };
  glm::vec4 major_color { 0.55F, 0.55F, 0.55F, 1.0F };
  glm::vec4 axis_color_x { 0.90F, 0.20F, 0.20F, 1.0F };
  glm::vec4 axis_color_y { 0.20F, 0.90F, 0.20F, 1.0F };
  glm::vec4 origin_color { 1.0F, 1.0F, 1.0F, 1.0F };
};

constexpr uint32_t kGroundGridConstantsSize = 224U;
static_assert(sizeof(GroundGridPassConstants) == kGroundGridConstantsSize,
  "GroundGridPassConstants must be 224 bytes");

constexpr uint32_t kConstantsBufferMinSize = 256U;

auto PrepareRenderTargetView(Texture& color_texture, ResourceRegistry& registry,
  DescriptorAllocator& allocator) -> oxygen::graphics::NativeView
{
  using oxygen::TextureType;

  const auto& tex_desc = color_texture.GetDescriptor();
  oxygen::graphics::TextureViewDescription rtv_view_desc {
    .view_type = ResourceViewType::kTexture_RTV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = tex_desc.format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = tex_desc.mip_levels,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = false,
  };

  if (const auto rtv = registry.Find(color_texture, rtv_view_desc);
    rtv->IsValid()) {
    return rtv;
  }

  auto rtv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_RTV, DescriptorVisibility::kCpuOnly);
  if (!rtv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "GroundGridPass: Failed to allocate RTV descriptor handle");
  }
  const auto rtv = registry.RegisterView(
    color_texture, std::move(rtv_desc_handle), rtv_view_desc);
  if (!rtv->IsValid()) {
    throw std::runtime_error(
      "GroundGridPass: Failed to register RTV with resource registry");
  }
  return rtv;
}

auto PrepareDepthShaderResourceView(Texture& depth_texture,
  ResourceRegistry& registry, DescriptorAllocator& allocator)
  -> std::pair<oxygen::graphics::NativeView, uint32_t>
{
  using oxygen::TextureType;

  const auto& tex_desc = depth_texture.GetDescriptor();

  oxygen::Format srv_format = tex_desc.format;
  if (tex_desc.format == oxygen::Format::kDepth32) {
    srv_format = oxygen::Format::kR32Float;
  }

  oxygen::graphics::TextureViewDescription srv_view_desc {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = DescriptorVisibility::kShaderVisible,
    .format = srv_format,
    .dimension = tex_desc.texture_type,
    .sub_resources = { .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = (tex_desc.texture_type == TextureType::kTexture3D
          ? tex_desc.depth
          : tex_desc.array_size) },
    .is_read_only_dsv = false,
  };

  if (const auto srv = registry.Find(depth_texture, srv_view_desc);
    srv->IsValid()) {
    return { srv, oxygen::kInvalidShaderVisibleIndex.get() };
  }
  auto srv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_SRV, DescriptorVisibility::kShaderVisible);
  if (!srv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "GroundGridPass: Failed to allocate SRV descriptor handle");
  }
  const uint32_t srv_index
    = allocator.GetShaderVisibleIndex(srv_desc_handle).get();
  const auto srv = registry.RegisterView(
    depth_texture, std::move(srv_desc_handle), srv_view_desc);
  if (!srv->IsValid()) {
    throw std::runtime_error(
      "GroundGridPass: Failed to register depth SRV with resource registry");
  }
  return { srv, srv_index };
}

} // namespace

GroundGridPass::GroundGridPass(std::shared_ptr<GroundGridPassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "GroundGridPass")
  , config_(std::move(config))
{
}

GroundGridPass::~GroundGridPass() { ReleasePassConstantsBuffer(); }

auto GroundGridPass::ValidateConfig() -> void
{
  [[maybe_unused]] const auto& _ = GetColorTexture();
}

auto GroundGridPass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  recorder.RequireResourceState(
    GetColorTexture(), graphics::ResourceStates::kRenderTarget);

  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    recorder.RequireResourceState(
      *depth_texture, graphics::ResourceStates::kDepthRead);
  }

  recorder.FlushBarriers();

  EnsurePassConstantsBuffer();
  UpdatePassConstants();

  co_return;
}

auto GroundGridPass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (config_ != nullptr && !config_->enabled) {
    co_return;
  }

  if (Context().scene_constants == nullptr) {
    LOG_F(ERROR, "GroundGridPass: SceneConstants not bound; skipping draw");
    co_return;
  }
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());

  SetupViewPortAndScissors(recorder);
  SetupRenderTargets(recorder);

  const uint32_t pass_constants_index = pass_constants_index_.IsValid()
    ? pass_constants_index_.get()
    : oxygen::kInvalidShaderVisibleIndex.get();
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants), 0U, 0);
  recorder.SetGraphicsRoot32BitConstant(
    static_cast<uint32_t>(binding::RootParam::kRootConstants),
    pass_constants_index, 1);

  recorder.Draw(3, 1, 0, 0);

  Context().RegisterPass(this);

  co_return;
}

auto GroundGridPass::EnsurePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ && pass_constants_index_.IsValid()) {
    return;
  }

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  const graphics::BufferDesc desc {
    .size_bytes = kConstantsBufferMinSize,
    .usage = graphics::BufferUsage::kConstant,
    .memory = graphics::BufferMemory::kUpload,
    .debug_name = "GroundGridPass_Constants",
  };

  pass_constants_buffer_ = graphics.CreateBuffer(desc);
  if (pass_constants_buffer_ == nullptr) {
    throw std::runtime_error(
      "GroundGridPass: Failed to create pass constants buffer");
  }
  pass_constants_buffer_->SetName(desc.debug_name);

  pass_constants_mapped_ptr_
    = static_cast<std::byte*>(pass_constants_buffer_->Map(0, desc.size_bytes));
  if (pass_constants_mapped_ptr_ == nullptr) {
    throw std::runtime_error(
      "GroundGridPass: Failed to map pass constants buffer");
  }

  graphics::BufferViewDescription cbv_view_desc;
  cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_view_desc.range = { 0U, desc.size_bytes };

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  if (!cbv_handle.IsValid()) {
    throw std::runtime_error("GroundGridPass: Failed to allocate CBV handle");
  }
  pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  SetPassConstantsIndex(pass_constants_index_);

  registry.Register(pass_constants_buffer_);
  registry.RegisterView(
    *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
}

auto GroundGridPass::UpdatePassConstants() -> void
{
  if (pass_constants_mapped_ptr_ == nullptr) {
    return;
  }

  GroundGridPassConstants constants {};

  if (Context().current_view.resolved_view != nullptr) {
    constants.inv_view_proj
      = Context().current_view.resolved_view->InverseViewProjection();
  }

  if (const Texture* depth_texture = GetDepthTexture();
    depth_texture != nullptr) {
    auto& graphics = Context().GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();

    if (depth_texture != last_depth_texture_) {
      last_depth_texture_ = depth_texture;
      depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
    }

    if (!depth_srv_index_.IsValid()) {
      const auto [srv_view, srv_index] = PrepareDepthShaderResourceView(
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        const_cast<Texture&>(*depth_texture), registry, allocator);
      if (srv_index != oxygen::kInvalidShaderVisibleIndex.get()) {
        depth_srv_index_ = oxygen::ShaderVisibleIndex { srv_index };
      }
    }
  } else {
    depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
    last_depth_texture_ = nullptr;
  }
  constants.depth_srv_index = depth_srv_index_.get();

  if (config_) {
    const float major_every = std::max(1.0F, static_cast<float>(
      config_->major_every == 0U ? 1U : config_->major_every));

    float fade_start = config_->fade_start;
    float fade_end = config_->fade_end;
    if (fade_end <= 0.0F && config_->plane_size > 0.0F) {
      fade_end = config_->plane_size;
      if (fade_start <= 0.0F) {
        fade_start = fade_end * 0.7F;
      }
    }

    constants.grid_params0 = glm::vec4(
      0.0F, config_->spacing, major_every, config_->line_thickness);
    constants.grid_params1
      = glm::vec4(config_->major_thickness, config_->axis_thickness, fade_start,
        fade_end);
    constants.grid_params2
      = glm::vec4(config_->plane_size, config_->origin.x, config_->origin.y,
        0.0F);
    constants.grid_params3 = glm::vec4(config_->fade_power,
      std::max(config_->thickness_max_scale, 1.0F),
      std::max(config_->depth_bias, 0.0F),
      std::max(config_->horizon_boost, 0.0F));

    constants.minor_color = glm::vec4(config_->minor_color.r,
      config_->minor_color.g, config_->minor_color.b,
      config_->minor_color.a);
    constants.major_color = glm::vec4(config_->major_color.r,
      config_->major_color.g, config_->major_color.b,
      config_->major_color.a);
    constants.axis_color_x = glm::vec4(config_->axis_color_x.r,
      config_->axis_color_x.g, config_->axis_color_x.b,
      config_->axis_color_x.a);
    constants.axis_color_y = glm::vec4(config_->axis_color_y.r,
      config_->axis_color_y.g, config_->axis_color_y.b,
      config_->axis_color_y.a);
    constants.origin_color = glm::vec4(config_->origin_color.r,
      config_->origin_color.g, config_->origin_color.b,
      config_->origin_color.a);

    static std::atomic<bool> logged_once { false };
    if (!logged_once.exchange(true)) {
      LOG_F(INFO,
        "GroundGridPass: UpdatePassConstants spacing={} major_every={} "
        "line_thickness={} major_thickness={} minor_color=({}, {}, {}, {}) "
        "major_color=({}, {}, {}, {})",
        constants.grid_params0.y,
        static_cast<uint32_t>(constants.grid_params0.z),
        constants.grid_params0.w, constants.grid_params1.x,
        constants.minor_color.r, constants.minor_color.g,
        constants.minor_color.b, constants.minor_color.a,
        constants.major_color.r, constants.major_color.g,
        constants.major_color.b, constants.major_color.a);
    }
  }

  std::memcpy(pass_constants_mapped_ptr_, &constants, sizeof(constants));
}

auto GroundGridPass::ReleasePassConstantsBuffer() -> void
{
  if (pass_constants_buffer_ == nullptr) {
    pass_constants_mapped_ptr_ = nullptr;
    return;
  }

  if (pass_constants_buffer_->IsMapped()) {
    pass_constants_buffer_->UnMap();
  }

  pass_constants_mapped_ptr_ = nullptr;
  pass_constants_buffer_.reset();
  pass_constants_index_ = oxygen::kInvalidShaderVisibleIndex;
  SetPassConstantsIndex(pass_constants_index_);
  depth_srv_index_ = oxygen::kInvalidShaderVisibleIndex;
  last_depth_texture_ = nullptr;
}

auto GroundGridPass::GetColorTexture() const -> const Texture&
{
  if ((config_ != nullptr) && (config_->color_texture != nullptr)) {
    return *config_->color_texture;
  }
  const auto* fb = GetFramebuffer();
  if (fb != nullptr && !fb->GetDescriptor().color_attachments.empty()
    && (fb->GetDescriptor().color_attachments[0].texture != nullptr)) {
    return *fb->GetDescriptor().color_attachments[0].texture;
  }
  throw std::runtime_error("GroundGridPass: No valid color texture found.");
}

auto GroundGridPass::GetFramebuffer() const -> const Framebuffer*
{
  return Context().framebuffer.get();
}

auto GroundGridPass::SetupRenderTargets(CommandRecorder& recorder) const -> void
{
  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto& color_texture = const_cast<Texture&>(GetColorTexture());
  const auto color_rtv
    = PrepareRenderTargetView(color_texture, registry, allocator);
  std::array rtvs { color_rtv };

  recorder.SetRenderTargets(std::span(rtvs), std::nullopt);
}

auto GroundGridPass::SetupViewPortAndScissors(CommandRecorder& recorder) const
  -> void
{
  const auto& tex_desc = GetColorTexture().GetDescriptor();
  const auto width = tex_desc.width;
  const auto height = tex_desc.height;

  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(width),
    .bottom = static_cast<int32_t>(height),
  };
  recorder.SetScissors(scissors);
}

auto GroundGridPass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  using graphics::BlendFactor;
  using graphics::BlendOp;
  using graphics::BlendTargetDesc;
  using graphics::ColorWriteMask;
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FillMode;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;
  using graphics::ShaderRequest;

  DepthStencilStateDesc ds_desc {
    .depth_test_enable = false,
    .depth_write_enable = false,
    .depth_func = CompareOp::kAlways,
    .stencil_enable = false,
    .stencil_read_mask = 0xFF,
    .stencil_write_mask = 0xFF,
  };

  RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  const BlendTargetDesc blend_desc {
    .blend_enable = true,
    .src_blend = BlendFactor::kSrcAlpha,
    .dest_blend = BlendFactor::kInvSrcAlpha,
    .blend_op = BlendOp::kAdd,
    .src_blend_alpha = BlendFactor::kZero,
    .dest_blend_alpha = BlendFactor::kOne,
    .blend_op_alpha = BlendOp::kAdd,
    .write_mask = ColorWriteMask::kAll,
  };

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { color_tex_desc.format },
    .depth_stencil_format = oxygen::Format::kUnknown,
    .sample_count = color_tex_desc.sample_count,
  };

  auto generated_bindings = BuildRootBindings();
  std::vector<graphics::ShaderDefine> ps_defines;
  if (graphics::detail::IsHdr(color_tex_desc.format)) {
    ps_defines.push_back(graphics::ShaderDefine {
      .name = "OXYGEN_HDR_OUTPUT",
      .value = "1",
    });
  }

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = oxygen::ShaderType::kVertex,
      .source_path = "Renderer/GroundGrid_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = oxygen::ShaderType::kPixel,
      .source_path = "Renderer/GroundGrid_PS.hlsl",
      .entry_point = "PS",
      .defines = ps_defines,
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .AddBlendTarget(blend_desc)
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .Build();
}

auto GroundGridPass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  const auto& color_tex_desc = GetColorTexture().GetDescriptor();
  const auto& layout = last_built->FramebufferLayout();

  if (layout.color_target_formats.empty()
    || (layout.color_target_formats[0] != color_tex_desc.format)) {
    return true;
  }

  if (layout.sample_count != color_tex_desc.sample_count) {
    return true;
  }

  return false;
}

auto GroundGridPass::GetDepthTexture() const -> const Texture*
{
  if (const auto* depth_pass = Context().GetPass<engine::DepthPrePass>();
    depth_pass != nullptr) {
    return &depth_pass->GetDepthTexture();
  }

  const auto* fb = GetFramebuffer();
  if (fb != nullptr && fb->GetDescriptor().depth_attachment.IsValid()
    && (fb->GetDescriptor().depth_attachment.texture != nullptr)) {
    return fb->GetDescriptor().depth_attachment.texture.get();
  }

  return nullptr;
}
