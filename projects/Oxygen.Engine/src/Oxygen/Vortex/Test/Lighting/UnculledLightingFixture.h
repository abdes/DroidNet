//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <span>
#include <vector>

#include <glm/ext/vector_uint2.hpp>

#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Vortex/Lighting/Types/ForwardLocalLightRecord.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureLightingFixture.h>

namespace oxygen::vortex::testing {

//! Full-list GPU reference for the fixture's white, rough, planar receiver.
//! Only geometry/material state is shared with the raster path; lights are
//! supplied directly by the authored recipe, independently of selection.
class UnculledLightingGpuTest : public exposure::ExposureLightingGpuTest {
protected:
  struct ReferenceInput {
    std::span<const ForwardLocalLightRecord> lights;
    std::span<const exposure::Pixel> baseline;
    glm::uvec2 extent { 0U };
    bool forward_shading { false };
    float pre_exposure { 1.0F };
  };
  auto SetUp() -> void override;
  auto RecordUnculledReference(
    graphics::CommandRecorder& recorder, const ReferenceInput& input) -> void;
  auto ReadUnculledReference(std::vector<exposure::Pixel>& result) -> void;

private:
  std::shared_ptr<graphics::GpuBufferReadback> reference_readback_;
  std::size_t reference_pixel_count_ { 0U };
};

} // namespace oxygen::vortex::testing
