//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ShaderType.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Vortex/Internal/BindlessRootBindings.h>
#include <Oxygen/Vortex/Internal/PerViewStructuredPublisher.h>
#include <Oxygen/Vortex/Internal/TextureViews.h>
#include <Oxygen/Vortex/Renderer.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>

namespace oxygen::vortex {

namespace {

  namespace bindless_d3d12 = oxygen::bindless::generated::d3d12;

  //! Mips per dispatch: a 64 x 64 tile of the dispatch's first level.
  constexpr std::uint32_t kLevelsPerDispatch = 7U;
  constexpr std::uint32_t kTileExtent = 64U;
  constexpr std::size_t kMipUavCapacity = 16U;
  //! Scalar fields before the per-mip UAV arrays, padding included.
  constexpr std::size_t kScalarFieldCount = 12U;
  static_assert(HzbPyramidBuilder::kMaxMipCount <= 2U * kLevelsPerDispatch);
  static_assert(HzbPyramidBuilder::kMaxMipCount <= kMipUavCapacity);

  //! Mirrors HzbBuildPassConstants in ScreenHzbBuild.hlsl.
  struct alignas(packing::kShaderDataFieldAlignment) HzbBuildPassConstants {
    ShaderVisibleIndex source_depth_srv { kInvalidShaderVisibleIndex };
    std::uint32_t source_origin_x { 0U };
    std::uint32_t source_origin_y { 0U };
    std::uint32_t source_width { 0U };
    std::uint32_t source_height { 0U };
    std::uint32_t root_width { 0U };
    std::uint32_t root_height { 0U };
    std::uint32_t mip_count { 0U };
    std::uint32_t base_level { 0U };
    std::uint32_t _pad0 { 0U };
    std::uint32_t _pad1 { 0U };
    std::uint32_t _pad2 { 0U };
    std::array<ShaderVisibleIndex, kMipUavCapacity> closest_mip_uavs {};
    std::array<ShaderVisibleIndex, kMipUavCapacity> furthest_mip_uavs {};
  };

  static_assert(sizeof(HzbBuildPassConstants)
    == (kScalarFieldCount + (2U * kMipUavCapacity)) * sizeof(std::uint32_t));
  static_assert(
    sizeof(HzbBuildPassConstants) % packing::kShaderDataFieldAlignment == 0U);

  auto BuildPipelineDesc(const std::string& debug_name)
    -> graphics::ComputePipelineDesc
  {
    const auto root_bindings = internal::BuildVortexRootBindings();
    return graphics::ComputePipelineDesc::Builder()
      .SetComputeShader({
        .stage = ShaderType::kCompute,
        .source_path = "Vortex/Stages/Occlusion/ScreenHzbBuild.hlsl",
        .entry_point = "VortexScreenHzbBuildCS",
      })
      .SetRootBindings(std::span(root_bindings))
      .SetDebugName(debug_name)
      .Build();
  }

  auto TrackFromKnownOrInitial(graphics::CommandRecorder& recorder,
    const graphics::Texture& texture,
    const graphics::ResourceStates fallback_initial) -> void
  {
    if (recorder.IsResourceTracked(texture)
      || recorder.AdoptKnownResourceState(texture)) {
      return;
    }
    auto initial = texture.GetDescriptor().initial_state;
    if (initial == graphics::ResourceStates::kUnknown
      || initial == graphics::ResourceStates::kUndefined) {
      initial = fallback_initial;
    }
    recorder.BeginTrackingResourceState(texture, initial, true);
  }

  //! Fills `uavs` with one UAV per mip of `texture`; false on failure.
  auto ResolveMipUavs(Graphics& gfx, const graphics::Texture& texture,
    const std::uint32_t mip_count,
    std::array<ShaderVisibleIndex, kMipUavCapacity>& uavs) -> bool
  {
    for (std::uint32_t mip = 0U; mip < mip_count; ++mip) {
      uavs.at(mip) = internal::EnsureTextureView(
        gfx, texture, internal::MipUavDesc(texture, mip));
      if (!uavs.at(mip).IsValid()) {
        return false;
      }
    }
    return true;
  }

} // namespace

struct HzbPyramidBuilder::Impl {
  Impl(Renderer& renderer_in, const std::string_view debug_name_in)
    : renderer(&renderer_in)
    , debug_name(debug_name_in)
    , pass_constants(observer_ptr { renderer_in.GetGraphics().get() },
        renderer_in.GetStagingProvider(),
        observer_ptr { &renderer_in.GetInlineTransfersCoordinator() },
        debug_name + ".PassConstants")
  {
  }

  observer_ptr<Renderer> renderer;
  std::string debug_name;
  internal::PerViewStructuredPublisher<HzbBuildPassConstants> pass_constants;
  std::optional<frame::SequenceNumber> constants_frame;
  std::optional<graphics::ComputePipelineDesc> pipeline_desc;
};

auto HzbPyramidBuilder::ComputeRootExtent(const std::uint32_t source_extent)
  -> std::uint32_t
{
  CHECK_F(source_extent != 0U, "HZB requires a non-empty source rect");
  return (std::max)(std::bit_ceil(source_extent) >> 1U, 1U);
}

auto HzbPyramidBuilder::ComputeMipCount(const std::uint32_t root_width,
  const std::uint32_t root_height) -> std::uint32_t
{
  CHECK_F(root_width != 0U && root_height != 0U,
    "HZB requires a non-empty root extent");
  return (std::max)(static_cast<std::uint32_t>(
                      std::bit_width((std::max)(root_width, root_height)))
      - 1U,
    1U);
}

auto HzbPyramidBuilder::MakeTextureDesc(const std::uint32_t source_width,
  const std::uint32_t source_height, std::string debug_name)
  -> graphics::TextureDesc
{
  auto desc = graphics::TextureDesc {};
  desc.width = ComputeRootExtent(source_width);
  desc.height = ComputeRootExtent(source_height);
  desc.mip_levels = ComputeMipCount(desc.width, desc.height);
  desc.format = oxygen::Format::kR32Float;
  desc.texture_type = oxygen::TextureType::kTexture2D;
  desc.is_shader_resource = true;
  desc.is_uav = true;
  desc.initial_state = graphics::ResourceStates::kCommon;
  desc.debug_name = std::move(debug_name);
  return desc;
}

HzbPyramidBuilder::HzbPyramidBuilder(
  Renderer& renderer, const std::string_view debug_name)
  : impl_(std::make_unique<Impl>(renderer, debug_name))
{
}

HzbPyramidBuilder::~HzbPyramidBuilder() = default;

auto HzbPyramidBuilder::Build(const BuildFrame& frame,
  graphics::CommandRecorder& recorder, const Source& source,
  const Targets& targets) -> bool
{
  CHECK_NOTNULL_F(source.depth.get(), "HZB build requires a source depth");
  CHECK_F(source.width != 0U && source.height != 0U,
    "HZB build requires a non-empty source rect");
  CHECK_F(targets.closest != nullptr || targets.furthest != nullptr,
    "HZB build requires at least one target pyramid");

  auto gfx = impl_->renderer->GetGraphics();
  CHECK_NOTNULL_F(gfx.get(), "HZB build requires Graphics");

  const auto root_width = ComputeRootExtent(source.width);
  const auto root_height = ComputeRootExtent(source.height);
  const auto mip_count = ComputeMipCount(root_width, root_height);
  CHECK_LE_F(mip_count, kMaxMipCount, "HZB source rect is too large");

  auto constants = HzbBuildPassConstants {
    .source_depth_srv = internal::EnsureTextureView(*gfx, *source.depth,
      internal::ArraySliceSrvDesc(*source.depth, source.array_slice)),
    .source_origin_x = source.origin_x,
    .source_origin_y = source.origin_y,
    .source_width = source.width,
    .source_height = source.height,
    .root_width = root_width,
    .root_height = root_height,
    .mip_count = mip_count,
  };
  constants.closest_mip_uavs.fill(kInvalidShaderVisibleIndex);
  constants.furthest_mip_uavs.fill(kInvalidShaderVisibleIndex);
  if (!constants.source_depth_srv.IsValid()) {
    LOG_F(
      ERROR, "{}: failed to resolve the source depth SRV", impl_->debug_name);
    return false;
  }

  const auto target_list = std::array {
    std::pair { targets.closest, &constants.closest_mip_uavs },
    std::pair { targets.furthest, &constants.furthest_mip_uavs },
  };
  for (const auto& [texture, uavs] : target_list) {
    if (texture == nullptr) {
      continue;
    }
    const auto& desc = texture->GetDescriptor();
    CHECK_F(desc.width == root_width && desc.height == root_height
        && desc.mip_levels == mip_count,
      "HZB target '{}' does not match its source rect", desc.debug_name);
    if (!ResolveMipUavs(*gfx, *texture, mip_count, *uavs)) {
      LOG_F(ERROR, "{}: failed to resolve the mip UAVs of '{}'",
        impl_->debug_name, desc.debug_name);
      return false;
    }
  }

  if (impl_->constants_frame != frame.sequence) {
    impl_->pass_constants.OnFrameStart(frame.sequence, frame.slot);
    impl_->constants_frame = frame.sequence;
  }
  const auto view_id = frame.view_id;
  const auto tile_constants = impl_->pass_constants.Publish(view_id, constants);
  const auto has_tail = mip_count > kLevelsPerDispatch;
  auto tail_constants = ShaderVisibleIndex { kInvalidShaderVisibleIndex };
  if (has_tail) {
    constants.base_level = kLevelsPerDispatch;
    tail_constants = impl_->pass_constants.Publish(view_id, constants);
  }
  if (!tile_constants.IsValid() || (has_tail && !tail_constants.IsValid())) {
    LOG_F(ERROR, "{}: failed to publish the pass constants", impl_->debug_name);
    return false;
  }

  if (!impl_->pipeline_desc.has_value()) {
    impl_->pipeline_desc = BuildPipelineDesc(impl_->debug_name);
  }

  TrackFromKnownOrInitial(
    recorder, *source.depth, graphics::ResourceStates::kDepthRead);
  recorder.RequireResourceState(
    *source.depth, graphics::ResourceStates::kShaderResource);
  for (const auto& [texture, uavs] : target_list) {
    if (texture == nullptr) {
      continue;
    }
    TrackFromKnownOrInitial(
      recorder, *texture, graphics::ResourceStates::kCommon);
    // Makes the second UAV transition below a UAV barrier, so the tail
    // dispatch reads the mip 6 the tile dispatch wrote.
    recorder.EnableAutoMemoryBarriers(*texture);
    recorder.RequireResourceState(
      *texture, graphics::ResourceStates::kUnorderedAccess);
  }
  recorder.FlushBarriers();

  recorder.SetPipelineState(*impl_->pipeline_desc);
  const auto root_constants
    = static_cast<std::uint32_t>(bindless_d3d12::RootParam::kRootConstants);
  recorder.SetComputeRoot32BitConstant(root_constants, 0U, 0U);
  recorder.SetComputeRoot32BitConstant(
    root_constants, tile_constants.get(), 1U);
  recorder.Dispatch((root_width + kTileExtent - 1U) / kTileExtent,
    (root_height + kTileExtent - 1U) / kTileExtent, 1U);

  if (has_tail) {
    for (const auto& [texture, uavs] : target_list) {
      if (texture != nullptr) {
        recorder.RequireResourceState(
          *texture, graphics::ResourceStates::kUnorderedAccess);
      }
    }
    recorder.FlushBarriers();
    recorder.SetComputeRoot32BitConstant(
      root_constants, tail_constants.get(), 1U);
    recorder.Dispatch(1U, 1U, 1U);
  }

  for (const auto& [texture, uavs] : target_list) {
    if (texture != nullptr) {
      recorder.DisableAutoMemoryBarriers(*texture);
      recorder.RequireResourceState(
        *texture, graphics::ResourceStates::kShaderResource);
    }
  }
  return true;
}

} // namespace oxygen::vortex
