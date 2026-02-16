//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <string>

#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::engine::sceneprep {

//! Handle to a transform entry managed by ScenePrep.
/*!
 Assigned during collection via `TransformUploader::GetOrAllocate(matrix)`.

 Provides stable identity for transforms before GPU buffers are allocated as
 part of finalization, and can be used as index into shared GPU transform buffer
 during rendering (bindless access).

 Handles are stable for the lifetime of the residency entry but may be recycled
 over long-running execution; do not assume monotonically increasing values. Use
 `TransformHandle::get()` to retrieve the underlying integer index when
 interacting with low-level APIs.
*/
using TransformHandle = NamedType<uint32_t,
  // clang-format off
  struct TransformHandleTag,
  Arithmetic
  >; // clang-format on

//! Invalid TransformHandle sentinel value.
inline constexpr TransformHandle kInvalidTransformHandle { (
  std::numeric_limits<std::uint32_t>::max)() };

constexpr auto to_string(TransformHandle h)
{
  return "TransH(" + std::to_string(h.get()) + ")";
}

//! Handle to a material entry managed by MaterialBinder.
/*!
 Assigned during collection via `MaterialBinder::GetOrAllocate(material)`.

 Uses content-based hashing: identical materials receive the same handle, while
 unique materials allocate new handles. Multiple items referencing the same
 material receive the same handle at collection time. During finalization,
 handles map to GPU atlas slots and constant buffer entries, enabling bindless
 access during rendering.

 Handles remain stable for the lifetime of the residency entry but may be
 recycled during long-running execution. Handle generation is part of identity
 and must be validated by MaterialBinder before use.
*/
class MaterialHandle {
public:
  using Index = NamedType<uint32_t, struct MaterialHandleIndexTag,
    // clang-format off
    Comparable,
    Printable
    >; // clang-format on
  using Generation = oxygen::bindless::Generation;
  using Packed = NamedType<uint64_t, struct MaterialHandlePackedTag>;

  constexpr MaterialHandle() noexcept = default;
  constexpr MaterialHandle(Index index, Generation generation) noexcept
    : index_(index)
    , generation_(generation)
  {
  }

  [[nodiscard]] constexpr static auto FromPacked(Packed packed) noexcept
    -> MaterialHandle
  {
    constexpr uint64_t kLower32BitsMask = 0xFFFF'FFFFULL;
    const uint64_t raw = packed.get();
    const auto index = static_cast<uint32_t>(raw >> 32U);
    const auto generation = static_cast<uint32_t>(raw & kLower32BitsMask);
    return MaterialHandle { Index { index }, Generation { generation } };
  }

  [[nodiscard]] constexpr auto ToPacked() const noexcept -> Packed
  {
    const auto high = static_cast<uint64_t>(index_.get());
    const auto low = static_cast<uint64_t>(generation_.get());
    return Packed { (high << 32U) | low }; // NOLINT(*-magic-numbers)
  }

  [[nodiscard]] constexpr auto get() const noexcept -> uint32_t
  {
    return index_.get();
  }
  [[nodiscard]] constexpr auto IndexValue() const noexcept -> Index
  {
    return index_;
  }
  [[nodiscard]] constexpr auto GenerationValue() const noexcept -> Generation
  {
    return generation_;
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {
    return index_.get() != kInvalidBindlessIndex && generation_.get() != 0U;
  }

  constexpr auto operator<=>(const MaterialHandle& other) const noexcept
  {
    if (auto cmp = index_.get() <=> other.index_.get(); cmp != 0) {
      return cmp;
    }
    return generation_.get() <=> other.generation_.get();
  }
  [[nodiscard]] constexpr auto operator==(
    const MaterialHandle& other) const noexcept
  {
    return index_.get() == other.index_.get()
      && generation_.get() == other.generation_.get();
  }

private:
  Index index_ { Index { kInvalidBindlessIndex } };
  Generation generation_ { 0U };
};

//! Invalid MaterialHandle sentinel value.
inline constexpr MaterialHandle kInvalidMaterialHandle {};

constexpr auto to_string(MaterialHandle h)
{
  return "MatH(" + std::to_string(h.get()) + ":"
    + std::to_string(h.GenerationValue().get()) + ")";
}

//! Handle to a geometry entry managed by GeometryUploader.
/*!
 Assigned during collection via `GeometryUploader::GetOrAllocate(mesh)`.

 Geometry deduplication (identical content resolving to a single asset identity)
 is owned by the asset loader and its cache.

 GeometryUploader may perform lightweight interning: repeated requests for the
 same geometry identity (AssetKey, LOD index) return the same handle.
 GeometryUploader must not attempt runtime content hashing of vertex/index
 buffers.

 During finalization, handles resolve to GeometryUploader residency entries
 which provide vertex/index buffer SRV indices.

 Handles remain stable for the lifetime of the residency entry but may be
 recycled during long-running execution. Handle generation is part of identity
 and must be validated by GeometryUploader before use.
*/
class GeometryHandle {
public:
  using Index = NamedType<uint32_t, struct GeometryHandleIndexTag,
    // clang-format off
    Comparable,
    Printable
    >; // clang-format on
  using Generation = oxygen::bindless::Generation;
  using Packed = NamedType<uint64_t, struct GeometryHandlePackedTag>;

  constexpr GeometryHandle() noexcept = default;
  constexpr GeometryHandle(Index index, Generation generation) noexcept
    : index_(index)
    , generation_(generation)
  {
  }
  constexpr explicit GeometryHandle(uint32_t index) noexcept
    : index_(Index { index })
    , generation_(Generation { 1U })
  {
  }

  [[nodiscard]] constexpr static auto FromPacked(Packed packed) noexcept
    -> GeometryHandle
  {
    constexpr uint64_t kLower32BitsMask = 0xFFFF'FFFFULL;
    const uint64_t raw = packed.get();
    const auto index = static_cast<uint32_t>(raw >> 32U);
    const auto generation = static_cast<uint32_t>(raw & kLower32BitsMask);
    return GeometryHandle { Index { index }, Generation { generation } };
  }

  [[nodiscard]] constexpr auto ToPacked() const noexcept -> Packed
  {
    const auto high = static_cast<uint64_t>(index_.get());
    const auto low = static_cast<uint64_t>(generation_.get());
    return Packed { (high << 32U) | low }; // NOLINT(*-magic-numbers)
  }

  [[nodiscard]] constexpr auto get() const noexcept -> uint32_t
  {
    return index_.get();
  }
  [[nodiscard]] constexpr auto IndexValue() const noexcept -> Index
  {
    return index_;
  }
  [[nodiscard]] constexpr auto GenerationValue() const noexcept -> Generation
  {
    return generation_;
  }
  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {
    return index_.get() != kInvalidBindlessIndex && generation_.get() != 0U;
  }

  constexpr auto operator<=>(const GeometryHandle& other) const noexcept
  {
    if (auto cmp = index_.get() <=> other.index_.get(); cmp != 0) {
      return cmp;
    }
    return generation_.get() <=> other.generation_.get();
  }
  [[nodiscard]] constexpr auto operator==(
    const GeometryHandle& other) const noexcept
  {
    return index_.get() == other.index_.get()
      && generation_.get() == other.generation_.get();
  }

private:
  Index index_ { Index { kInvalidBindlessIndex } };
  Generation generation_ { 0U };
};

//! Invalid GeometryHandle sentinel value.
inline constexpr GeometryHandle kInvalidGeometryHandle {};

constexpr auto to_string(GeometryHandle h)
{
  return "GeoH(" + std::to_string(h.get()) + ":"
    + std::to_string(h.GenerationValue().get()) + ")";
}

} // namespace oxygen::engine::sceneprep
