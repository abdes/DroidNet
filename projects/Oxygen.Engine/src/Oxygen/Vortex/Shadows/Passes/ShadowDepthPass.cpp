//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Vortex/Shadows/Passes/ShadowDepthPass.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/Types/PassMask.h>

namespace oxygen::vortex::shadows {

namespace {

namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

constexpr std::uint32_t kShadowPassConstantsStride
  = packing::kConstantBufferAlignment;
constexpr float kUeCsmShadowSlopeScaleDepthBias = 3.0F;
constexpr float kUeDefaultUserShadowSlopeBias = 0.5F;
constexpr float kUeShadowMaxSlopeScaleDepthBias = 1.0F;

struct alignas(packing::kShaderDataFieldAlignment) ShadowPassConstants {
  glm::mat4 light_view_projection { 1.0F };
  glm::vec4 shadow_bias_parameters { 0.0F };
  glm::vec4 light_direction_to_source { 0.0F, -1.0F, 0.0F, 0.0F };
  glm::vec4 light_position_and_inv_range { 0.0F };
  std::uint32_t draw_metadata_slot { kInvalidShaderVisibleIndex.get() };
  std::uint32_t current_worlds_slot { kInvalidShaderVisibleIndex.get() };
  std::uint32_t instance_data_slot { kInvalidShaderVisibleIndex.get() };
  std::uint32_t _padding0 { 0U };
};

static_assert(sizeof(ShadowPassConstants) == 128U);
static_assert(offsetof(ShadowPassConstants, light_view_projection) == 0U);
static_assert(offsetof(ShadowPassConstants, shadow_bias_parameters) == 64U);
static_assert(offsetof(ShadowPassConstants, light_direction_to_source) == 80U);
static_assert(
  offsetof(ShadowPassConstants, light_position_and_inv_range) == 96U);
static_assert(offsetof(ShadowPassConstants, draw_metadata_slot) == 112U);

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

auto BuildVortexRootBindings() -> std::vector<graphics::RootBindingItem>
{
  std::vector<graphics::RootBindingItem> bindings;
  bindings.reserve(bindless_d3d12::kRootParamTableCount);

  for (std::uint32_t index = 0; index < bindless_d3d12::kRootParamTableCount;
       ++index) {
    const auto& desc = bindless_d3d12::kRootParamTable.at(index);
    auto binding = graphics::RootBindingDesc {};
    binding.binding_slot_desc.register_index = desc.shader_register;
    binding.binding_slot_desc.register_space = desc.register_space;
    binding.visibility = graphics::ShaderStageFlags::kAll;

    switch (desc.kind) {
    case bindless_d3d12::RootParamKind::DescriptorTable: {
      auto table = graphics::DescriptorTableBinding {};
      if (desc.ranges_count > 0U && desc.ranges.data() != nullptr) {
        const auto& range = desc.ranges.front();
        table.view_type = RangeTypeToViewType(
          static_cast<bindless_d3d12::RangeType>(range.range_type));
        table.base_index = range.base_register;
        table.count
          = range.num_descriptors == (std::numeric_limits<std::uint32_t>::max)()
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

auto AddBooleanDefine(const bool enabled, std::string_view name,
  std::vector<graphics::ShaderDefine>& defines) -> void
{
  if (enabled) {
    defines.push_back(
      graphics::ShaderDefine { .name = std::string(name), .value = "1" });
  }
}

auto RegisterBufferViewIndex(Graphics& gfx, graphics::Buffer& buffer,
  const graphics::BufferViewDescription& desc) -> ShaderVisibleIndex
{
  auto& registry = gfx.GetResourceRegistry();
  if (const auto existing = registry.FindShaderVisibleIndex(buffer, desc);
    existing.has_value()) {
    return *existing;
  }

  auto& allocator = gfx.GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(desc.view_type, desc.visibility);
  CHECK_F(handle.IsValid(),
    "ShadowDepthPass: failed to allocate {} view for '{}'",
    graphics::to_string(desc.view_type), buffer.GetName());
  const auto shader_visible_index = allocator.GetShaderVisibleIndex(handle);
  const auto view = registry.RegisterView(buffer, std::move(handle), desc);
  CHECK_F(view->IsValid(),
    "ShadowDepthPass: failed to register {} view for '{}'",
    graphics::to_string(desc.view_type), buffer.GetName());
  return shader_visible_index;
}

auto AdoptOrBeginPersistentState(graphics::CommandRecorder& recorder,
  const graphics::Texture& texture) -> void
{
  if (!recorder.AdoptKnownResourceState(texture)) {
    auto initial = texture.GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = graphics::ResourceStates::kDepthWrite;
    }
    recorder.BeginTrackingResourceState(texture, initial);
  }
}

auto BuildShadowPipelineDesc(const graphics::Texture& shadow_surface,
  const bool alpha_test) -> graphics::GraphicsPipelineDesc
{
  auto root_bindings = BuildVortexRootBindings();
  auto defines = std::vector<graphics::ShaderDefine> {};
  AddBooleanDefine(alpha_test, "ALPHA_TEST", defines);

  return graphics::GraphicsPipelineDesc::Builder {}
    .SetVertexShader(graphics::ShaderRequest {
      .stage = ShaderType::kVertex,
      .source_path = "Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
      .entry_point = "VortexShadowDepthVS",
      .defines = defines,
    })
    .SetPixelShader(graphics::ShaderRequest {
      .stage = ShaderType::kPixel,
      .source_path = "Vortex/Services/Shadows/DirectionalShadowDepth.hlsl",
      .entry_point = "VortexShadowDepthMaskedPS",
      .defines = defines,
    })
    .SetPrimitiveTopology(graphics::PrimitiveType::kTriangleList)
    .SetRasterizerState(graphics::RasterizerStateDesc::NoCulling())
    .SetDepthStencilState(graphics::DepthStencilStateDesc {
      .depth_test_enable = true,
      .depth_write_enable = true,
      .depth_func = graphics::CompareOp::kGreaterOrEqual,
      .stencil_enable = false,
    })
    .SetFramebufferLayout(graphics::FramebufferLayoutDesc {
      .depth_stencil_format = shadow_surface.GetDescriptor().format,
      .sample_count = shadow_surface.GetDescriptor().sample_count,
      .sample_quality = shadow_surface.GetDescriptor().sample_quality,
    })
    .SetRootBindings(std::span<const graphics::RootBindingItem>(
      root_bindings.data(), root_bindings.size()))
    .SetDebugName(alpha_test ? "Vortex.ShadowDepth.Masked"
                             : "Vortex.ShadowDepth.Opaque")
    .Build();
}

auto IsMaskedDraw(
  const PreparedSceneFrame& prepared_scene, const DrawCommand& draw_command)
  -> bool
{
  const auto metadata = prepared_scene.GetDrawMetadata();
  return draw_command.draw_index < metadata.size()
    && metadata[draw_command.draw_index].flags.IsSet(PassMaskBit::kMasked);
}

auto EnsureDepthStencilViewForCascade(Graphics& gfx,
  std::vector<graphics::NativeView>& dsvs, graphics::Texture& shadow_surface,
  const std::uint32_t cascade_index) -> graphics::NativeView
{
  if (dsvs.size() <= cascade_index) {
    dsvs.resize(cascade_index + 1U);
  }
  if (dsvs[cascade_index]->IsValid()) {
    return dsvs[cascade_index];
  }

  auto& registry = gfx.GetResourceRegistry();
  CHECK_F(registry.Contains(shadow_surface),
    "ShadowDepthPass: shadow surface '{}' must be registered before DSV "
    "lookup",
    shadow_surface.GetName());

  const auto dsv_desc = graphics::TextureViewDescription {
    .view_type = graphics::ResourceViewType::kTexture_DSV,
    .visibility = graphics::DescriptorVisibility::kCpuOnly,
    .format = shadow_surface.GetDescriptor().format,
    .dimension = TextureType::kTexture2DArray,
    .sub_resources = graphics::TextureSubResourceSet {
      .base_mip_level = 0U,
      .num_mip_levels = 1U,
      .base_array_slice = cascade_index,
      .num_array_slices = 1U,
    },
    .is_read_only_dsv = false,
  };

  if (const auto existing = registry.Find(shadow_surface, dsv_desc);
    existing->IsValid()) {
    dsvs[cascade_index] = existing;
    return existing;
  }

  auto& allocator = gfx.GetDescriptorAllocator();
  auto handle = allocator.AllocateRaw(graphics::ResourceViewType::kTexture_DSV,
    graphics::DescriptorVisibility::kCpuOnly);
  CHECK_F(handle.IsValid(),
    "ShadowDepthPass: failed to allocate a DSV for shadow cascade {}",
    cascade_index);
  const auto dsv
    = registry.RegisterView(shadow_surface, std::move(handle), dsv_desc);
  CHECK_F(dsv->IsValid(),
    "ShadowDepthPass: failed to register a DSV for shadow cascade {}",
    cascade_index);
  dsvs[cascade_index] = dsv;
  return dsv;
}

} // namespace

ShadowDepthPass::ShadowDepthPass(Renderer& renderer)
  : renderer_(renderer)
{
}

ShadowDepthPass::~ShadowDepthPass()
{
  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return;
  }

  auto& registry = gfx->GetResourceRegistry();
  if (pass_constants_buffer_ != nullptr && pass_constants_mapped_ptr_ != nullptr) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  if (pass_constants_buffer_ != nullptr && registry.Contains(*pass_constants_buffer_)) {
    registry.UnRegisterResource(*pass_constants_buffer_);
  }
}

auto ShadowDepthPass::OnFrameStart(
  const frame::SequenceNumber sequence, const frame::Slot slot) -> void
{
  current_sequence_ = sequence;
  current_slot_ = slot;
  last_render_state_ = {};
}

auto ShadowDepthPass::Record(const PreparedViewShadowInput& view_input,
  const std::shared_ptr<graphics::Texture>& shadow_surface,
  const DirectionalShadowFrameData& frame_data,
  const std::span<const DrawCommand> draw_commands) -> RenderState
{
  auto depth_slices = std::vector<DepthSlice> {};
  depth_slices.reserve(frame_data.bindings.cascade_count);
  for (std::uint32_t cascade_index = 0U;
       cascade_index < frame_data.bindings.cascade_count; ++cascade_index) {
    depth_slices.push_back(DepthSlice {
      .light_view_projection
      = frame_data.bindings.cascades[cascade_index].light_view_projection,
      .shadow_bias_parameters = glm::vec4(
        frame_data.bindings.cascades[cascade_index].sampling_metadata1.z,
        frame_data.bindings.cascades[cascade_index].sampling_metadata1.z
          * kUeCsmShadowSlopeScaleDepthBias * kUeDefaultUserShadowSlopeBias,
        kUeShadowMaxSlopeScaleDepthBias, 0.0F),
      .light_direction_to_source = frame_data.bindings.light_direction_to_source,
      .target_slice = cascade_index,
    });
  }

  return RecordSlices(view_input, shadow_surface, std::span(depth_slices),
    draw_commands);
}

auto ShadowDepthPass::RecordSlices(const PreparedViewShadowInput& view_input,
  const std::shared_ptr<graphics::Texture>& shadow_surface,
  const std::span<const DepthSlice> depth_slices,
  const std::span<const DrawCommand> draw_commands) -> RenderState
{
  last_render_state_ = {};
  if (shadow_surface == nullptr || depth_slices.empty()) {
    return last_render_state_;
  }
  if (!draw_commands.empty()
    && (view_input.prepared_scene == nullptr
      || view_input.view_constants == nullptr)) {
    return last_render_state_;
  }

  auto gfx = renderer_.GetGraphics();
  if (gfx == nullptr) {
    return last_render_state_;
  }

  if (cascade_dsv_surface_ != shadow_surface.get()) {
    cascade_dsvs_.clear();
    cascade_dsv_surface_ = shadow_surface.get();
  }

  const auto ensure_pass_constants =
    [&](const std::uint32_t required_slots) -> void {
    if (pass_constants_buffer_ != nullptr
      && pass_constants_slot_count_ >= required_slots) {
      return;
    }

    auto& registry = gfx->GetResourceRegistry();
    if (pass_constants_buffer_ != nullptr && pass_constants_mapped_ptr_ != nullptr) {
      pass_constants_buffer_->UnMap();
      pass_constants_mapped_ptr_ = nullptr;
    }
    if (pass_constants_buffer_ != nullptr && registry.Contains(*pass_constants_buffer_)) {
      registry.UnRegisterResource(*pass_constants_buffer_);
    }
    pass_constants_buffer_.reset();
    pass_constants_srvs_.clear();

    auto desc = graphics::BufferDesc {};
    desc.size_bytes
      = static_cast<std::uint64_t>(required_slots) * kShadowPassConstantsStride;
    desc.usage = graphics::BufferUsage::kConstant;
    desc.memory = graphics::BufferMemory::kUpload;
    desc.debug_name = "ShadowService.DirectionPassConstants";
    pass_constants_buffer_ = gfx->CreateBuffer(desc);
    CHECK_NOTNULL_F(pass_constants_buffer_.get(),
      "ShadowDepthPass: failed to create shadow pass constants buffer");
    registry.Register(pass_constants_buffer_);
    pass_constants_mapped_ptr_ = pass_constants_buffer_->Map(0U, desc.size_bytes);
    CHECK_NOTNULL_F(pass_constants_mapped_ptr_,
      "ShadowDepthPass: failed to map shadow pass constants buffer");

    pass_constants_srvs_.reserve(required_slots);
    for (std::uint32_t i = 0U; i < required_slots; ++i) {
      pass_constants_srvs_.push_back(RegisterBufferViewIndex(*gfx,
        *pass_constants_buffer_, graphics::BufferViewDescription {
          .view_type = graphics::ResourceViewType::kConstantBuffer,
          .visibility = graphics::DescriptorVisibility::kShaderVisible,
          .range = { static_cast<std::uint64_t>(i) * kShadowPassConstantsStride,
            kShadowPassConstantsStride },
        }));
    }
    pass_constants_slot_count_ = required_slots;
  };

  if (!draw_commands.empty()) {
    ensure_pass_constants(static_cast<std::uint32_t>(depth_slices.size()));

    auto* mapped_bytes = static_cast<std::byte*>(pass_constants_mapped_ptr_);
    for (std::uint32_t slice_index = 0U;
         slice_index < depth_slices.size(); ++slice_index) {
      const auto& depth_slice = depth_slices[slice_index];
      auto constants = ShadowPassConstants {
        .light_view_projection = depth_slice.light_view_projection,
        .shadow_bias_parameters = depth_slice.shadow_bias_parameters,
        .light_direction_to_source = depth_slice.light_direction_to_source,
        .light_position_and_inv_range
        = depth_slice.light_position_and_inv_range,
        .draw_metadata_slot
        = view_input.prepared_scene->bindless_draw_metadata_slot.get(),
        .current_worlds_slot
        = view_input.prepared_scene->bindless_worlds_slot.get(),
        .instance_data_slot
        = view_input.prepared_scene->bindless_instance_data_slot.get(),
      };
      std::memcpy(mapped_bytes + slice_index * kShadowPassConstantsStride,
        &constants, sizeof(ShadowPassConstants));
    }
  }

  const auto queue_key = gfx->QueueKeyFor(graphics::QueueRole::kGraphics);
  auto recorder = gfx->AcquireCommandRecorder(queue_key, "ShadowService ShadowDepth");
  if (!recorder) {
    return last_render_state_;
  }

  graphics::GpuEventScope stage_scope(*recorder, "Vortex.Stage8.ShadowDepths",
    profiling::ProfileGranularity::kTelemetry,
    profiling::ProfileCategory::kPass);
  AdoptOrBeginPersistentState(*recorder, *shadow_surface);
  recorder->RequireResourceState(
    *shadow_surface, graphics::ResourceStates::kDepthWrite);

  const auto root_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  const auto view_constants_param
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kViewConstants);

  for (std::uint32_t slice_index = 0U;
       slice_index < depth_slices.size(); ++slice_index) {
    const auto target_slice = depth_slices[slice_index].target_slice;
    const auto dsv = EnsureDepthStencilViewForCascade(
      *gfx, cascade_dsvs_, *shadow_surface, target_slice);
    const auto& shadow_desc = shadow_surface->GetDescriptor();
    recorder->FlushBarriers();
    recorder->SetRenderTargets({}, dsv);
    recorder->ClearDepthStencilView(*shadow_surface, dsv,
      graphics::ClearFlags::kDepth | graphics::ClearFlags::kStencil, 0.0F,
      0U);
    recorder->SetViewport({
      .top_left_x = 0.0F,
      .top_left_y = 0.0F,
      .width = static_cast<float>(shadow_desc.width),
      .height = static_cast<float>(shadow_desc.height),
      .min_depth = 0.0F,
      .max_depth = 1.0F,
    });
    recorder->SetScissors({
      .left = 0,
      .top = 0,
      .right = static_cast<std::int32_t>(shadow_desc.width),
      .bottom = static_cast<std::int32_t>(shadow_desc.height),
    });

    auto current_alpha_test = std::optional<bool> {};
    for (const auto& draw_command : draw_commands) {
      const auto alpha_test = IsMaskedDraw(*view_input.prepared_scene, draw_command);
      if (!current_alpha_test.has_value()
        || current_alpha_test.value() != alpha_test) {
        recorder->SetPipelineState(
          BuildShadowPipelineDesc(*shadow_surface, alpha_test));
        recorder->SetGraphicsRootConstantBufferView(
          view_constants_param, view_input.view_constants->GetGPUVirtualAddress());
        recorder->SetGraphicsRoot32BitConstant(
          root_constants_param, 0U, 0U);
        recorder->SetGraphicsRoot32BitConstant(
          root_constants_param, pass_constants_srvs_[slice_index].get(), 1U);
        current_alpha_test = alpha_test;
      }

      recorder->SetGraphicsRoot32BitConstant(
        root_constants_param, draw_command.draw_index, 0U);
      recorder->Draw(draw_command.index_count, draw_command.instance_count, 0U,
        draw_command.start_instance);
      ++last_render_state_.rendered_draw_count;
    }

    ++last_render_state_.rendered_cascade_count;
  }

  recorder->RequireResourceStateFinal(
    *shadow_surface, graphics::ResourceStates::kShaderResource);
  return last_render_state_;
}

} // namespace oxygen::vortex::shadows
