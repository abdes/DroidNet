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
#include <Oxygen/Renderer/Internal/EnvironmentStaticDataManager.h>
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
{
}

SkyCapturePass::~SkyCapturePass()
{
  for (auto& [_, state] : capture_state_by_view_) {
    ReleaseStateResources(state);
  }
}

auto SkyCapturePass::MarkDirty(const ViewId view_id) noexcept -> void
{
  if (const auto it = capture_state_by_view_.find(view_id);
    it != capture_state_by_view_.end()) {
    it->second.is_captured = false;
  }
}

auto SkyCapturePass::EraseViewState(const ViewId view_id) -> void
{
  if (const auto it = capture_state_by_view_.find(view_id);
    it != capture_state_by_view_.end()) {
    ReleaseStateResources(it->second);
    capture_state_by_view_.erase(it);
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
  const auto view_id = Context().current_view.view_id;
  auto& state = EnsureResourcesCreated(view_id);

  // If already captured and not marked dirty, we can skip.
  if (state.is_captured) {
    co_return;
  }

  // Ensure internal resources are being tracked by this recorder.
  // Use the last known GPU state (not always kCommon) so that recapture after
  // MarkDirty() emits correct barriers.
  if (!recorder.IsResourceTracked(*state.captured_cubemap)) {
    recorder.BeginTrackingResourceState(
      *state.captured_cubemap, state.cubemap_last_state, false);
  }

  // Transition cubemap to RENDER_TARGET state for capture.
  recorder.RequireResourceState(
    *state.captured_cubemap, graphics::ResourceStates::kRenderTarget);
  state.cubemap_last_state = graphics::ResourceStates::kRenderTarget;

  if (!recorder.IsResourceTracked(*state.face_constants_buffer)) {
    recorder.BeginTrackingResourceState(
      *state.face_constants_buffer, state.face_cb_last_state, false);
  }
  // Constant buffers stay in kConstantBuffer.
  recorder.RequireResourceState(
    *state.face_constants_buffer, graphics::ResourceStates::kConstantBuffer);
  state.face_cb_last_state = graphics::ResourceStates::kConstantBuffer;

  recorder.FlushBarriers();

  co_return;
}

auto SkyCapturePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  const auto view_id = Context().current_view.view_id;
  auto& state = EnsureResourcesCreated(view_id);

  if (state.is_captured) {
    // This pass is often invoked by the renderer when upstream state changed
    // (e.g. sky-atmosphere LUT generation). If we are not marked dirty,
    // execution will be a no-op; log once per generation/view to validate.
    static std::unordered_map<std::uint32_t, std::uint64_t>
      last_logged_skip_gen_by_view;
    const auto u_view_id = view_id.get();
    const auto gen = state.capture_generation;
    const auto it = last_logged_skip_gen_by_view.find(u_view_id);
    if (it == last_logged_skip_gen_by_view.end() || it->second != gen) {
      last_logged_skip_gen_by_view[u_view_id] = gen;
      const auto env_manager
        = Context().GetRenderer().GetEnvironmentStaticDataManager();
      LOG_F(INFO,
        "SkyCapturePass: skipping (already captured) (view={} frame_slot={} "
        "frame_seq={} env_srv={} slot={} gen={})",
        u_view_id, Context().frame_slot.get(), Context().frame_sequence.get(),
        env_manager ? env_manager->GetSrvIndex(view_id).get() : 0U,
        state.captured_cubemap_srv.get(), gen);
    }
    co_return;
  }

  const auto env_manager
    = Context().GetRenderer().GetEnvironmentStaticDataManager();
  LOG_F(INFO,
    "SkyCapturePass: capture begin (view={} frame_slot={} frame_seq={} "
    "env_srv={} res={} slot={})",
    view_id.get(), Context().frame_slot.get(), Context().frame_sequence.get(),
    env_manager ? env_manager->GetSrvIndex(view_id).get() : 0U,
    config_->resolution, state.captured_cubemap_srv.get());

  // SkyCapture shaders load EnvironmentStaticData using SceneConstants
  // (bindless_env_static_slot + frame_slot). Bind it explicitly to avoid any
  // root-CBV leakage from previous passes.
  if (Context().scene_constants == nullptr) {
    LOG_F(ERROR,
      "SkyCapturePass: missing SceneConstants (view={} frame_slot={} "
      "frame_seq={})",
      view_id.get(), Context().frame_slot.get(),
      Context().frame_sequence.get());
    co_return;
  }
  recorder.SetGraphicsRootConstantBufferView(
    static_cast<uint32_t>(binding::RootParam::kSceneConstants),
    Context().scene_constants->GetGPUVirtualAddress());

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

  // Transition cubemap to RENDER_TARGET state so we can clear and draw.
  // The framebuffer attachment logic might not automatically transition
  // sub-resources correctly if they are used as bindings elsewhere.
  recorder.RequireResourceState(
    *state.captured_cubemap, graphics::ResourceStates::kRenderTarget);
  state.cubemap_last_state = graphics::ResourceStates::kRenderTarget;
  recorder.FlushBarriers();

  // Clear the whole cubemap once using the single multi-face FB.
  // Use the clear value defined in the texture descriptor to avoid D3D12
  // warnings.
  const graphics::Color clear_color { 0.0F, 0.0F, 0.0F, 1.0F };
  recorder.ClearFramebuffer(*state.all_faces_fb,
    std::vector<std::optional<graphics::Color>> { clear_color }, std::nullopt,
    std::nullopt);

  const float aspect = 1.0F;
  const float fov = glm::radians(90.0F);
  const float near_plane = 0.1F;
  const float far_plane = 100.0F;
  const glm::mat4 proj = glm::perspective(fov, aspect, near_plane, far_plane);

  // Directions for 6 cubemap faces (Target, Up) mapped from Oxygen world-space
  // to the standard GPU cubemap convention (Y-up).
  //
  // Oxygen: X=Right, Y=Back, Z=Up (Forward is -Y)
  // GPU Faces: 0:+X, 1:-X, 2:+Y, 3:-Y, 4:+Z, 5:-Z
  struct FaceBasis {
    glm::vec3 right;
    glm::vec3 up;
    glm::vec3 forward;
  };
  // Oxygen-space basis derived from kGpuCubeFaceBases (center/right/up)
  // mapped via OxygenDirFromCubemapSamplingDir.
  std::array<FaceBasis, 6> face_basis = {
    FaceBasis { { 0, 1, 0 }, { 0, 0, 1 }, { 1, 0, 0 } }, // Face 0 (+X)
    FaceBasis { { 0, -1, 0 }, { 0, 0, 1 }, { -1, 0, 0 } }, // Face 1 (-X)
    FaceBasis { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } }, // Face 2 (+Y)
    FaceBasis { { 1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 } }, // Face 3 (-Y)
    FaceBasis { { 1, 0, 0 }, { 0, 0, 1 }, { 0, -1, 0 } }, // Face 4 (+Z)
    FaceBasis { { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } } // Face 5 (-Z)
  };
  const uint32_t kFaceConstantSize = 256;

  for (uint32_t i = 0; i < 6; ++i) {
    // Set render target for this face directly via RTV
    recorder.SetRenderTargets(std::span(&state.face_rtvs[i], 1), std::nullopt);

    // Update face constants at the specific offset for this face
    const glm::vec3 right = glm::normalize(face_basis[i].right);
    const glm::vec3 up = glm::normalize(face_basis[i].up);
    const glm::vec3 forward = glm::normalize(face_basis[i].forward);

    SkyCaptureFaceConstants face_const {};
    // View matrix: rows are right, up, -forward (world -> view).
    // glm is column-major, so assign columns from row components explicitly.
    glm::mat4 view(1.0F);
    view[0] = glm::vec4(right.x, up.x, -forward.x, 0.0F);
    view[1] = glm::vec4(right.y, up.y, -forward.y, 0.0F);
    view[2] = glm::vec4(right.z, up.z, -forward.z, 0.0F);
    view[3] = glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
    face_const.view_matrix = view;
    face_const.projection_matrix = proj;

    auto* dest = static_cast<uint8_t*>(state.face_constants_mapped)
      + (i * kFaceConstantSize);
    std::memcpy(dest, &face_const, sizeof(face_const));

    // Bind the specific face constants index via root constants.
    // GPU will see the correct descriptor pointing to the correct buffer slice.
    recorder.SetGraphicsRoot32BitConstant(
      static_cast<uint32_t>(binding::RootParam::kRootConstants),
      state.face_constants_indices[i].get(), 1);

    recorder.Draw(3, 1, 0, 0);
  }

  // Transition cubemap to SHADER_RESOURCE state so it can be used for IBL.
  recorder.RequireResourceState(
    *state.captured_cubemap, graphics::ResourceStates::kShaderResource);
  state.cubemap_last_state = graphics::ResourceStates::kShaderResource;
  recorder.FlushBarriers();

  state.is_captured = true;
  ++state.capture_generation;

  LOG_F(INFO, "SkyCapturePass: capture done (view={}, slot={} gen={})",
    view_id.get(), state.captured_cubemap_srv.get(), state.capture_generation);
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
      .source_path = "Atmosphere/SkyCapture_VS.hlsl",
      .entry_point = "VS",
      .defines = {},
    })
    .SetPixelShader(ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Atmosphere/SkyCapture_PS.hlsl",
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

auto SkyCapturePass::ReleaseStateResources(CaptureState& state) -> void
{
  if (!gfx_.get()) {
    return;
  }
  auto& registry = gfx_->GetResourceRegistry();

  if (state.captured_cubemap) {
    if (registry.Contains(*state.captured_cubemap)) {
      if (state.captured_cubemap_srv_view.get().IsValid()) {
        registry.UnRegisterView(
          *state.captured_cubemap, state.captured_cubemap_srv_view);
      }
      for (auto& rtv : state.face_rtvs) {
        if (rtv.get().IsValid()) {
          registry.UnRegisterView(*state.captured_cubemap, rtv);
        }
      }
      registry.UnRegisterResource(*state.captured_cubemap);
    }
    state.captured_cubemap.reset();
  }

  if (state.face_constants_buffer) {
    if (registry.Contains(*state.face_constants_buffer)) {
      for (auto& cbv : state.face_constants_cbvs) {
        if (cbv.get().IsValid()) {
          registry.UnRegisterView(*state.face_constants_buffer, cbv);
        }
      }
      registry.UnRegisterResource(*state.face_constants_buffer);
    }
    if (state.face_constants_mapped != nullptr) {
      state.face_constants_buffer->UnMap();
      state.face_constants_mapped = nullptr;
    }
    state.face_constants_buffer.reset();
  }

  state.captured_cubemap_srv = kInvalidShaderVisibleIndex;
  state.captured_cubemap_srv_view = {};
  state.face_rtvs.clear();
  state.all_faces_fb.reset();
  state.face_constants_cbvs.clear();
  state.face_constants_indices.clear();
}

auto SkyCapturePass::EnsureResourcesCreated(const ViewId view_id)
  -> CaptureState&
{
  auto [it, inserted] = capture_state_by_view_.try_emplace(view_id);
  auto& state = it->second;
  if (!inserted && state.captured_cubemap) {
    return state;
  }

  auto& graphics = Context().GetGraphics();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& registry = graphics.GetResourceRegistry();

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

  state.captured_cubemap = graphics.CreateTexture(desc);

  graphics::FramebufferDesc all_faces_fb_desc;
  all_faces_fb_desc.AddColorAttachment(state.captured_cubemap,
    { .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 0,
      .num_array_slices = 6 });
  state.all_faces_fb = graphics.CreateFramebuffer(all_faces_fb_desc);

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

  state.captured_cubemap_srv = allocator.GetShaderVisibleIndex(srv_handle);
  state.captured_cubemap_srv_view = registry.RegisterView(
    *state.captured_cubemap, std::move(srv_handle), srv_desc);

  state.face_rtvs.resize(6);
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

    state.face_rtvs[i] = registry.RegisterView(
      *state.captured_cubemap, std::move(rtv_handle), rtv_desc);
  }

  constexpr uint32_t kFaceConstantSize = 256;
  BufferDesc cb_desc {
    .size_bytes = kFaceConstantSize * 6,
    .usage = BufferUsage::kConstant,
    .memory = BufferMemory::kUpload,
    .debug_name = "SkyCapture_FaceConstants",
  };
  state.face_constants_buffer = graphics.CreateBuffer(cb_desc);
  registry.Register(state.face_constants_buffer);
  state.face_constants_mapped = state.face_constants_buffer->Map();

  state.face_constants_cbvs.reserve(6);
  state.face_constants_indices.reserve(6);
  for (uint32_t i = 0; i < 6; ++i) {
    auto cbv_handle
      = allocator.Allocate(graphics::ResourceViewType::kConstantBuffer,
        graphics::DescriptorVisibility::kShaderVisible);
    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = graphics::ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { i * kFaceConstantSize, kFaceConstantSize };

    state.face_constants_indices.push_back(
      allocator.GetShaderVisibleIndex(cbv_handle));
    state.face_constants_cbvs.push_back(registry.RegisterView(
      *state.face_constants_buffer, std::move(cbv_handle), cbv_view_desc));
  }

  return state;
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
