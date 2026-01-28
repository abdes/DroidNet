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

//! Transparent overlay for engine statistics.
/*!
 Draws a right-aligned stats overlay for FPS and frame timing. The overlay is
 non-interactive and designed to be shared across demos.
 */
class StatsOverlay {
public:
  StatsOverlay() = default;
  ~StatsOverlay() = default;

  StatsOverlay(const StatsOverlay&) = delete;
  auto operator=(const StatsOverlay&) -> StatsOverlay& = delete;
  StatsOverlay(StatsOverlay&&) = default;
  auto operator=(StatsOverlay&&) -> StatsOverlay& = default;

  //! Set the stats overlay configuration.
  void SetConfig(const StatsOverlayConfig& config) { config_ = config; }

  //! Get the stats overlay configuration.
  [[nodiscard]] auto GetConfig() const -> const StatsOverlayConfig&
  {
    return config_;
  }

  //! Draw the stats overlay.
  auto Draw() const -> void;

private:
  StatsOverlayConfig config_ {};
};

} // namespace oxygen::examples::ui
