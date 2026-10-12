//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Vortex/Passes/GraphicsRenderPass.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>

namespace oxygen::graphics {
class Buffer;
class CommandRecorder;
class Framebuffer;
struct FramebufferAttachment;
class GraphicsPipelineDesc;
class Texture;
} // namespace oxygen::graphics

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::vortex {
class Renderer;
} // namespace oxygen::vortex

namespace oxygen::vortex::internal {

struct CompositingPassConfig {
  std::shared_ptr<graphics::Texture> source_texture;
  ViewPort viewport {};
  float alpha { 1.0F };
  bool failed_view { false };
  //! Draws this colour, without a source texture, when set.
  std::optional<graphics::Color> fill_color;
  ShaderVisibleIndex lighting_frame_slot { kInvalidShaderVisibleIndex };
  std::string debug_name { "CompositingPass" };
};

class CompositingPass final : public GraphicsRenderPass {
public:
  using Config = CompositingPassConfig;

  //! Pass inputs come from `renderer`'s per-frame transient records.
  CompositingPass(
    std::shared_ptr<CompositingPassConfig> config, Renderer& renderer);

protected:
  auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<> override;
  auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> override;
  auto ValidateConfig() -> void override;
  auto CreatePipelineStateDesc() -> graphics::GraphicsPipelineDesc override;
  auto NeedRebuildPipelineState() const -> bool override;

private:
  auto SetupRenderTargets(graphics::CommandRecorder& recorder) const -> void;
  auto SetupViewPortAndScissors(graphics::CommandRecorder& recorder) const
    -> void;

  [[nodiscard]] auto GetFramebuffer() const -> const graphics::Framebuffer*;
  [[nodiscard]] auto GetOutputAttachment() const
    -> const graphics::FramebufferAttachment&;
  [[nodiscard]] auto GetOutputTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetSourceTexture() const -> const graphics::Texture&;
  [[nodiscard]] auto GetClampedViewport() const -> ViewPort;

  auto EnsureSourceTextureSrv(const graphics::Texture& texture)
    -> ShaderVisibleIndex;
  auto UpdatePassConstants(ShaderVisibleIndex source_texture_index) -> void;

  std::shared_ptr<Config> config_;
  upload::TransientStructuredBuffer pass_constants_;
  ViewPort clamped_viewport_ {};
  bool has_drawable_region_ { true };
};

} // namespace oxygen::vortex::internal
