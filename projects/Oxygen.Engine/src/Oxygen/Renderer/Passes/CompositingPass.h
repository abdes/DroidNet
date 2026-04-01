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
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
struct FramebufferAttachment;
struct GraphicsPipelineDesc;
class Texture;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::engine {

//! Configuration for compositing a source texture into a framebuffer.
struct CompositingPassConfig {
  //! Source texture to composite.
  /*!
   Must already be registered in the graphics ResourceRegistry before the pass
   prepares SRVs for it.
  */
  std::shared_ptr<graphics::Texture> source_texture {};

  //! Destination region for compositing.
  ViewPort viewport {};

  //! Global alpha applied to the composited source.
  float alpha { 1.0F };

  //! Debug label for diagnostics.
  std::string debug_name { "CompositingPass" };
};

//! Alpha-blended compositing pass for picture-in-picture output.
/*!
 Composites a source render target into a specified viewport region of a
 framebuffer using alpha blending. This pass is designed for integrating
 offscreen view results into the swapchain backbuffer.

 Source and destination textures are not owned by the pass. Callers must
 establish command-recorder resource-state tracking for both before execution;
 the pass enforces that contract and does not seed tracking itself.
*/
class CompositingPass final : public GraphicsRenderPass {
public:
  using Config = CompositingPassConfig;

  OXGN_RNDR_API explicit CompositingPass(
    std::shared_ptr<CompositingPassConfig> config);

  ~CompositingPass() override;

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  static constexpr uint32_t kPassConstantsStride = 256u;
  static constexpr uint32_t kPassConstantsChunkSlots = 16u;
  static constexpr size_t kFrameSlotCount
    = static_cast<size_t>(frame::kFramesInFlight.get());

  struct PassConstantsChunk {
    std::shared_ptr<graphics::Buffer> buffer {};
    std::byte* mapped_ptr { nullptr };
    std::array<ShaderVisibleIndex, kPassConstantsChunkSlots> indices {};
    uint32_t used_slots { 0u };
  };

  struct FramePassConstantsState {
    frame::SequenceNumber frame_sequence { frame::kInvalidSequenceNumber };
    std::vector<PassConstantsChunk> chunks {};
  };

  auto ReleasePassConstantsBuffer() -> void;
  auto ReleasePassConstantsFrameState(FramePassConstantsState& state) -> void;

  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;
  [[nodiscard]] auto GetOutputAttachment() const
    -> const graphics::FramebufferAttachment&;
  [[nodiscard]] auto GetOutputTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetSourceTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetClampedViewport() const -> ViewPort;

  auto EnsurePassConstantsBuffer() -> void;
  auto CreatePassConstantsChunk(FramePassConstantsState& state) -> void;
  auto GetCurrentFramePassConstantsState() -> FramePassConstantsState&;
  auto EnsureSourceTextureSrv(const graphics::Texture& texture)
    -> ShaderVisibleIndex;
  auto UpdatePassConstants(ShaderVisibleIndex source_texture_index) -> void;

  std::shared_ptr<Config> config_ {};
  observer_ptr<Graphics> graphics_ { nullptr };
  std::array<FramePassConstantsState, kFrameSlotCount>
    pass_constants_frames_ {};
  ViewPort clamped_viewport_ {};
  bool has_drawable_region_ { true };
};

} // namespace oxygen::engine
