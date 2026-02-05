//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/Passes/SkyCaptureConstants.h>
#include <Oxygen/Renderer/Passes/SkyCapturePass.h>

#include <stdexcept>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
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
#include <Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>

namespace oxygen::engine {

using graphics::BufferDesc;
using graphics::BufferMemory;
using graphics::BufferUsage;
using graphics::CommandRecorder;
using graphics::DescriptorAllocator;
using graphics::DescriptorVisibility;
using graphics::NativeObject;
using graphics::ResourceRegistry;
using graphics::ResourceViewType;
using graphics::Texture;
using graphics::TextureDesc;

SkyCapturePass::SkyCapturePass(observer_ptr<oxygen::Graphics> gfx,
  std::shared_ptr<SkyCapturePassConfig> config)
  : GraphicsRenderPass(config ? config->debug_name : "SkyCapturePass", true)
  , gfx_(gfx)
  , config_(std::move(config))
  , captured_cubemap_(nullptr)
  , captured_cubemap_srv_view_({})
  , face_rtvs_({})
  , all_faces_fb_(nullptr)
  , face_constants_buffer_(nullptr)
  , face_constants_mapped_(nullptr)
  , face_constants_cbv_({})
{
}

SkyCapturePass::~SkyCapturePass()
{
  if (!gfx_.get()) {
    return;
  }
  auto& registry = gfx_->GetResourceRegistry();

  if (captured_cubemap_) {
    if (registry.Contains(*captured_cubemap_)) {
      if (captured_cubemap_srv_view_.get().IsValid()) {
        registry.UnRegisterView(*captured_cubemap_, captured_cubemap_srv_view_);
      }
      for (auto& rtv : face_rtvs_) {
        if (rtv.get().IsValid()) {
          registry.UnRegisterView(*captured_cubemap_, rtv);
        }
      }
      registry.UnRegisterResource(*captured_cubemap_);
    }
  }

  if (face_constants_buffer_) {
    if (registry.Contains(*face_constants_buffer_)) {
      if (face_constants_cbv_.get().IsValid()) {
        registry.UnRegisterView(*face_constants_buffer_, face_constants_cbv_);
      }
      registry.UnRegisterResource(*face_constants_buffer_);
    }
    if (face_constants_mapped_) {
      face_constants_buffer_->UnMap();
    }
  }
}

auto SkyCapturePass::ValidateConfig() -> void
{
  if (config_ == nullptr) {
    throw std::runtime_error("SkyCapturePass: config is required");
  }
  if (config_->resolution == 0) {
    throw std::runtime_error("SkyCapturePass: resolution must be > 0");
  }
}

auto SkyCapturePass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  EnsureResourcesCreated();

  // If already captured and not marked dirty, we can skip.
  if (is_captured_) {
    co_return;
  }

  // Ensure internal resources are being tracked by this recorder.
  if (!recorder.IsResourceTracked(*captured_cubemap_)) {
    // Start from kCommon (initial state) and transition to RENDER_TARGET.
    recorder.BeginTrackingResourceState(
      *captured_cubemap_, graphics::ResourceStates::kCommon, false);
  }

  // Transition cubemap to RENDER_TARGET state for capture.
  recorder.RequireResourceState(
    *captured_cubemap_, graphics::ResourceStates::kRenderTarget);

  if (!recorder.IsResourceTracked(*face_constants_buffer_)) {
    recorder.BeginTrackingResourceState(
      *face_constants_buffer_, graphics::ResourceStates::kCommon, false);
  }
  // Constant buffers stay in kConstantBuffer.
  recorder.RequireResourceState(
    *face_constants_buffer_, graphics::ResourceStates::kConstantBuffer);

  recorder.FlushBarriers();

  co_return;
}

auto SkyCapturePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  if (is_captured_) {
    co_return;
  }

  LOG_F(INFO, "SkyCapturePass: Capturing sky to {}x{} cubemap",
    config_->resolution, config_->resolution);

  // Bind EnvironmentDynamicData for exposure and other dynamic data.
  if (const auto manager = Context().env_dynamic_manager) {
    const auto view_id = Context().current_view.view_id;
    manager->UpdateIfNeeded(view_id);
    if (const auto env_addr = manager->GetGpuVirtualAddress(view_id);
      env_addr != 0) {
      recorder.SetGraphicsRootConstantBufferView(
        static_cast<uint32_t>(binding::RootParam::kEnvironmentDynamicData),
        env_addr);
    }
  }

  SetupViewPortAndScissors(recorder);

  // Clear the whole cubemap once using the single multi-face FB.
  // Use the clear value defined in the texture descriptor to avoid D3D12
  // warnings.
  const graphics::Color clear_color { 0.0F, 0.0F, 0.0F, 1.0F };
  recorder.ClearFramebuffer(*all_faces_fb_,
    std::vector<std::optional<graphics::Color>> { clear_color }, std::nullopt,
    std::nullopt);

  const float aspect = 1.0F;
  const float fov = glm::radians(90.0F);
  const float near_plane = 0.1F;
  const float far_plane = 100.0F;
  const glm::mat4 proj = glm::perspective(fov, aspect, near_plane, far_plane);

  // Directions for 6 cubemap faces (Target, Up)
  struct FaceDir {
    glm::vec3 target;
    glm::vec3 up;
  };
  std::array<FaceDir, 6> face_dirs = {
    FaceDir { { 1, 0, 0 }, { 0, 0, 1 } }, // +X
    FaceDir { { -1, 0, 0 }, { 0, 0, 1 } }, // -X
    FaceDir { { 0, 1, 0 }, { 0, 0, 1 } }, // +Y
    FaceDir { { 0, -1, 0 }, { 0, 0, 1 } }, // -Y
    FaceDir { { 0, 0, 1 }, { 0, -1, 0 } }, // +Z
    FaceDir { { 0, 0, -1 }, { 0, 1, 0 } } // -Z
  };

  const glm::vec3 eye(0.0F);

  for (uint32_t i = 0; i < 6; ++i) {
    // Set render target for this face directly via RTV
    recorder.SetRenderTargets(std::span(&face_rtvs_[i], 1), std::nullopt);

    // Update face constants
    SkyCaptureFaceConstants face_const {};
    face_const.view_matrix
      = glm::lookAt(eye, face_dirs[i].target, face_dirs[i].up);
    face_const.projection_matrix = proj;

    std::memcpy(face_constants_mapped_, &face_const, sizeof(face_const));

    // Bind the face constants index via root constants.
    recorder.SetGraphicsRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      face_constants_cbv_index_.get(), 1);

    recorder.Draw(3, 1, 0, 0);
  }

  // Transition cubemap to SHADER_RESOURCE state so it can be used for IBL.
  recorder.RequireResourceState(
    *captured_cubemap_, graphics::ResourceStates::kShaderResource);
  recorder.FlushBarriers();

  is_captured_ = true;
  ++capture_generation_;
  co_return;
}

auto SkyCapturePass::CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc
{
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::FillMode;
  using graphics::FramebufferLayoutDesc;
  using graphics::GraphicsPipelineDesc;
  using graphics::PrimitiveType;
  using graphics::RasterizerStateDesc;
  using graphics::ShaderRequest;

  // Render to RGBA16F cubemap faces. No depth needed.
  DepthStencilStateDesc ds_desc {
    .depth_test_enable = false,
    .depth_write_enable = false,
    .depth_func = CompareOp::kAlways,
    .stencil_enable = false,
  };

  RasterizerStateDesc raster_desc {
    .fill_mode = FillMode::kSolid,
    .cull_mode = CullMode::kNone,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };

  const FramebufferLayoutDesc fb_layout_desc {
    .color_target_formats = { Format::kRGBA16Float },
    .depth_stencil_format = Format::kUnknown,
    .sample_count = 1,
  };

  auto generated_bindings = BuildRootBindings();

  return GraphicsPipelineDesc::Builder()
    .SetVertexShader(ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Passes/Sky/SkyCapture_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Passes/Sky/SkyCapture_PS.hlsl",
      .entry_point = "PS",
      .defines = {},
    })
    .SetPrimitiveTopology(PrimitiveType::kTriangleList)
    .SetRasterizerState(raster_desc)
    .SetDepthStencilState(ds_desc)
    .SetBlendState({})
    .SetFramebufferLayout(fb_layout_desc)
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      generated_bindings.data(), generated_bindings.size()))
    .Build();
}

auto SkyCapturePass::NeedRebuildPipelineState() const -> bool
{
  return !LastBuiltPsoDesc().has_value();
}

auto SkyCapturePass::EnsureResourcesCreated() -> void
{
  if (captured_cubemap_) {
    return;
  }

  auto& graphics = Context().GetGraphics();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& registry = graphics.GetResourceRegistry();

  // 1. Create captured cubemap
  TextureDesc desc;
  desc.width = config_->resolution;
  desc.height = config_->resolution;
  desc.depth = 1;
  desc.array_size = 6;
  desc.mip_levels = 1;
  desc.sample_count = 1;
  desc.format = Format::kRGBA16Float;
  desc.texture_type = oxygen::TextureType::kTextureCube;
  desc.debug_name = "SkyCapture_Cubemap";
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.use_clear_value = true;
  desc.clear_value = { 0.0F, 0.0F, 0.0F, 1.0F };

  captured_cubemap_ = graphics.CreateTexture(desc);

  // 2. Create ONE Framebuffer for the whole cubemap.
  // THIS WILL REGISTER THE TEXTURE with the registry.
  graphics::FramebufferDesc all_faces_fb_desc;
  all_faces_fb_desc.AddColorAttachment(captured_cubemap_,
    { .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = 6 });
  all_faces_fb_ = graphics.CreateFramebuffer(all_faces_fb_desc);

  // 3. Now that the texture is registered, create the remaining views.

  // SRV for the cubemap
  auto srv_handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  graphics::TextureViewDescription srv_desc;
  srv_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  srv_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  srv_desc.format = desc.format;
  srv_desc.dimension = oxygen::TextureType::kTextureCube;
  srv_desc.sub_resources.base_mip_level = 0;
  srv_desc.sub_resources.num_mip_levels = 1;
  srv_desc.sub_resources.base_array_slice = 0;
  srv_desc.sub_resources.num_array_slices = 6;

  captured_cubemap_srv_ = allocator.GetShaderVisibleIndex(srv_handle);
  captured_cubemap_srv_view_ = registry.RegisterView(
    *captured_cubemap_, std::move(srv_handle), srv_desc);

  // RTVs for each face
  face_rtvs_.resize(6);
  for (uint32_t i = 0; i < 6; ++i) {
    auto rtv_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_RTV,
        graphics::DescriptorVisibility::kCpuOnly);
    graphics::TextureViewDescription rtv_desc;
    rtv_desc.view_type = graphics::ResourceViewType::kTexture_RTV;
    rtv_desc.visibility = graphics::DescriptorVisibility::kCpuOnly;
    rtv_desc.format = desc.format;
    rtv_desc.dimension = oxygen::TextureType::kTexture2DArray;
    rtv_desc.sub_resources.base_mip_level = 0;
    rtv_desc.sub_resources.num_mip_levels = 1;
    rtv_desc.sub_resources.base_array_slice = i;
    rtv_desc.sub_resources.num_array_slices = 1;

    face_rtvs_[i] = registry.RegisterView(
      *captured_cubemap_, std::move(rtv_handle), rtv_desc);
  }

  // 4. Create and register the face constants buffer.
  BufferDesc cb_desc {
    .size_bytes = 256u, // Must be 256-byte aligned for D3D12 CBV
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = "SkyCapture_FaceConstants",
  };
  face_constants_buffer_ = graphics.CreateBuffer(cb_desc);
  registry.Register(face_constants_buffer_);
  face_constants_mapped_ = face_constants_buffer_->Map();

  auto cbv_handle
    = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
  graphics::BufferViewDescription cbv_view_desc;
  cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
  cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  cbv_view_desc.range = { 0, 256u };

  face_constants_cbv_index_ = allocator.GetShaderVisibleIndex(cbv_handle);
  face_constants_cbv_ = registry.RegisterView(
    *face_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
}

auto SkyCapturePass::SetupViewPortAndScissors(CommandRecorder& recorder) const
  -> void
{
  const float res = static_cast<float>(config_->resolution);
  const ViewPort viewport {
    .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = res,
    .height = res,
    .min_depth = 0.0F,
    .max_depth = 1.0F,
  };
  recorder.SetViewport(viewport);

  const Scissors scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(config_->resolution),
    .bottom = static_cast<int32_t>(config_->resolution),
  };
  recorder.SetScissors(scissors);
}

} // namespace oxygen::engine
