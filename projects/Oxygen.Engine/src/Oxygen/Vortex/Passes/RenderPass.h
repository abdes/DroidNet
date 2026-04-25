//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <Oxygen/Composition/Composition.h>
#include <Oxygen/Composition/Named.h>
#include <Oxygen/Core/Bindless/Generated.RootSignature.D3D12.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Vortex/Types/PassMask.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
struct DrawMetadata;

class RenderPass : public Composition, public Named {
public:
  OXGN_VRTX_API explicit RenderPass(std::string_view name);
  ~RenderPass() override = default;

  OXYGEN_MAKE_NON_COPYABLE(RenderPass)
  OXYGEN_DEFAULT_MOVABLE(RenderPass)

  OXGN_VRTX_NDAPI auto PrepareResources(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;
  OXGN_VRTX_NDAPI auto Execute(const RenderContext& context,
    graphics::CommandRecorder& recorder) -> co::Co<>;

  OXGN_VRTX_NDAPI auto GetName() const noexcept -> std::string_view override;
  OXGN_VRTX_API auto SetName(std::string_view name) noexcept -> void override;

protected:
  OXGN_VRTX_NDAPI auto Context() const -> const RenderContext&;
  OXGN_VRTX_NDAPI static auto BuildRootBindings()
    -> std::vector<graphics::RootBindingItem>;
  OXGN_VRTX_NDAPI static auto RootConstantsBindingSlot()
    -> graphics::BindingSlotDesc;

  auto SetPassConstantsIndex(ShaderVisibleIndex pass_constants_index) noexcept
    -> void
  {
    pass_constants_index_ = pass_constants_index;
  }

  [[nodiscard]] auto GetPassConstantsIndex() const noexcept
    -> ShaderVisibleIndex
  {
    return pass_constants_index_;
  }

  virtual auto ValidateConfig() -> void = 0;
  virtual auto OnPrepareResources(graphics::CommandRecorder& recorder) -> void
    = 0;
  virtual auto DoPrepareResources(graphics::CommandRecorder& recorder)
    -> co::Co<>
    = 0;
  virtual auto OnExecute(graphics::CommandRecorder& recorder) -> void = 0;
  virtual auto DoExecute(graphics::CommandRecorder& recorder) -> co::Co<> = 0;

  OXGN_VRTX_API auto BindDrawIndexConstant(graphics::CommandRecorder& recorder,
    std::uint32_t draw_index) const -> void;
  OXGN_VRTX_API auto EmitDrawRange(graphics::CommandRecorder& recorder,
    const DrawMetadata* records, uint32_t begin, uint32_t end,
    uint32_t& emitted_count, uint32_t& skipped_invalid,
    uint32_t& draw_errors) const noexcept -> void;
  OXGN_VRTX_API auto IssueDrawCallsOverPass(graphics::CommandRecorder& recorder,
    PassMaskBit pass_bit) const noexcept -> void;

private:
  const RenderContext* context_ { nullptr };
  ShaderVisibleIndex pass_constants_index_ { kInvalidShaderVisibleIndex };
};

} // namespace oxygen::vortex
