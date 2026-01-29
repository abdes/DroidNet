//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

namespace oxygen::examples::ui {

//! Configuration for the stats overlay.
struct StatsOverlayConfig {
  bool show_fps { false };
  bool show_frame_timing_detail { false };
};

} // namespace oxygen::examples::ui
