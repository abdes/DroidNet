//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <vector>

#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::renderer {

//! CPU-visible bridge payload emitted by the live resolve pass.
struct VirtualShadowResolvedRasterSchedule {
  frame::SequenceNumber source_frame_sequence { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t clip_level_count { 0U };
  std::uint64_t directional_address_space_hash { 0U };
  std::vector<std::uint64_t> requested_resident_keys {};
};

} // namespace oxygen::renderer
