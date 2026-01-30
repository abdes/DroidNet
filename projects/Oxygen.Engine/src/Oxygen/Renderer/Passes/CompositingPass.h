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

#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Renderer/Passes/GraphicsRenderPass.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine {

//! Configuration for compositing a source texture into a framebuffer.
struct CompositingPassConfig {
  //! Source texture to composite.
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
  auto ReleasePassConstantsBuffer() -> void;

  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;
  [[nodiscard]] auto GetOutputTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetSourceTexture() const -> const graphics::Texture&;

  auto EnsurePassConstantsBuffer() -> void;
  auto EnsureSourceTextureSrv(const graphics::Texture& texture)
    -> ShaderVisibleIndex;
  auto UpdatePassConstants(ShaderVisibleIndex source_texture_index) -> void;

  static constexpr uint32_t kPassConstantsStride = 256u;
  static constexpr size_t kPassConstantsSlots = 8u;

  std::shared_ptr<Config> config_ {};

  std::shared_ptr<graphics::Buffer> pass_constants_buffer_ {};
  std::byte* pass_constants_mapped_ptr_ { nullptr };
  std::array<ShaderVisibleIndex, kPassConstantsSlots> pass_constants_indices_;
  size_t pass_constants_slot_ { 0u };

  std::unordered_map<const graphics::Texture*, ShaderVisibleIndex>
    source_texture_srvs_ {};
};

} // namespace oxygen::engine
