// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.h>

#include <cstring>
#include <stdexcept>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/RenderContext.h>

using oxygen::Graphics;
using oxygen::ShaderVisibleIndex;
using oxygen::engine::VsmShadowRasterizerPass;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::ResourceViewType;

namespace {

struct ShadowRasterPassConstants {
  float alpha_cutoff_default { 0.5F };
  std::uint32_t _pad0 { 0U };
  std::uint32_t _pad1 { 0U };
  std::uint32_t _pad2 { 0U };
};
static_assert(sizeof(ShadowRasterPassConstants) == 16U);

} // namespace

namespace oxygen::engine {

struct VsmShadowRasterizerPass::Impl {
  observer_ptr<Graphics> gfx;
  std::shared_ptr<Config> config;
  std::optional<VsmShadowRasterizerPassInput> input {};
  std::vector<renderer::vsm::VsmShadowRasterPageJob> prepared_pages {};

  std::shared_ptr<Buffer> pass_constants_buffer {};
  void* pass_constants_mapped_ptr { nullptr };
  ShaderVisibleIndex pass_constants_index { kInvalidShaderVisibleIndex };

  Impl(observer_ptr<Graphics> gfx_, std::shared_ptr<Config> config_)
    : gfx(gfx_)
    , config(std::move(config_))
  {
  }

  ~Impl()
  {
    if (pass_constants_buffer && pass_constants_mapped_ptr != nullptr) {
      pass_constants_buffer->UnMap();
      pass_constants_mapped_ptr = nullptr;
    }

    if (gfx != nullptr && pass_constants_buffer != nullptr) {
      auto& registry = gfx->GetResourceRegistry();
      if (registry.Contains(*pass_constants_buffer)) {
        registry.UnRegisterResource(*pass_constants_buffer);
      }
    }
  }

  auto EnsurePassConstantsBuffer() -> void
  {
    if (pass_constants_buffer != nullptr) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    auto& allocator = gfx->GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = 256U,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = config->debug_name + "_PassConstants",
    };

    pass_constants_buffer = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(pass_constants_buffer.get(),
      "Failed to create VSM shadow-rasterizer pass constants buffer");
    pass_constants_buffer->SetName(desc.debug_name);
    registry.Register(pass_constants_buffer);

    pass_constants_mapped_ptr = pass_constants_buffer->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(pass_constants_mapped_ptr,
      "Failed to map VSM shadow-rasterizer pass constants buffer");

    const ShadowRasterPassConstants snapshot {};
    std::memcpy(pass_constants_mapped_ptr, &snapshot, sizeof(snapshot));

    graphics::BufferViewDescription cbv_desc;
    cbv_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_desc.range = { 0U, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(cbv_handle.IsValid(),
      "Failed to allocate VSM shadow-rasterizer constants CBV");

    pass_constants_index = allocator.GetShaderVisibleIndex(cbv_handle);
    const auto cbv = registry.RegisterView(
      *pass_constants_buffer, std::move(cbv_handle), cbv_desc);
    CHECK_F(
      cbv->IsValid(), "Failed to register VSM shadow-rasterizer constants CBV");
  }
};

VsmShadowRasterizerPass::VsmShadowRasterizerPass(
  const observer_ptr<Graphics> gfx, std::shared_ptr<Config> config)
  : GraphicsRenderPass(config ? config->debug_name : "VsmShadowRasterizerPass")
  , impl_(std::make_unique<Impl>(gfx, std::move(config)))
{
  DCHECK_NOTNULL_F(gfx.get());
  DCHECK_NOTNULL_F(impl_->config.get());
}

VsmShadowRasterizerPass::~VsmShadowRasterizerPass() = default;

auto VsmShadowRasterizerPass::SetInput(VsmShadowRasterizerPassInput input)
  -> void
{
  impl_->input = std::move(input);
}

auto VsmShadowRasterizerPass::ResetInput() noexcept -> void
{
  impl_->input.reset();
  impl_->prepared_pages.clear();
}

auto VsmShadowRasterizerPass::GetPreparedPageCount() const noexcept
  -> std::size_t
{
  return impl_->prepared_pages.size();
}

auto VsmShadowRasterizerPass::GetPreparedPages() const noexcept
  -> std::span<const renderer::vsm::VsmShadowRasterPageJob>
{
  return { impl_->prepared_pages.data(), impl_->prepared_pages.size() };
}

auto VsmShadowRasterizerPass::ValidateConfig() -> void
{
  if (impl_->gfx == nullptr) {
    throw std::runtime_error("VsmShadowRasterizerPass requires Graphics");
  }
  if (!impl_->config) {
    throw std::runtime_error("VsmShadowRasterizerPass requires Config");
  }
}

auto VsmShadowRasterizerPass::NeedRebuildPipelineState() const -> bool
{
  if (!LastBuiltPsoDesc().has_value()) {
    return true;
  }

  if (!impl_->input.has_value()
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    return false;
  }

  const auto& last_built = *LastBuiltPsoDesc();
  const auto& texture_desc
    = impl_->input->physical_pool.shadow_texture->GetDescriptor();
  return last_built.FramebufferLayout().depth_stencil_format
    != texture_desc.format
    || last_built.FramebufferLayout().sample_count != texture_desc.sample_count;
}

auto VsmShadowRasterizerPass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
{
  if (!impl_->input.has_value()
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    throw std::runtime_error(
      "VsmShadowRasterizerPass requires a shadow texture before pipeline "
      "creation");
  }

  const auto& shadow_desc
    = impl_->input->physical_pool.shadow_texture->GetDescriptor();
  const auto framebuffer_layout = graphics::FramebufferLayoutDesc {
    .color_target_formats = {},
    .depth_stencil_format = shadow_desc.format,
    .sample_count = shadow_desc.sample_count,
  };

  auto root_bindings = BuildRootBindings();
  return GraphicsPipelineDesc::Builder()
    .SetVertexShader({ .stage = oxygen::ShaderType::kVertex,
      .source_path = "Depth/DepthPrePass.hlsl",
      .entry_point = "VS" })
    .SetPixelShader({ .stage = oxygen::ShaderType::kPixel,
      .source_path = "Depth/DepthPrePass.hlsl",
      .entry_point = "PS" })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc {
      .fill_mode = graphics::FillMode::kSolid,
      .cull_mode = graphics::CullMode::kBack,
      .front_counter_clockwise = true,
      .depth_bias = 0.0F,
      .depth_bias_clamp = 0.0F,
      .slope_scaled_depth_bias = 0.0F,
      .depth_clip_enable = true,
      .multisample_enable = false,
      .antialiased_line_enable = false,
    })
    .SetDepthStencilState(graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = true,
      .depth_func = graphics::CompareOp::kLessOrEqual,
      .stencil_enable = false,
      .stencil_read_mask = 0xFF,
      .stencil_write_mask = 0xFF,
    })
    .SetBlendState({})
    .SetFramebufferLayout(framebuffer_layout)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName("VsmShadowRasterizerPass")
    .Build();
}

auto VsmShadowRasterizerPass::DoPrepareResources(CommandRecorder& recorder)
  -> co::Co<>
{
  impl_->prepared_pages.clear();
  if (!impl_->input.has_value() || !impl_->input->physical_pool.is_available
    || impl_->input->physical_pool.shadow_texture == nullptr) {
    DLOG_F(2,
      "VsmShadowRasterizerPass: skipped prepare because no valid input is "
      "bound");
    co_return;
  }

  impl_->EnsurePassConstantsBuffer();
  SetPassConstantsIndex(impl_->pass_constants_index);

  impl_->prepared_pages
    = renderer::vsm::BuildShadowRasterPageJobs(impl_->input->frame,
      impl_->input->physical_pool, impl_->input->projections);

  DLOG_F(2,
    "VsmShadowRasterizerPass: prepare map_count={} prepared_pages={} "
    "pool_identity={} page_size={} slices={}",
    impl_->input->projections.size(), impl_->prepared_pages.size(),
    impl_->input->physical_pool.pool_identity,
    impl_->input->physical_pool.page_size_texels,
    impl_->input->physical_pool.slice_count);

  const auto& shadow_texture = *impl_->input->physical_pool.shadow_texture;
  if (!recorder.IsResourceTracked(shadow_texture)) {
    recorder.BeginTrackingResourceState(
      shadow_texture, graphics::ResourceStates::kCommon, true);
  }
  recorder.RequireResourceState(
    shadow_texture, graphics::ResourceStates::kDepthWrite);
  recorder.FlushBarriers();

  co_return;
}

auto VsmShadowRasterizerPass::DoExecute(CommandRecorder& /*recorder*/)
  -> co::Co<>
{
  if (impl_->prepared_pages.empty()) {
    DLOG_F(2, "VsmShadowRasterizerPass: no prepared page jobs");
  } else {
    DLOG_F(2,
      "VsmShadowRasterizerPass: prepared {} page jobs; raster submission "
      "remains in a later slice",
      impl_->prepared_pages.size());
  }

  Context().RegisterPass(this);
  co_return;
}

} // namespace oxygen::engine
