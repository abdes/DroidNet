//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <type_traits>

#include <Oxygen/Base/NamedType.h>

namespace oxygen::vortex {

//! Reserved wire value for an absent lighting-array element, not a descriptor.
inline constexpr auto kInvalidLightingArrayIndexValue
  = std::numeric_limits<std::uint32_t>::max();

//! Explicit primary/secondary atmosphere slot, independent of selection order.
using AtmosphereLightIndex = NamedType<std::uint32_t,
  struct AtmosphereLightIndexTag, Comparable, Printable, Hashable>;
inline constexpr AtmosphereLightIndex kInvalidAtmosphereLightIndex {
  kInvalidLightingArrayIndexValue,
};

//! Index into the immutable local or directional selection owned by a binding.
/*!
 The owning array and its selection revision complete this index's identity.
 A shadow-reference array has the same order as its corresponding light array.
*/
using LightSelectionIndex = NamedType<std::uint32_t,
  struct LightSelectionIndexTag, Comparable, Printable, Hashable>;

//! Index into the shadow-record array selected by a reference's projection
//! kind.
using ShadowRecordIndex = NamedType<std::uint32_t, struct ShadowRecordIndexTag,
  Comparable, Printable, Hashable>;

//! Index into the current view's flat cascade-record array.
using ShadowCascadeIndex = NamedType<std::uint32_t,
  struct ShadowCascadeIndexTag, Comparable, Printable, Hashable>;

//! Texture-array layer, relative to a separately identified shadow surface.
using ShadowArrayLayer = NamedType<std::uint32_t, struct ShadowArrayLayerTag,
  Comparable, Printable, Hashable>;

//! Element offset into the current view's compact local-light index list.
using LightListOffset = NamedType<std::uint32_t, struct LightListOffsetTag,
  Comparable, Printable, Hashable>;

inline constexpr LightSelectionIndex kInvalidLightSelectionIndex {
  kInvalidLightingArrayIndexValue,
};
inline constexpr ShadowRecordIndex kInvalidShadowRecordIndex {
  kInvalidLightingArrayIndexValue,
};
inline constexpr ShadowCascadeIndex kInvalidShadowCascadeIndex {
  kInvalidLightingArrayIndexValue,
};
inline constexpr ShadowArrayLayer kInvalidShadowArrayLayer {
  kInvalidLightingArrayIndexValue,
};

//! This range encoding enumerates all local records without compact-list reads.
//! It is a complete-list marker, not a valid compact-list offset.
inline constexpr LightListOffset kCompleteLightListOffset {
  kInvalidLightingArrayIndexValue,
};

// Fixed wire widths from lighting-gpu-abi.md; C++ types are not
// interchangeable.

// NOLINTBEGIN(*-magic-numbers)
static_assert(sizeof(AtmosphereLightIndex) == 4U);
static_assert(alignof(AtmosphereLightIndex) == 4U);
static_assert(std::is_standard_layout_v<AtmosphereLightIndex>);
static_assert(std::is_trivially_copyable_v<AtmosphereLightIndex>);
static_assert(sizeof(LightSelectionIndex) == 4U);
static_assert(alignof(LightSelectionIndex) == 4U);
static_assert(std::is_standard_layout_v<LightSelectionIndex>);
static_assert(std::is_trivially_copyable_v<LightSelectionIndex>);
static_assert(sizeof(ShadowRecordIndex) == 4U);
static_assert(alignof(ShadowRecordIndex) == 4U);
static_assert(std::is_standard_layout_v<ShadowRecordIndex>);
static_assert(std::is_trivially_copyable_v<ShadowRecordIndex>);
static_assert(sizeof(ShadowCascadeIndex) == 4U);
static_assert(alignof(ShadowCascadeIndex) == 4U);
static_assert(std::is_standard_layout_v<ShadowCascadeIndex>);
static_assert(std::is_trivially_copyable_v<ShadowCascadeIndex>);
static_assert(sizeof(ShadowArrayLayer) == 4U);
static_assert(alignof(ShadowArrayLayer) == 4U);
static_assert(std::is_standard_layout_v<ShadowArrayLayer>);
static_assert(std::is_trivially_copyable_v<ShadowArrayLayer>);
static_assert(sizeof(LightListOffset) == 4U);
static_assert(alignof(LightListOffset) == 4U);
static_assert(std::is_standard_layout_v<LightListOffset>);
static_assert(std::is_trivially_copyable_v<LightListOffset>);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex
