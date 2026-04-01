//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Passes/DepthPrePass.h>
#include <Oxygen/Renderer/RenderContext.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Renderer/Types/MaterialPermutations.h>
#include <Oxygen/Renderer/Types/PassMask.h>

using oxygen::Scissors;
using oxygen::ViewPort;
using oxygen::engine::DepthPrePass;
using oxygen::graphics::Buffer;
using oxygen::graphics::Color;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::GraphicsPipelineDesc;
using oxygen::graphics::NativeObject;
using oxygen::graphics::ResourceViewType;
using oxygen::graphics::Texture;

namespace {

struct DepthPrePassConstantsSnapshot {
  float alpha_cutoff_default { 0.5F };
  uint32_t _pad0 { 0 };
  uint32_t _pad1 { 0 };
  uint32_t _pad2 { 0 };
};
static_assert(sizeof(DepthPrePassConstantsSnapshot) == 16);

auto HasPositiveArea(const Scissors& rect) -> bool
{
  return rect.left < rect.right && rect.top < rect.bottom;
}

auto FullViewportForTexture(const Texture& texture) -> ViewPort
{
  const auto& desc = texture.GetDescriptor();
  return ViewPort {
    .top_left_x = 0.0f,
    .top_left_y = 0.0f,
    .width = static_cast<float>(desc.width),
    .height = static_cast<float>(desc.height),
    .min_depth = 0.0f,
    .max_depth = 1.0f,
  };
}

auto FullScissorsForTexture(const Texture& texture) -> Scissors
{
  const auto& desc = texture.GetDescriptor();
  return Scissors {
    .left = 0,
    .top = 0,
    .right = static_cast<int32_t>(desc.width),
    .bottom = static_cast<int32_t>(desc.height),
  };
}

auto ViewportPixelBounds(const ViewPort& viewport, const Texture& texture)
  -> Scissors
{
  const auto& desc = texture.GetDescriptor();
  const auto clamp_x
    = [width = static_cast<int32_t>(desc.width)](
        const int32_t value) { return std::clamp(value, 0, width); };
  const auto clamp_y
    = [height = static_cast<int32_t>(desc.height)](
        const int32_t value) { return std::clamp(value, 0, height); };

  return Scissors {
    .left = clamp_x(static_cast<int32_t>(std::floor(viewport.top_left_x))),
    .top = clamp_y(static_cast<int32_t>(std::floor(viewport.top_left_y))),
    .right = clamp_x(
      static_cast<int32_t>(std::ceil(viewport.top_left_x + viewport.width))),
    .bottom = clamp_y(
      static_cast<int32_t>(std::ceil(viewport.top_left_y + viewport.height))),
  };
}

auto IntersectRects(const Scissors& lhs, const Scissors& rhs) -> Scissors
{
  return Scissors {
    .left = (std::max)(lhs.left, rhs.left),
    .top = (std::max)(lhs.top, rhs.top),
    .right = (std::min)(lhs.right, rhs.right),
    .bottom = (std::min)(lhs.bottom, rhs.bottom),
  };
}

auto CanonicalDepthSrvFormat(const Texture& texture) -> oxygen::Format
{
  switch (texture.GetDescriptor().format) {
  case oxygen::Format::kDepth32:
  case oxygen::Format::kDepth24Stencil8:
  case oxygen::Format::kDepth32Stencil8:
    return oxygen::Format::kR32Float;
  case oxygen::Format::kDepth16:
    return oxygen::Format::kR16UNorm;
  default:
    return texture.GetDescriptor().format;
  }
}

} // namespace

DepthPrePass::DepthPrePass(std::shared_ptr<Config> config)
  : GraphicsRenderPass(config->debug_name)
  , config_(std::move(config))
{
}

DepthPrePass::~DepthPrePass()
{
  if (pass_constants_buffer_ && pass_constants_mapped_ptr_) {
    pass_constants_buffer_->UnMap();
    pass_constants_mapped_ptr_ = nullptr;
  }
  pass_constants_cbv_ = {};
  pass_constants_index_ = kInvalidShaderVisibleIndex;
  pass_constants_buffer_.reset();
  canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
  canonical_depth_srv_owner_ = nullptr;
  output_is_complete_ = false;
}

//! Sets the viewport for the depth pre-pass.
auto DepthPrePass::SetViewport(const ViewPort& viewport) -> void
{
  if (!viewport.IsValid()) {
    throw std::invalid_argument(
      fmt::format("viewport {} is invalid", nostd::to_string(viewport)));
  }
  DCHECK_NOTNULL_F(
    config_->depth_texture, "expecting a non-null depth texture");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  auto viewport_width = viewport.top_left_x + viewport.width;
  auto viewport_height = viewport.top_left_y + viewport.height;
  if (viewport_width > static_cast<float>(tex_desc.width)
    || viewport_height > static_cast<float>(tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "viewport dimensions ({}, {}) exceed depth_texture bounds: ({}, {})",
      viewport_width, viewport_height, tex_desc.width, tex_desc.height));
  }

  if (scissors_.has_value()) {
    const auto valid_rect = IntersectRects(
      ViewportPixelBounds(viewport, *config_->depth_texture), *scissors_);
    if (!HasPositiveArea(valid_rect)) {
      throw std::invalid_argument(
        "viewport/scissors intersection resolves to an empty depth rect");
    }
  }

  viewport_.emplace(viewport);
}

auto DepthPrePass::SetScissors(const Scissors& scissors) -> void
{
  if (!scissors.IsValid()) {
    throw std::invalid_argument(
      fmt::format("scissors {} are invalid.", nostd::to_string(scissors)));
  }
  DCHECK_NOTNULL_F(config_->depth_texture,
    "expecting depth texture to be valid when setting scissors");

  const auto& tex_desc = config_->depth_texture->GetDescriptor();

  // Assuming scissors coordinates are relative to the texture origin (0,0)
  if (scissors.left < 0 || scissors.top < 0) {
    throw std::out_of_range("scissors left and top must be non-negative.");
  }
  if (std::cmp_greater(scissors.right, tex_desc.width)
    || std::cmp_greater(scissors.bottom, tex_desc.height)) {
    throw std::out_of_range(fmt::format(
      "scissors dimensions ({}, {}) exceed depth_texture bounds ({}, {})",
      scissors.right, scissors.bottom, tex_desc.width, tex_desc.height));
  }

  if (viewport_.has_value()) {
    const auto valid_rect = IntersectRects(
      ViewportPixelBounds(*viewport_, *config_->depth_texture), scissors);
    if (!HasPositiveArea(valid_rect)) {
      throw std::invalid_argument(
        "viewport/scissors intersection resolves to an empty depth rect");
    }
  }

  scissors_.emplace(scissors);
}

auto DepthPrePass::SetClearColor(const Color& color) -> void
{
  clear_color_.emplace(color);
}

/*!
 The base implementation of this method ensures that the `depth_texture`

 (specified in `Config`) is transitioned to a state suitable for depth-stencil

 attachment (e.g., `ResourceStates::kDepthWrite`) using the provided
 `CommandRecorder`. It then flushes any pending resource barriers.

 Flushing barriers here is crucial to ensure the `depth_texture` is definitively
 in the `kDepthWrite` state before any subsequent operations by derived classes
 (e.g., clearing the texture) or later render stages.

 Backend-specific derived classes should call this base method and can then
 perform additional preparations, such as:
 - Interpreting `clear_color_` to derive depth and/or stencil clear values and
   applying them to the `depth_texture`.
 - Preparing the optional `framebuffer` if it's provided in `Config` and is
   relevant to the backend operation (e.g., for binding or coordinated
   transitions).
*/
auto DepthPrePass::DoPrepareResources(CommandRecorder& recorder) -> co::Co<>
{
  output_is_complete_ = false;
  canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
  canonical_depth_srv_owner_ = nullptr;

  // Ensure the depth_texture is in kDepthWrite state before derived classes
  // might perform operations like clears. Note that the depth_texture should
  // be already in a valid state when this method is called, but we are
  // explicitly transitioning it for safety. The transition will be optimized
  // out if the state is already correct.
  if (config_->depth_texture) {
    recorder.RequireResourceState(
      *config_->depth_texture, graphics::ResourceStates::kDepthWrite);
    recorder.FlushBarriers();
  }

  // Ensure pass-level constants are available via g_PassConstantsIndex.
  // This is a small, shader-visible CBV used for fallback values.
  if (!pass_constants_buffer_
    || pass_constants_index_ == kInvalidShaderVisibleIndex) {
    auto& graphics = Context().GetGraphics();
    auto& registry = graphics.GetResourceRegistry();
    auto& allocator = graphics.GetDescriptorAllocator();

    const graphics::BufferDesc desc {
      .size_bytes = 256u,
      .usage = graphics::BufferUsage::kConstant,
      .memory = graphics::BufferMemory::kUpload,
      .debug_name = std::string { GetName() } + "_PassConstants",
    };

    pass_constants_buffer_ = graphics.CreateBuffer(desc);
    if (!pass_constants_buffer_) {
      throw std::runtime_error(
        "DepthPrePass: Failed to create pass constants buffer");
    }
    pass_constants_buffer_->SetName(desc.debug_name);

    pass_constants_mapped_ptr_
      = pass_constants_buffer_->Map(0, desc.size_bytes);
    if (!pass_constants_mapped_ptr_) {
      throw std::runtime_error(
        "DepthPrePass: Failed to map pass constants buffer");
    }
    const DepthPrePassConstantsSnapshot snapshot {};
    std::memcpy(pass_constants_mapped_ptr_, &snapshot, sizeof(snapshot));

    graphics::BufferViewDescription cbv_view_desc;
    cbv_view_desc.view_type = ResourceViewType::kConstantBuffer;
    cbv_view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    cbv_view_desc.range = { 0u, desc.size_bytes };

    auto cbv_handle = allocator.Allocate(ResourceViewType::kConstantBuffer,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!cbv_handle.IsValid()) {
      throw std::runtime_error(
        "DepthPrePass: Failed to allocate CBV descriptor handle");
    }
    pass_constants_index_ = allocator.GetShaderVisibleIndex(cbv_handle);

    registry.Register(pass_constants_buffer_);
    pass_constants_cbv_ = registry.RegisterView(
      *pass_constants_buffer_, std::move(cbv_handle), cbv_view_desc);
    if (!pass_constants_cbv_->IsValid()) {
      throw std::runtime_error(
        "DepthPrePass: Failed to register pass constants CBV");
    }
  }

  SetPassConstantsIndex(pass_constants_index_);

  if (const auto* resolved_view = Context().current_view.resolved_view.get();
    resolved_view != nullptr) {
    output_ndc_depth_range_ = resolved_view->DepthRange();
    output_reverse_z_ = resolved_view->ReverseZ();
  } else {
    output_ndc_depth_range_ = oxygen::NdcDepthRange::ZeroToOne;
    output_reverse_z_ = true;
  }

  if (PublishesCanonicalDepthOutput()) {
    const auto canonical_depth_srv = EnsureCanonicalDepthSrv();
    if (!canonical_depth_srv.IsValid()) {
      throw std::runtime_error(
        "DepthPrePass: Failed to publish canonical depth SRV");
    }
    output_is_complete_ = config_->depth_texture != nullptr
      && canonical_depth_srv_index_.IsValid();
  } else {
    canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
    canonical_depth_srv_owner_ = nullptr;
    output_is_complete_ = false;
  }

  co_return;
}

auto DepthPrePass::ValidateConfig() -> void
{
  // Will throw if no valid color texture is found.
  [[maybe_unused]] const auto& _ = GetDepthTexture();
}

auto DepthPrePass::NeedRebuildPipelineState() const -> bool
{
  const auto& last_built = LastBuiltPsoDesc();
  if (!last_built) {
    return true;
  }

  // If pipeline state exists, check if depth texture properties have changed
  if (last_built->FramebufferLayout().depth_stencil_format
    != GetDepthTexture().GetDescriptor().format) {
    return true;
  }
  if (last_built->FramebufferLayout().sample_count
    != GetDepthTexture().GetDescriptor().sample_count) {
    return true;
  }
  // Rebuild if requested rasterizer fill-mode differs from last-built PSO
  // Depth pre-pass must write filled triangles for correct depth buffer
  // population; ignore any 'wireframe' hints for the depth pass.
  // Depth pre-pass uses a fixed solid rasterizer configuration; do not
  // trigger rebuilds based on fill-mode differences in user config.
  return false; // No need to rebuild
}

auto DepthPrePass::UsesFramebufferDepthAttachment() const -> bool
{
  return true;
}

auto DepthPrePass::PublishesCanonicalDepthOutput() const -> bool
{
  return true;
}

auto DepthPrePass::BuildRasterizerStateDesc(
  const graphics::CullMode cull_mode) const -> graphics::RasterizerStateDesc
{
  return graphics::RasterizerStateDesc {
    .fill_mode = graphics::FillMode::kSolid,
    .cull_mode = cull_mode,
    .front_counter_clockwise = true,
    .multisample_enable = false,
  };
}

auto DepthPrePass::GetDepthTexture() const -> const Texture&
{
  if (const auto* depth_texture = TryGetConfiguredDepthTexture();
    depth_texture != nullptr) {
    return *depth_texture;
  }

  throw std::runtime_error(
    "DepthPrePass requires an explicit config depth_texture.");
}

auto DepthPrePass::TryGetConfiguredDepthTexture() const noexcept
  -> const graphics::Texture*
{
  return config_ && config_->depth_texture ? config_->depth_texture.get()
                                           : nullptr;
}

auto DepthPrePass::SetConfiguredDepthTexture(
  std::shared_ptr<const graphics::Texture> depth_texture) -> void
{
  if (!config_) {
    throw std::runtime_error(
      "DepthPrePass requires a valid config before updating depth_texture.");
  }
  config_->depth_texture = std::move(depth_texture);
}

auto DepthPrePass::GetDepthTextureMutable() -> graphics::Texture&
{
  // ResourceRegistry view registration still requires a non-const Resource&.
  // Keep that const-escape localized here instead of repeating const_cast at
  // each call site that needs to register or update depth views.
  if (const auto* depth_texture = TryGetConfiguredDepthTexture();
    depth_texture != nullptr) {
    return const_cast<graphics::Texture&>(*depth_texture);
  }

  throw std::runtime_error(
    "DepthPrePass requires an explicit config depth_texture.");
}

auto DepthPrePass::GetEffectiveViewport() const -> ViewPort
{
  return viewport_.value_or(FullViewportForTexture(GetDepthTexture()));
}

auto DepthPrePass::GetEffectiveScissors() const -> Scissors
{
  return scissors_.value_or(FullScissorsForTexture(GetDepthTexture()));
}

auto DepthPrePass::GetEffectiveDepthRect() const -> Scissors
{
  const auto valid_rect = IntersectRects(
    ViewportPixelBounds(GetEffectiveViewport(), GetDepthTexture()),
    GetEffectiveScissors());
  DCHECK_F(HasPositiveArea(valid_rect),
    "DepthPrePass effective depth rect must have positive area");
  return valid_rect;
}

auto DepthPrePass::GetOutput() const -> DepthPrePassOutput
{
  if (!PublishesCanonicalDepthOutput()) {
    return {};
  }

  const auto* depth_texture = config_ && config_->depth_texture
    ? config_->depth_texture.get()
    : nullptr;
  const auto width
    = depth_texture != nullptr ? depth_texture->GetDescriptor().width : 0U;
  const auto height
    = depth_texture != nullptr ? depth_texture->GetDescriptor().height : 0U;
  return DepthPrePassOutput {
    .depth_texture = depth_texture,
    .canonical_srv_index = canonical_depth_srv_index_,
    .width = width,
    .height = height,
    .viewport = GetEffectiveViewport(),
    .scissors = GetEffectiveScissors(),
    .valid_rect = GetEffectiveDepthRect(),
    .ndc_depth_range = output_ndc_depth_range_,
    .reverse_z = output_reverse_z_,
    .has_depth_texture = depth_texture != nullptr,
    .has_canonical_srv = canonical_depth_srv_index_.IsValid(),
    .is_complete = output_is_complete_,
  };
}

auto DepthPrePass::EnsureCanonicalDepthSrv() -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(config_->depth_texture.get(),
    "DepthPrePass requires a valid depth texture before publishing output");

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();
  auto& depth_texture = GetDepthTextureMutable();
  const auto srv_desc = graphics::TextureViewDescription {
    .view_type = ResourceViewType::kTexture_SRV,
    .visibility = graphics::DescriptorVisibility::kShaderVisible,
    .format = CanonicalDepthSrvFormat(depth_texture),
    .dimension = depth_texture.GetDescriptor().texture_type,
    .sub_resources = graphics::TextureSubResourceSet::EntireTexture(),
    .is_read_only_dsv = false,
  };

  if (const auto existing_index
    = registry.FindShaderVisibleIndex(depth_texture, srv_desc);
    existing_index.has_value()) {
    canonical_depth_srv_index_ = *existing_index;
    canonical_depth_srv_owner_ = &depth_texture;
    return canonical_depth_srv_index_;
  }

  if (canonical_depth_srv_index_.IsValid()
    && canonical_depth_srv_owner_ == &depth_texture
    && registry.Contains(depth_texture, srv_desc)) {
    return canonical_depth_srv_index_;
  }

  auto register_new_view = [&]() -> ShaderVisibleIndex {
    auto srv_handle = allocator.Allocate(ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
    if (!srv_handle.IsValid()) {
      canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
      canonical_depth_srv_owner_ = nullptr;
      return canonical_depth_srv_index_;
    }

    canonical_depth_srv_index_ = allocator.GetShaderVisibleIndex(srv_handle);
    const auto view
      = registry.RegisterView(depth_texture, std::move(srv_handle), srv_desc);
    if (!view->IsValid()) {
      canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
      canonical_depth_srv_owner_ = nullptr;
      return canonical_depth_srv_index_;
    }

    canonical_depth_srv_owner_ = &depth_texture;
    return canonical_depth_srv_index_;
  };

  if (!canonical_depth_srv_index_.IsValid()) {
    return register_new_view();
  }

  const auto updated = registry.UpdateView(depth_texture,
    bindless::HeapIndex { canonical_depth_srv_index_.get() }, srv_desc);
  if (!updated) {
    canonical_depth_srv_index_ = kInvalidShaderVisibleIndex;
    canonical_depth_srv_owner_ = nullptr;
    return register_new_view();
  }

  canonical_depth_srv_owner_ = &depth_texture;
  return canonical_depth_srv_index_;
}

/*!
 For a DepthPrePass, this involves rendering the geometry from the `draw_list`
 (specified in `Config`) to populate the `depth_texture`. Key responsibilities
 include:
 - Setting up a pipeline state configured for depth-only rendering (no color
   writes).
 - Applying the `viewport_` and `scissors_` if they have been set.
 - Issuing draw calls for the specified geometry.
*/

auto DepthPrePass::DoExecute(CommandRecorder& recorder) -> co::Co<>
{
  LOG_SCOPE_FUNCTION(2);

  if (auto psf = Context().current_view.prepared_frame; psf && psf->IsValid()) {
    DLOG_F(3,
      "DepthPrePass: PreparedSceneFrame matrices: world_floats={} "
      "normal_floats={}",
      psf->world_matrices.size(), psf->normal_matrices.size());
  }

  auto& depth_texture = GetDepthTextureMutable();
  const auto dsv = PrepareDepthStencilView(depth_texture);
  DCHECK_F(dsv->IsValid(), "DepthStencilView must be valid after preparation");

  SetupViewPortAndScissors(recorder);
  const graphics::GpuEventScopeOptions scope_options {};
  graphics::GpuEventScope scene_depth_work_scope(
    recorder, "DepthPrePass.SceneDepthWork", scope_options);
  ClearDepthStencilView(recorder, dsv);
  SetupRenderTargets(recorder, dsv);

  // NOTE: Transparent draws are intentionally excluded from the depth pre-pass
  // to prevent them from writing depth and later occluding opaque color when
  // blended (would produce the previously observed inverted transparency).
  // Depth-writing geometry is split into two explicit buckets:
  // - Opaque  : VS-only depth path (no alpha test).
  // - Masked  : Alpha-tested depth path (PS clip).

  const auto psf = Context().current_view.prepared_frame;
  if (!psf || !psf->IsValid() || psf->draw_metadata_bytes.empty()) {
    Context().RegisterPass(this);
    co_return;
  }
  if (psf->partitions.empty()) {
    // Partitions are required for correct PSO selection; without them we would
    // not know whether to use the opaque or masked shader path.
    LOG_F(ERROR, "DepthPrePass: partitions are missing; nothing will be drawn");
    Context().RegisterPass(this);
    co_return;
  }

  DCHECK_F(pso_opaque_single_.has_value());
  DCHECK_F(pso_opaque_double_.has_value());
  DCHECK_F(pso_masked_single_.has_value());
  DCHECK_F(pso_masked_double_.has_value());

  const auto* records = reinterpret_cast<const engine::DrawMetadata*>(
    psf->draw_metadata_bytes.data());

  uint32_t emitted_count = 0;
  uint32_t skipped_invalid = 0;
  uint32_t draw_errors = 0;

  for (const auto& pr : psf->partitions) {
    if (!pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kMainViewVisible)) {
      continue;
    }
    if (!pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kOpaque)
      && !pr.pass_mask.IsSet(oxygen::engine::PassMaskBit::kMasked)) {
      continue;
    }

    const auto& pso_desc = SelectPipelineStateForPartition(pr.pass_mask);
    recorder.SetPipelineState(pso_desc);

    EmitDrawRange(recorder, records, pr.begin, pr.end, emitted_count,
      skipped_invalid, draw_errors);
  }

  if (emitted_count > 0 || skipped_invalid > 0 || draw_errors > 0) {
    DLOG_F(2,
      "DepthPrePass: emitted={}, skipped_invalid={}, errors={} "
      "(partition-aware)",
      emitted_count, skipped_invalid, draw_errors);
  }

  Context().RegisterPass(this);
  co_return;
}

// --- Private helper implementations for Execute() ---

auto DepthPrePass::PrepareDepthStencilView(Texture& depth_texture_ref)
  -> graphics::NativeView
{
  using graphics::DescriptorHandle;
  using graphics::DescriptorVisibility;
  using graphics::TextureViewDescription;

  auto& graphics = Context().GetGraphics();
  auto& registry = graphics.GetResourceRegistry();
  auto& allocator = graphics.GetDescriptorAllocator();

  // 1. Prepare TextureViewDescription
  const auto& depth_tex_desc = depth_texture_ref.GetDescriptor();
  const TextureViewDescription dsv_view_desc {
    .view_type = ResourceViewType::kTexture_DSV,
    .visibility = DescriptorVisibility::kCpuOnly,
    .format = depth_tex_desc.format,
    .dimension = depth_tex_desc.texture_type,
    .sub_resources = {
        .base_mip_level = 0,
        .num_mip_levels = depth_tex_desc.mip_levels,
        .base_array_slice = 0,
        .num_array_slices = (depth_tex_desc.texture_type == TextureType::kTexture3D
                ? depth_tex_desc.depth
                : depth_tex_desc.array_size),
    },
    .is_read_only_dsv = false, // Default for a writable DSV
  };

  // 2. Check with ResourceRegistry::FindView
  if (const auto dsv = registry.Find(depth_texture_ref, dsv_view_desc);
    dsv->IsValid()) {
    return dsv;
  }
  // View not found (cache miss), create and register it
  DescriptorHandle dsv_desc_handle = allocator.Allocate(
    ResourceViewType::kTexture_DSV, DescriptorVisibility::kCpuOnly);

  if (!dsv_desc_handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate DSV descriptor handle for depth texture");
  }
  // Register the newly created view
  const auto dsv = registry.RegisterView(
    depth_texture_ref, std::move(dsv_desc_handle), dsv_view_desc);

  if (!dsv->IsValid()) {
    throw std::runtime_error("Failed to register DSV with resource registry "
                             "even after successful allocation.");
  }

  return dsv;
}

auto DepthPrePass::ClearDepthStencilView(CommandRecorder& command_recorder,
  const graphics::NativeView& dsv_handle) const -> void
{
  const auto full_rect = FullScissorsForTexture(GetDepthTexture());
  const auto valid_rect = GetEffectiveDepthRect();
  if (valid_rect.left == full_rect.left && valid_rect.top == full_rect.top
    && valid_rect.right == full_rect.right
    && valid_rect.bottom == full_rect.bottom) {
    command_recorder.ClearDepthStencilView(
      GetDepthTexture(), dsv_handle, graphics::ClearFlags::kDepth, 0.0f, 0);
    return;
  }

  const std::array clear_rects { valid_rect };
  command_recorder.ClearDepthStencilView(GetDepthTexture(), dsv_handle,
    graphics::ClearFlags::kDepth, 0.0f, 0, clear_rects);
}

auto DepthPrePass::SetupRenderTargets(CommandRecorder& command_recorder,
  const graphics::NativeView& dsv) const -> void
{
  DCHECK_F(dsv->IsValid(),
    "DepthStencilView must be valid before setting render targets");

  command_recorder.SetRenderTargets({}, dsv);
}

auto DepthPrePass::SetupViewPortAndScissors(
  CommandRecorder& command_recorder) const -> void
{
  command_recorder.SetViewport(GetEffectiveViewport());
  command_recorder.SetScissors(GetEffectiveScissors());
}

auto DepthPrePass::SelectPipelineStateForPartition(
  const PassMask& pass_mask) const -> const graphics::GraphicsPipelineDesc&
{
  const bool is_masked = pass_mask.IsSet(oxygen::engine::PassMaskBit::kMasked);
  const bool is_double_sided
    = pass_mask.IsSet(oxygen::engine::PassMaskBit::kDoubleSided);

  DCHECK_F(pso_opaque_single_.has_value());
  DCHECK_F(pso_opaque_double_.has_value());
  DCHECK_F(pso_masked_single_.has_value());
  DCHECK_F(pso_masked_double_.has_value());

  if (is_masked) {
    return is_double_sided ? *pso_masked_double_ : *pso_masked_single_;
  }
  return is_double_sided ? *pso_opaque_double_ : *pso_opaque_single_;
}

auto DepthPrePass::CreatePipelineStateDesc() -> GraphicsPipelineDesc
{
  using graphics::BindingSlotDesc;
  using graphics::CompareOp;
  using graphics::CullMode;
  using graphics::DepthStencilStateDesc;
  using graphics::DescriptorTableBinding;
  using graphics::DirectBufferBinding;
  using graphics::FramebufferLayoutDesc;
  using graphics::PrimitiveType;
  using graphics::PushConstantsBinding;
  using graphics::RasterizerStateDesc;
  using graphics::RootBindingDesc;
  using graphics::RootBindingItem;
  using graphics::ShaderRequest;
  using graphics::ShaderStageFlags;

  constexpr DepthStencilStateDesc ds_desc {
    .depth_test_enable = true, // Enable depth testing
    .depth_write_enable = true, // Enable writing to depth buffer
    .depth_func = CompareOp::kGreaterOrEqual, // Reversed-Z depth comparison
    .stencil_enable = false, // Stencil testing usually disabled unless required
    .stencil_read_mask = 0xFF, // full-mask for reading stencil buffer
    .stencil_write_mask = 0xFF, // full-mask for writing to stencil buffer
  };

  auto& depth_texture_desc = GetDepthTexture().GetDescriptor();
  const FramebufferLayoutDesc fb_layout_desc { .color_target_formats = {},
    .depth_stencil_format = depth_texture_desc.format,
    .sample_count = depth_texture_desc.sample_count };

  // Build root bindings from generated table
  auto generated_bindings = BuildRootBindings();

  // Depth pre-pass uses shader defines (e.g., ALPHA_TEST) to differentiate
  // between opaque and masked paths. The same entry points (VS, PS) compile
  // into different variants based on active defines.
  using graphics::ShaderDefine;

  const auto BuildDesc
    = [&](CullMode cull_mode,
        std::vector<ShaderDefine> defines) -> GraphicsPipelineDesc {
    ExtendShaderDefines(defines);
    return GraphicsPipelineDesc::Builder()
      .SetVertexShader(ShaderRequest {
        .stage = ShaderType::kVertex,
        .source_path = "Depth/DepthPrePass.hlsl",
        .entry_point = "VS",
        .defines = defines,
      })
      .SetPixelShader(ShaderRequest {
        .stage = ShaderType::kPixel,
        .source_path = "Depth/DepthPrePass.hlsl",
        .entry_point = "PS",
        .defines = defines,
      })
      .SetPrimitiveTopology(PrimitiveType::kTriangleList)
      .SetRasterizerState(BuildRasterizerStateDesc(cull_mode))
      .SetDepthStencilState(ds_desc)
      .SetBlendState({})
      .SetFramebufferLayout(fb_layout_desc)
      .SetRootBindings(std::span<const RootBindingItem>(
        generated_bindings.data(), generated_bindings.size()))
      .Build();
  };

  // Partition-aware variants using shader defines.
  // ALPHA_TEST define enables alpha-tested (masked) path in pixel shader.
  pso_opaque_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kOpaqueDefines));
  pso_opaque_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kOpaqueDefines));
  pso_masked_single_
    = BuildDesc(CullMode::kBack, ToDefines(permutation::kMaskedDefines));
  pso_masked_double_
    = BuildDesc(CullMode::kNone, ToDefines(permutation::kMaskedDefines));

  // The base class needs a single descriptor to cache and bind initially.
  // Use the most common variant as the default.
  return *pso_opaque_single_;
}

auto DepthPrePass::ExtendShaderDefines(
  std::vector<graphics::ShaderDefine>& /*defines*/) const -> void
{
}
