//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::graphics {
class Buffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

//! Per-draw visibility bits of one culling view, written on the GPU.
/*!
 Mirrored by `Vortex/Contracts/Draw/DrawVisibility.hlsli`.
*/
enum class DrawVisibilityBit : std::uint8_t {
  //! The box intersects the view frustum and covers a pixel center.
  kInFrustum = 1U << 0U,
  //! Drawn by phase 1: in the frustum and visible last frame.
  kPhase1Drawn = 1U << 1U,
  //! Drawn by phase 2: in the frustum, not phase 1, and not occluded.
  kPhase2Drawn = 1U << 2U,
  //! In the frustum and not occluded; next frame's history.
  kVisible = 1U << 3U,
};

[[nodiscard]] constexpr auto ToUnderlying(const DrawVisibilityBit bit) noexcept
  -> std::uint32_t
{
  return static_cast<std::uint32_t>(bit);
}

//! A list predicate: keeps a draw when any of its bits is set.
/*!
 The empty predicate keeps every draw; it is the fallback when a view has no
 visibility buffer.
*/
class DrawVisibilityPredicate {
public:
  constexpr DrawVisibilityPredicate() noexcept = default;

  template <typename... Bits>
  constexpr explicit DrawVisibilityPredicate(
    const DrawVisibilityBit first, const Bits... rest) noexcept
    : mask_((ToUnderlying(first) | ... | ToUnderlying(rest)))
  {
  }

  //! The final drawn set: phase 1 or phase 2.
  [[nodiscard]] static constexpr auto Drawn() noexcept
    -> DrawVisibilityPredicate
  {
    return DrawVisibilityPredicate {
      DrawVisibilityBit::kPhase1Drawn,
      DrawVisibilityBit::kPhase2Drawn,
    };
  }

  [[nodiscard]] constexpr auto Mask() const noexcept -> std::uint32_t
  {
    return mask_;
  }

  [[nodiscard]] constexpr auto KeepsAll() const noexcept -> bool
  {
    return mask_ == 0U;
  }

  auto operator==(const DrawVisibilityPredicate&) const -> bool = default;

private:
  std::uint32_t mask_ { 0U };
};

//! One view's per-draw visibility for this frame, one `uint` per draw.
/*!
 The buffer is in the shader-resource state. An invalid product means the
 view was not culled; lists then keep every candidate. Until phase 2 has run,
 `kPhase2Drawn` is clear for every draw.
*/
struct DrawVisibilityProducts {
  observer_ptr<const graphics::Buffer> buffer;
  ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  std::uint32_t draw_count { 0U };
  //! Phase 2 has written its bits; the visibility is final.
  bool phase2 { false };

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return buffer != nullptr && srv.IsValid();
  }
};

} // namespace oxygen::vortex
