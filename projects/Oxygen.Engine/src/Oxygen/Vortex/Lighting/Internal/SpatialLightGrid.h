//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Lighting/Internal/LightGridBuilder.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridResources.h>

namespace oxygen::vortex {
class Renderer;
namespace lighting::internal {

  //! Owns per-view, frame-slot-isolated GPU count/scan/scatter products.
  class SpatialLightGrid {
  public:
    explicit SpatialLightGrid(Renderer& renderer);
    ~SpatialLightGrid();
    void OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot);
    void SetActiveViewCount(std::uint32_t count);
    [[nodiscard]] auto Prepare(
      const BuiltLightGridView& view, LightingFrameBindings& bindings) -> bool;
    [[nodiscard]] auto Record(
      const BuiltLightGridView& view, ShaderVisibleIndex header) -> bool;
    [[nodiscard]] auto Inspect(ViewId view) const -> LightGridResources;
    [[nodiscard]] auto InspectCompleted(ViewId view) -> std::optional<CompletedLightGridBuild>;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
  };

} // namespace lighting::internal
} // namespace oxygen::vortex
