#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

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
class Texture;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
}

namespace oxygen::engine {

struct VirtualShadowScreenDebugPassConfig {
  std::string debug_name { "VirtualShadowScreenDebugPass" };
};

class VirtualShadowScreenDebugPass final : public ComputeRenderPass {
public:
  using Config = VirtualShadowScreenDebugPassConfig;

  OXGN_RNDR_API VirtualShadowScreenDebugPass(
    observer_ptr<Graphics> gfx, std::shared_ptr<Config> config);
  OXGN_RNDR_API ~VirtualShadowScreenDebugPass() override;

  OXYGEN_MAKE_NON_COPYABLE(VirtualShadowScreenDebugPass)
  OXYGEN_DEFAULT_MOVABLE(VirtualShadowScreenDebugPass)

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::ComputePipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr std::uint32_t kDispatchGroupSize = 8U;
  static constexpr std::uint32_t kClipHistogramCount = 12U;
  static constexpr std::uint32_t kStatsWordCount = 7U + 2U * kClipHistogramCount;

  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureStatsBuffer() -> void;
  auto EnsureStatsClearUploadBuffer() -> void;
  auto EnsureStatsReadbackBuffer(frame::Slot slot) -> void;
  auto EnsureDepthTextureSrv(const graphics::Texture& depth_tex)
    -> bindless::ShaderVisibleIndex;
  auto ProcessCompletedStats(frame::Slot slot) -> void;

  observer_ptr<Graphics> gfx_;
  std::shared_ptr<Config> config_;

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_;
  graphics::NativeView pass_constants_cbv_ {};
  bindless::ShaderVisibleIndex pass_constants_index_ {
    kInvalidShaderVisibleIndex
  };
  void* pass_constants_mapped_ptr_ { nullptr };

  ShaderVisibleIndex depth_texture_srv_ { kInvalidShaderVisibleIndex };
  const graphics::Texture* depth_texture_owner_ { nullptr };
  bool owns_depth_texture_srv_ { false };

  std::shared_ptr<graphics::Buffer> stats_buffer_;
  graphics::NativeView stats_uav_view_ {};
  bindless::ShaderVisibleIndex stats_uav_index_ { kInvalidShaderVisibleIndex };
  std::shared_ptr<graphics::Buffer> stats_clear_upload_buffer_;
  void* stats_clear_upload_mapped_ptr_ { nullptr };

  struct StatsReadbackState {
    std::shared_ptr<graphics::Buffer> buffer;
    std::uint32_t* mapped_words { nullptr };
    frame::SequenceNumber source_frame { 0U };
    ViewId view_id {};
    bool pending { false };
  };
  std::array<StatsReadbackState, frame::kFramesInFlight.get()>
    stats_readbacks_ {};

  bool active_dispatch_ { false };
  std::uint32_t active_width_ { 0U };
  std::uint32_t active_height_ { 0U };
};

} // namespace oxygen::engine
