//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/SubmissionCallback.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Profiling/GpuEventScope.h>
#include <Oxygen/Profiling/ProfileScope.h>
#include <Oxygen/Vortex/Internal/TextureViews.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/ScreenHzbModule.h>
#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>

namespace oxygen::vortex {

namespace {

  template <typename Resource>
  auto RegisterResourceIfNeeded(
    Graphics& gfx, const std::shared_ptr<Resource>& resource) -> void
  {
    if (!resource) {
      return;
    }
    auto& registry = gfx.GetResourceRegistry();
    if (!registry.Contains(*resource)) {
      registry.Register(resource);
    }
  }

  //! Retires `texture` once the GPU no longer uses it.
  auto RetireTexture(Graphics& gfx, std::shared_ptr<graphics::Texture>& texture)
    -> void
  {
    if (!texture) {
      return;
    }
    auto* registry = &gfx.GetResourceRegistry();
    gfx.GetDeferredReclaimer().RegisterDeferredAction(
      [registry, texture = std::move(texture)] mutable -> void {
        if (registry->Contains(*texture)) {
          registry->UnRegisterResource(*texture);
        }
        texture.reset();
      });
  }

  //! Creates a registered pyramid texture and its whole-chain SRV.
  auto CreatePyramid(Graphics& gfx, const graphics::TextureDesc& desc,
    std::shared_ptr<graphics::Texture>& texture, ShaderVisibleIndex& srv)
    -> void
  {
    texture = gfx.CreateTexture(desc);
    CHECK_NOTNULL_F(
      texture.get(), "Failed to create HZB texture '{}'", desc.debug_name);
    RegisterResourceIfNeeded(gfx, texture);
    srv = internal::EnsureTextureView(
      gfx, *texture, internal::WholeTextureSrvDesc(*texture));
    CHECK_F(
      srv.IsValid(), "HZB texture '{}' SRV must be valid", desc.debug_name);
  }

  //! A view rect inside a scene texture, in texels.
  struct ScreenViewRect {
    std::uint32_t buffer_width { 0U };
    std::uint32_t buffer_height { 0U };
    std::uint32_t min_x { 0U };
    std::uint32_t min_y { 0U };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
  };

  //! Scale and bias from the view's clip-space position to scene-texture UV.
  struct ScreenPositionScaleBias {
    float scale_x { 0.0F };
    float scale_y { 0.0F };
    float bias_y { 0.0F };
    float bias_x { 0.0F };
  };

  [[nodiscard]] auto ComputeScreenPositionScaleBias(const ScreenViewRect& rect)
    -> ScreenPositionScaleBias
  {
    CHECK_F(rect.buffer_width != 0U && rect.buffer_height != 0U,
      "Screen HZB requires non-zero scene-texture extent");
    const auto inv_buffer_width = 1.0F / static_cast<float>(rect.buffer_width);
    const auto inv_buffer_height
      = 1.0F / static_cast<float>(rect.buffer_height);
    return {
      .scale_x = static_cast<float>(rect.width) * inv_buffer_width / 2.0F,
      .scale_y = static_cast<float>(rect.height) * inv_buffer_height / -2.0F,
      .bias_y = ((static_cast<float>(rect.height) / 2.0F)
                  + static_cast<float>(rect.min_y))
        * inv_buffer_height,
      .bias_x = ((static_cast<float>(rect.width) / 2.0F)
                  + static_cast<float>(rect.min_x))
        * inv_buffer_width,
    };
  }

} // namespace

struct ScreenHzbModule::Impl {
  struct PyramidResources {
    bool enabled { false };
    std::array<std::shared_ptr<graphics::Texture>, 2> history_textures {};
    std::array<ShaderVisibleIndex, 2> history_srv_indices {
      kInvalidShaderVisibleIndex,
      kInvalidShaderVisibleIndex,
    };
  };

  struct OcclusionResources {
    std::shared_ptr<graphics::Texture> texture;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  };

  struct ViewState {
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_count { 0U };
    std::uint32_t scene_texture_width { 0U };
    std::uint32_t scene_texture_height { 0U };
    std::uint32_t source_view_rect_min_x { 0U };
    std::uint32_t source_view_rect_min_y { 0U };
    std::uint32_t source_view_rect_width { 0U };
    std::uint32_t source_view_rect_height { 0U };
    PyramidResources closest {};
    PyramidResources furthest {};
    OcclusionResources occlusion {};
    std::uint32_t current_history_slot { 0U };
    bool has_current_output { false };
    bool has_previous_output { false };
  };

  explicit Impl(Renderer& renderer_in)
    : renderer(renderer_in)
    , builder(renderer_in, "Vortex.Stage5.ScreenHzbBuild")
  {
  }

  ~Impl()
  {
    auto gfx = renderer.GetGraphics();
    if (gfx == nullptr) {
      return;
    }

    for (auto& [view_id, state] : view_states) {
      std::ignore = view_id;
      ReleaseViewResources(*gfx, state);
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  static auto ReleaseHistoryResources(Graphics& gfx, ViewState& state) -> void
  {
    const auto release_pyramid = [&](PyramidResources& pyramid) -> void {
      for (auto& texture : pyramid.history_textures) {
        RetireTexture(gfx, texture);
      }
      pyramid.enabled = false;
      pyramid.history_srv_indices.fill(kInvalidShaderVisibleIndex);
    };

    release_pyramid(state.closest);
    release_pyramid(state.furthest);
    state.current_history_slot = 0U;
    state.has_current_output = false;
    state.has_previous_output = false;
    state.width = 0U;
    state.height = 0U;
    state.mip_count = 0U;
    state.scene_texture_width = 0U;
    state.scene_texture_height = 0U;
    state.source_view_rect_min_x = 0U;
    state.source_view_rect_min_y = 0U;
    state.source_view_rect_width = 0U;
    state.source_view_rect_height = 0U;
  }

  static auto ReleaseViewResources(Graphics& gfx, ViewState& state) -> void
  {
    ReleaseHistoryResources(gfx, state);
    RetireTexture(gfx, state.occlusion.texture);
    state.occlusion.srv = kInvalidShaderVisibleIndex;
  }

  auto EnsureHistoryResources(const ViewId view_id,
    const HzbPyramidBuilder::Source& source, const bool build_closest,
    const bool build_furthest) -> ViewState&
  {
    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& state = view_states[view_id];
    const auto desc
      = HzbPyramidBuilder::MakeTextureDesc(source.width, source.height, {});

    const auto pyramid_missing_resource
      = [](const PyramidResources& pyramid) -> bool {
      return pyramid.enabled
        && (pyramid.history_textures.at(0) == nullptr
          || pyramid.history_textures.at(1) == nullptr);
    };
    const bool needs_recreate = pyramid_missing_resource(state.closest)
      || pyramid_missing_resource(state.furthest)
      || state.closest.enabled != build_closest
      || state.furthest.enabled != build_furthest || state.width != desc.width
      || state.height != desc.height || state.mip_count != desc.mip_levels;
    if (!needs_recreate) {
      return state;
    }

    ReleaseHistoryResources(*gfx, state);
    const auto base_name
      = "Vortex.Stage5.ScreenHzbBuild.View" + std::to_string(view_id.get());
    const auto create_pyramid
      = [&](PyramidResources& pyramid, const char* semantic_label) -> void {
      pyramid.enabled = true;
      for (std::uint32_t slot = 0U; slot < 2U; ++slot) {
        auto history_desc = desc;
        history_desc.debug_name = base_name + "." + semantic_label + ".History"
          + std::to_string(slot);
        CreatePyramid(*gfx, history_desc, pyramid.history_textures.at(slot),
          pyramid.history_srv_indices.at(slot));
      }
    };

    if (build_closest) {
      create_pyramid(state.closest, "Closest");
    }
    if (build_furthest) {
      create_pyramid(state.furthest, "Furthest");
    }
    state.width = desc.width;
    state.height = desc.height;
    state.mip_count = desc.mip_levels;
    state.current_history_slot = 0U;
    state.has_current_output = false;
    state.has_previous_output = false;
    return state;
  }

  auto EnsureOcclusionResources(const ViewId view_id,
    const HzbPyramidBuilder::Source& source) -> OcclusionResources&
  {
    auto gfx = renderer.GetGraphics();
    CHECK_NOTNULL_F(gfx.get(), "Screen HZB requires Graphics");
    auto& occlusion = view_states[view_id].occlusion;
    const auto desc
      = HzbPyramidBuilder::MakeTextureDesc(source.width, source.height,
        "Vortex.Occlusion.Pyramid.View" + std::to_string(view_id.get()));
    if (occlusion.texture != nullptr) {
      const auto& current = occlusion.texture->GetDescriptor();
      if (current.width == desc.width && current.height == desc.height
        && current.mip_levels == desc.mip_levels) {
        return occlusion;
      }
      RetireTexture(*gfx, occlusion.texture);
    }
    CreatePyramid(*gfx, desc, occlusion.texture, occlusion.srv);
    return occlusion;
  }

  [[nodiscard]] static auto BuildOutput(const ViewState& state,
    const std::uint32_t slot, const bool available) -> ScreenHzbModule::Output
  {
    if (!available) {
      return {};
    }

    const auto closest_valid = state.closest.enabled
      && state.closest.history_textures.at(slot) != nullptr
      && state.closest.history_srv_indices.at(slot).IsValid();
    const auto furthest_valid = state.furthest.enabled
      && state.furthest.history_textures.at(slot) != nullptr
      && state.furthest.history_srv_indices.at(slot).IsValid();
    const auto requested_valid = (state.closest.enabled ? closest_valid : true)
      && (state.furthest.enabled ? furthest_valid : true)
      && (closest_valid || furthest_valid);
    if (!requested_valid) {
      return {};
    }

    const auto hzb_uv_factor_x
      = static_cast<float>(state.source_view_rect_width)
      / (2.0F * static_cast<float>(state.width));
    const auto hzb_uv_factor_y
      = static_cast<float>(state.source_view_rect_height)
      / (2.0F * static_cast<float>(state.height));
    const auto hzb_uv_inv_factor_x
      = hzb_uv_factor_x > 0.0F ? 1.0F / hzb_uv_factor_x : 0.0F;
    const auto hzb_uv_inv_factor_y
      = hzb_uv_factor_y > 0.0F ? 1.0F / hzb_uv_factor_y : 0.0F;
    const auto screen = ComputeScreenPositionScaleBias({
      .buffer_width = state.scene_texture_width,
      .buffer_height = state.scene_texture_height,
      .min_x = state.source_view_rect_min_x,
      .min_y = state.source_view_rect_min_y,
      .width = state.source_view_rect_width,
      .height = state.source_view_rect_height,
    });

    auto bindings = ScreenHzbFrameBindings {
      .closest_srv = closest_valid ? state.closest.history_srv_indices.at(slot)
                                   : kInvalidShaderVisibleIndex,
      .furthest_srv = furthest_valid
        ? state.furthest.history_srv_indices.at(slot)
        : kInvalidShaderVisibleIndex,
      .width = state.width,
      .height = state.height,
      .mip_count = state.mip_count,
      .flags = kScreenHzbFrameBindingsFlagAvailable
        | (furthest_valid ? kScreenHzbFrameBindingsFlagFurthestValid : 0U)
        | (closest_valid ? kScreenHzbFrameBindingsFlagClosestValid : 0U),
      .hzb_size_x = static_cast<float>(state.width),
      .hzb_size_y = static_cast<float>(state.height),
      .hzb_view_size_x = static_cast<float>(state.source_view_rect_width),
      .hzb_view_size_y = static_cast<float>(state.source_view_rect_height),
      .hzb_view_rect_min_x = 0,
      .hzb_view_rect_min_y = 0,
      .hzb_view_rect_width
      = static_cast<std::int32_t>(state.source_view_rect_width),
      .hzb_view_rect_height
      = static_cast<std::int32_t>(state.source_view_rect_height),
      .viewport_uv_to_hzb_buffer_uv_x = hzb_uv_factor_x,
      .viewport_uv_to_hzb_buffer_uv_y = hzb_uv_factor_y,
      .hzb_uv_factor_x = hzb_uv_factor_x,
      .hzb_uv_factor_y = hzb_uv_factor_y,
      .hzb_uv_inv_factor_x = hzb_uv_inv_factor_x,
      .hzb_uv_inv_factor_y = hzb_uv_inv_factor_y,
      .hzb_uv_to_screen_uv_scale_x
      = hzb_uv_inv_factor_x * 2.0F * screen.scale_x,
      .hzb_uv_to_screen_uv_scale_y
      = hzb_uv_inv_factor_y * -2.0F * screen.scale_y,
      .hzb_uv_to_screen_uv_bias_x = -screen.scale_x + screen.bias_x,
      .hzb_uv_to_screen_uv_bias_y = screen.scale_y + screen.bias_y,
      .hzb_base_texel_size_x = 1.0F / static_cast<float>(state.width),
      .hzb_base_texel_size_y = 1.0F / static_cast<float>(state.height),
      .sample_pixel_to_hzb_uv_x = 0.5F / static_cast<float>(state.width),
      .sample_pixel_to_hzb_uv_y = 0.5F / static_cast<float>(state.height),
    };

    return ScreenHzbModule::Output {
      .closest_texture
      = closest_valid ? state.closest.history_textures.at(slot) : nullptr,
      .furthest_texture
      = furthest_valid ? state.furthest.history_textures.at(slot) : nullptr,
      .bindings = bindings,
      .available = true,
    };
  }

  Renderer& renderer;
  HzbPyramidBuilder builder;
  std::unordered_map<ViewId, ViewState> view_states;
};

ScreenHzbModule::ScreenHzbModule(
  Renderer& renderer, const SceneTexturesConfig& scene_textures_config)
  : impl_(std::make_unique<Impl>(renderer))
{
  std::ignore = scene_textures_config;
}

ScreenHzbModule::~ScreenHzbModule() = default;

void ScreenHzbModule::OnFrameStart()
{
  current_output_ = {};
  previous_output_ = {};
  output_view_id_ = kInvalidViewId;
}

void ScreenHzbModule::RemoveViewState(const ViewId view_id)
{
  const auto found = impl_->view_states.find(view_id);
  if (found != impl_->view_states.end()) {
    if (auto gfx = impl_->renderer.GetGraphics()) {
      Impl::ReleaseViewResources(*gfx, found->second);
    }
    impl_->view_states.erase(found);
  }
  if (output_view_id_ == view_id) {
    current_output_ = {};
    previous_output_ = {};
    output_view_id_ = kInvalidViewId;
  }
}

auto ScreenHzbModule::ResolveViewDepthSource(const RenderContext& ctx,
  const graphics::Texture& scene_depth) -> HzbPyramidBuilder::Source
{
  const auto scene_depth_width = scene_depth.GetDescriptor().width;
  const auto scene_depth_height = scene_depth.GetDescriptor().height;
  auto source = HzbPyramidBuilder::Source {
    .depth = observer_ptr { &scene_depth },
    .width = scene_depth_width,
    .height = scene_depth_height,
  };
  if (const auto* resolved_view = ctx.current_view.resolved_view.get();
    resolved_view != nullptr && resolved_view->Viewport().IsValid()) {
    const auto viewport = resolved_view->Viewport();
    source.origin_x = (std::min)(scene_depth_width - 1U,
      static_cast<std::uint32_t>(
        std::floor((std::max)(viewport.top_left_x, 0.0F))));
    source.origin_y = (std::min)(scene_depth_height - 1U,
      static_cast<std::uint32_t>(
        std::floor((std::max)(viewport.top_left_y, 0.0F))));
    source.width = (std::min)(scene_depth_width - source.origin_x,
      (std::max)(1U, static_cast<std::uint32_t>(std::ceil(viewport.width))));
    source.height = (std::min)(scene_depth_height - source.origin_y,
      (std::max)(1U, static_cast<std::uint32_t>(std::ceil(viewport.height))));
  }
  return source;
}

auto ScreenHzbModule::BuildOcclusionPyramid(RenderContext& ctx,
  graphics::CommandRecorder& recorder, const HzbPyramidBuilder::Source& source)
  -> std::optional<OcclusionPyramid>
{
  const auto view_id = ctx.current_view.view_id;
  if (view_id == kInvalidViewId || impl_->renderer.GetGraphics() == nullptr) {
    return std::nullopt;
  }
  auto& occlusion = impl_->EnsureOcclusionResources(view_id, source);

  graphics::GpuEventScope pass_scope(recorder, "Vortex.Occlusion.PyramidBuild",
    profiling::ProfileGranularity::kDiagnostic,
    profiling::ProfileCategory::kPass);
  if (!impl_->builder.Build(
        HzbPyramidBuilder::BuildFrame {
          .sequence = ctx.frame_sequence,
          .slot = ctx.frame_slot,
          .view_id = ctx.current_view.view_id,
        },
        recorder, source,
        HzbPyramidBuilder::Targets {
          .closest = nullptr,
          .furthest = observer_ptr { occlusion.texture.get() },
        })) {
    return std::nullopt;
  }
  const auto& desc = occlusion.texture->GetDescriptor();
  return OcclusionPyramid {
    .texture = occlusion.texture,
    .srv = occlusion.srv,
    .width = desc.width,
    .height = desc.height,
    .mip_count = desc.mip_levels,
    .source = HzbPyramidBuilder::Source {
      .depth = nullptr,
      .array_slice = source.array_slice,
      .origin_x = source.origin_x,
      .origin_y = source.origin_y,
      .width = source.width,
      .height = source.height,
    },
  };
}

void ScreenHzbModule::Execute(RenderContext& ctx,
  graphics::CommandRecorder& recorder, SceneTextures& scene_textures)
{
  current_output_ = {};
  previous_output_ = {};

  const auto view_id = ctx.current_view.view_id;
  output_view_id_ = view_id;
  if (view_id == kInvalidViewId) {
    return;
  }
  if (ctx.current_view.view_state_handle
    == CompositionView::kInvalidViewStateHandle) {
    RemoveViewState(view_id);
    output_view_id_ = view_id;
  }
  auto retire_stateless = ScopeGuard([&] noexcept -> void {
    if (ctx.current_view.view_state_handle
      != CompositionView::kInvalidViewStateHandle) {
      return;
    }
    const auto found = impl_->view_states.find(view_id);
    if (found != impl_->view_states.end()) {
      if (auto gfx = impl_->renderer.GetGraphics()) {
        Impl::ReleaseViewResources(*gfx, found->second);
      }
      impl_->view_states.erase(found);
    }
  });
  if (!ctx.current_view.screen_hzb_request.WantsCurrentHzb()) {
    return;
  }

  if (impl_->renderer.GetGraphics() == nullptr) {
    return;
  }

  auto& scene_depth = scene_textures.GetSceneDepth();
  const auto scene_depth_width = scene_depth.GetDescriptor().width;
  const auto scene_depth_height = scene_depth.GetDescriptor().height;
  const auto build_closest
    = ctx.current_view.screen_hzb_request.current_closest;
  const auto build_furthest
    = ctx.current_view.screen_hzb_request.current_furthest;
  const auto source = ResolveViewDepthSource(ctx, scene_depth);

  auto& state = impl_->EnsureHistoryResources(
    view_id, source, build_closest, build_furthest);
  if (ctx.current_view.history_discontinuity) {
    state.has_current_output = false;
    state.has_previous_output = false;
  }
  state.scene_texture_width = scene_depth_width;
  state.scene_texture_height = scene_depth_height;
  state.source_view_rect_min_x = source.origin_x;
  state.source_view_rect_min_y = source.origin_y;
  state.source_view_rect_width = source.width;
  state.source_view_rect_height = source.height;

  const auto write_slot
    = state.has_current_output ? (state.current_history_slot ^ 1U) : 0U;
  const auto write_target
    = [&](const Impl::PyramidResources& pyramid,
        const bool requested) -> observer_ptr<graphics::Texture> {
    if (!requested) {
      return nullptr;
    }
    const auto& texture = pyramid.history_textures.at(write_slot);
    CHECK_NOTNULL_F(texture.get(), "Screen HZB write texture is null");
    return observer_ptr { texture.get() };
  };

  {
    graphics::GpuEventScope pass_scope(recorder, "Vortex.Stage5.ScreenHzbBuild",
      profiling::ProfileGranularity::kDiagnostic,
      profiling::ProfileCategory::kPass);
    if (!impl_->builder.Build(
          HzbPyramidBuilder::BuildFrame {
            .sequence = ctx.frame_sequence,
            .slot = ctx.frame_slot,
            .view_id = ctx.current_view.view_id,
          },
          recorder, source,
          HzbPyramidBuilder::Targets {
            .closest = write_target(state.closest, build_closest),
            .furthest = write_target(state.furthest, build_furthest),
          })) {
      return;
    }
  }
  recorder.RequireResourceState(
    scene_depth, graphics::ResourceStates::kDepthRead);

  const auto had_previous = state.has_current_output;
  current_output_ = Impl::BuildOutput(state, write_slot, true);
  previous_output_ = Impl::BuildOutput(state, state.current_history_slot,
    had_previous && current_output_.available);
  recorder.OnSubmission([this, view_id, write_slot, had_previous,
                          available = current_output_.available](
                          const graphics::SubmissionOutcome outcome) -> void {
    if (outcome != graphics::SubmissionOutcome::kSubmitted) {
      if (output_view_id_ == view_id) {
        current_output_ = {};
        previous_output_ = {};
      }
      return;
    }
    const auto found = impl_->view_states.find(view_id);
    if (found == impl_->view_states.end()) {
      return;
    }
    found->second.current_history_slot = write_slot;
    found->second.has_current_output = available;
    found->second.has_previous_output = had_previous && available;
  });

  if (current_output_.available) {
    LOG_F(INFO, "screen_hzb_published=true width={} height={} mips={}",
      current_output_.bindings.width, current_output_.bindings.height,
      current_output_.bindings.mip_count);
  }
}

auto ScreenHzbModule::GetCurrentOutput() const -> const Output&
{
  return current_output_;
}

auto ScreenHzbModule::GetPreviousOutput() const -> const Output&
{
  return previous_output_;
}

} // namespace oxygen::vortex
