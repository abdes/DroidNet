//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>

#include <Oxygen/Renderer/Types/VirtualShadowRenderPlan.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Renderer/Passes/ComputeRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
}

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowResolvePassConfig {
  std::string debug_name { "VirtualShadowResolvePass" };
};

class VirtualShadowResolvePass : public ComputeRenderPass {
public:
  using Config = VirtualShadowResolvePassConfig;

  OXGN_RNDR_API VirtualShadowResolvePass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowResolvePass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowResolvePass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowResolvePass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kDispatchGroupSize = 64U;
  static constexpr std::uint32_t kMaxSupportedPagesPerAxis = 64U;
  static constexpr std::uint32_t kMaxSupportedClipLevels = 12U;
  static constexpr std::uint32_t kPassConstantsSlotsPerFrame
    = (2U * kMaxSupportedClipLevels) + 2U;
  static constexpr std::uint32_t kPassConstantsSlotCount
    = kPassConstantsSlotsPerFrame * frame::kFramesInFlight.get();
  static constexpr std::uint32_t kMaxSupportedPageCount
    = kMaxSupportedPagesPerAxis * kMaxSupportedPagesPerAxis
    * kMaxSupportedClipLevels;

  struct alignas(16) ScheduleEntry {
    std::uint32_t global_page_index { 0U };
    std::uint32_t packed_entry { 0U };
    std::uint32_t atlas_tile_x { 0U };
    std::uint32_t atlas_tile_y { 0U };
  };

  struct alignas(16) PackedInt4 {
    std::int32_t x { 0 };
    std::int32_t y { 0 };
    std::int32_t z { 0 };
    std::int32_t w { 0 };
  };

  struct alignas(16) PackedFloat4 {
    float x { 0.0F };
    float y { 0.0F };
    float z { 0.0F };
    float w { 0.0F };
  };

  struct ViewScheduleResources {
    std::shared_ptr<graphics::Buffer> schedule_buffer;
    ShaderVisibleIndex schedule_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex schedule_uav { kInvalidShaderVisibleIndex };

    std::shared_ptr<graphics::Buffer> count_buffer;
    ShaderVisibleIndex count_srv { kInvalidShaderVisibleIndex };
    ShaderVisibleIndex count_uav { kInvalidShaderVisibleIndex };

    std::uint32_t entry_capacity { 0U };
  };

  struct SlotReadbackState {
    std::shared_ptr<graphics::Buffer> buffer;
    std::byte* mapped_bytes { nullptr };
    frame::SequenceNumber source_frame_sequence { 0U };
    ViewId view_id {};
    std::uint32_t pages_per_axis { 0U };
    std::uint32_t clip_level_count { 0U };
    std::uint64_t directional_address_space_hash { 0U };
    std::array<std::int32_t, kMaxSupportedClipLevels> clip_grid_origin_x {};
    std::array<std::int32_t, kMaxSupportedClipLevels> clip_grid_origin_y {};
    std::uint32_t schedule_capacity { 0U };
    bool pending_schedule { false };
  };

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  std::array<graphics::NativeView, kPassConstantsSlotCount>
    pass_constants_cbvs_ {};
  std::array<ShaderVisibleIndex, kPassConstantsSlotCount>
    pass_constants_indices_ {};
  void* pass_constants_mapped_ptr_ { nullptr };

  std::shared_ptr<graphics::Buffer> clear_count_upload_buffer_;
  void* clear_count_upload_mapped_ptr_ { nullptr };

  std::unordered_map<ViewId, ViewScheduleResources> view_schedule_resources_;
  std::array<SlotReadbackState, frame::kFramesInFlight.get()>
    slot_readbacks_ {};

  ViewId active_view_id_ {};
  ShaderVisibleIndex active_request_words_srv_ { kInvalidShaderVisibleIndex };
  std::uint32_t active_request_word_count_ { 0U };
  std::uint32_t active_dispatch_group_count_ { 0U };
  std::uint32_t active_pages_per_axis_ { 0U };
  std::uint32_t active_clip_level_count_ { 0U };
  std::uint64_t active_directional_address_space_hash_ { 0U };
  std::array<std::int32_t, kMaxSupportedClipLevels>
    active_clip_grid_origin_x_ {};
  std::array<std::int32_t, kMaxSupportedClipLevels>
    active_clip_grid_origin_y_ {};
  std::uint32_t active_schedule_capacity_ { 0U };
  std::uint32_t active_pages_per_level_ { 0U };
  std::uint32_t active_requested_page_list_count_ { 0U };
  std::uint32_t active_dirty_page_list_count_ { 0U };
  std::uint32_t active_clean_page_list_count_ { 0U };
  std::uint32_t active_total_page_management_list_count_ { 0U };
  renderer::VirtualShadowPageManagementBindings
    active_page_management_bindings_ {};
  std::array<float, kMaxSupportedClipLevels> active_clip_origin_x_ {};
  std::array<float, kMaxSupportedClipLevels> active_clip_origin_y_ {};
  std::array<float, kMaxSupportedClipLevels> active_clip_page_world_ {};
  bool active_dispatch_ { false };

  OXGN_RNDR_API auto EnsurePassConstantsBuffer() -> void;
  OXGN_RNDR_API auto EnsureClearCountUploadBuffer() -> void;
  OXGN_RNDR_API auto EnsureReadbackBuffer(frame::Slot slot) -> void;
  OXGN_RNDR_API auto ProcessCompletedSchedule(frame::Slot slot) -> void;
  OXGN_RNDR_API auto EnsureViewScheduleResources(ViewId view_id,
    std::uint32_t required_entry_capacity) -> ViewScheduleResources*;
};

} // namespace oxygen::engine
