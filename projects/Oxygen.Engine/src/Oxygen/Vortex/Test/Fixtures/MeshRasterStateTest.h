//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <span>
#include <string_view>

#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace oxygen::vortex::testing {

//! Adjacent draws exercise each sidedness/handedness pair and return to the
//! first.
inline auto MakeRasterStateDraws(const PassMaskBit kind)
  -> std::array<DrawMetadata, 5>
{
  auto draws = std::array<DrawMetadata, 5> {};
  for (std::size_t index = 0U; index < draws.size(); ++index) {
    auto& draw = draws.at(index);
    draw.vertex_count = (static_cast<std::uint32_t>(index) + 1U) * 3U;
    draw.instance_count = 1U;
    draw.material_handle = 1U;
    draw.flags = PassMask {
      kind,
      PassMaskBit::kMainViewVisible,
    };
  }
  draws.at(1).flags.Set(PassMaskBit::kDoubleSided);
  draws.at(2).flags.Set(PassMaskBit::kDoubleSided);
  draws.at(2).flags.Set(PassMaskBit::kReverseWinding);
  draws.at(3).flags.Set(PassMaskBit::kReverseWinding);
  return draws;
}

//! Checks the state active at each draw, rather than an unrelated PSO bind.
inline auto ExpectRasterStateDraws(
  const std::span<const DrawCommandLog::Event> draws,
  const std::string_view pipeline_prefix) -> void
{
  constexpr auto kCullModes = std::array {
    graphics::CullMode::kBack,
    graphics::CullMode::kNone,
    graphics::CullMode::kNone,
    graphics::CullMode::kBack,
    graphics::CullMode::kBack,
  };
  constexpr auto kFrontCounterClockwise = std::array {
    true,
    true,
    false,
    false,
    true,
  };
  auto seen = std::array<std::size_t, 5> {};
  for (const auto& draw : draws) {
    if (!std::string_view(draw.pipeline_name).starts_with(pipeline_prefix)) {
      continue;
    }
    ASSERT_GE(draw.vertex_num, 3U);
    ASSERT_EQ(draw.vertex_num % 3U, 0U);
    const auto index = static_cast<std::size_t>((draw.vertex_num / 3U) - 1U);
    ASSERT_LT(index, seen.size());
    SCOPED_TRACE(index);
    ++seen.at(index);
    if (!draw.rasterizer.has_value()) {
      FAIL() << "Matching draw has no rasterizer state";
    }
    EXPECT_EQ(draw.rasterizer->cull_mode, kCullModes.at(index));
    EXPECT_EQ(draw.rasterizer->front_counter_clockwise,
      kFrontCounterClockwise.at(index));
  }
  for (const auto count : seen) {
    EXPECT_EQ(count, 1U);
  }
}

} // namespace oxygen::vortex::testing
