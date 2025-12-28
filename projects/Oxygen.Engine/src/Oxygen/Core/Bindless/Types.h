//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Bindless/Generated.Constants.h>
#include <Oxygen/Core/api_export.h>

namespace oxygen {

//! Strong type representing an index into a bindless heap, managed by an engine
//! descriptor allocator.
/*!
 The underlying index cannot be assumed to be unique outside its corresponding
 `heap`, and the interpretation of `heap` is allocator specific.

 This is not to be confused with a GPU/CPU descriptor address, or a
 shader-visible index. When needed, the original allocator, when also provided
 with enough information to identify the corresponding heap (resource type,
 visibility, etc...) will provide the appropriate mapping.

 @see ShaderVisibleIndex
*/
using BindlessHeapIndex = NamedType<uint32_t, struct BindlessHeapIndexTag,
  // clang-format off
  Hashable,
  Comparable,
  Printable>; // clang-format on

//! Strongly-typed shader-visible bindless index (32-bit).
/*!
 Represents the index used in shaders to access a bindless resource from a
 specific bindless descriptor table. This is distinct from `BindlessHeapIndex`,
 which is a backend specific index used by the Graphics backend descriptor
 allocators. The mapping between `BindlessHeapIndex` and `ShaderVisibleIndex` is
 managed by the allocator.

 @warning No assumptions should be made about the algorithm used for deriving a
 `ShaderVisibleIndex` from a `BindlessHeapIndex`.

 @see BindlessHeapIndex
*/
using ShaderVisibleIndex = NamedType<uint32_t, struct ShaderVisibleIndexTag,
  // clang-format off
  Hashable,
  Comparable,
  Printable>; // clang-format on

//! Strong type representing a count of bindless items (descriptors, indices,
//! etc.).
/*!
 This strong type is particularly useful to disambiguate size/count parameters
 and return types from indices or capacity. Its underlying type is the same as
 `BindlessHeapIndex` to guarantee consistent bounds and semantics.

 @see BindlessHeapIndex, BindlessHeapCapacity
*/
using BindlessItemCount = NamedType<uint32_t, struct BindlessItemCountTag,
  // clang-format off
    DefaultInitialized,
    PreIncrementable,
    PostIncrementable,
    Addable,
    Subtractable,
    Comparable,
    Printable,
    Hashable>; // clang-format on

//! Strong type representing the capacity of an allocator or a container of
//! bindless handles.
/*!
 Its underlying type is the same as `BindlessHeapIndex` to guarantee consistent
 bounds and semantics.

 @see BindlessHeapIndex, BindlessItemCount
*/
using BindlessHeapCapacity = NamedType<uint32_t, struct BindlessHeapCapacityTag,
  // clang-format off
  DefaultInitialized,
  Addable,
  Subtractable,
  Comparable,
  Printable,
  Hashable>; // clang-format on

//! Sentinel value representing an invalid bindless handle.
static constexpr BindlessHeapIndex kInvalidBindlessHeapIndex {
  kInvalidBindlessIndex
};

//! Sentinel value representing an invalid bindless handle.
static constexpr ShaderVisibleIndex kInvalidShaderVisibleIndex {
  kInvalidBindlessIndex
};

//! Convert a BindlessHeapIndex to a human-readable string representation.
OXGN_CORE_NDAPI auto to_string(BindlessHeapIndex h) -> std::string;

//! Convert a ShaderVisibleIndex to a human-readable string
//! representation.
OXGN_CORE_NDAPI auto to_string(ShaderVisibleIndex h) -> std::string;

//! Convert a BindlessItemCount to a human-readable string representation.
OXGN_CORE_NDAPI auto to_string(BindlessItemCount count) -> std::string;

//! Convert a BindlessHeapCapacity to a human-readable string representation.
OXGN_CORE_NDAPI auto to_string(BindlessHeapCapacity capacity) -> std::string;

// Forward declaration for the hasher, so that we can use it for the Hasher type
// alias
struct VersionedBindlessHandleHash;

//! CPU-side versioned handle pairing index with generation counter.
/*!
 Combines a shader-visible bindless index with a CPU-side generation counter to
 detect stale or recycled indices. Use the generation counter to detect when an
 index has been reused by the allocator and avoid use-after-free bugs.

 ### Key features:
 - Index-first ordering: comparisons order by index, then generation.
 - Packed transport: a nested `Packed` NamedType wraps the raw uint64_t
   representation for serialization/deserialization.
 - Strong typing: `Generation` is a scoped NamedType to prevent mixing values
   with other integer types.
 - Constexpr-friendly: construction and packing/unpacking are constexpr.

 ### Usage example:
 ```cpp
 VersionedBindlessHandle h{BindlessHeapIndex{42},
 VersionedBindlessHandle::Generation{1}}; auto packed = h.ToPacked(); // returns
 VersionedBindlessHandle::Packed auto restored =
 VersionedBindlessHandle::FromPacked(packed);
 ```

 @warning Do not use the packed format as a long-term on-disk layout without
          explicit versioning; the representation is an implementation detail
          and may change.
*/
class VersionedBindlessHandle {
public:
  //! Packed transport type for serialized uint64_t values.
  /*!
   This nested NamedType wraps the raw packed uint64_t and intentionally does
   not provide hashing or comparison behavior. Use
   `VersionedBindlessHandle::FromPacked(...)` to obtain the logical structure
   for comparisons or hashing.
  */
  using Packed = NamedType<uint64_t, struct PackedTag>;

  //! Strongly-typed generation counter for versioned handles.
  using Generation = NamedType<uint32_t, struct GenerationTag,
    // clang-format off
    PreIncrementable,
    PostIncrementable,
    Addable,
    Comparable,
    Printable>; // clang-format on

  //! Hasher alias to improve discoverability, and ergonomics when using the
  //! class in a generic context.
  /*!
   ### Usage Example
   ```cpp
   // Simple
   std::unordered_set<VersionedBindlessHandle, VersionedBindlessHandle::Hasher>
   // Or generic also works across many types
   std::unordered_set<T, typename T::Hasher>
   ```
  */
  using Hasher = VersionedBindlessHandleHash;

  constexpr VersionedBindlessHandle() noexcept = default;

  //! Construct a versioned handle from index and generation.
  /*!
   @param idx The shader-visible bindless index
   @param gen The generation counter for staleness detection
  */
  constexpr VersionedBindlessHandle(
    BindlessHeapIndex idx, Generation gen) noexcept
    : index_(idx)
    , generation_(gen)
  {
  }

  // Rule of Five: defaulted destructor, copy and move special members.
  OXYGEN_DEFAULT_COPYABLE(VersionedBindlessHandle)
  OXYGEN_DEFAULT_MOVABLE(VersionedBindlessHandle)

  ~VersionedBindlessHandle() = default;

  //! Unpack a versioned handle from its 64-bit representation.
  /*!
   @param p Packed representation with index in high 32 bits, generation in low
   @return Unpacked versioned handle
  */
  [[nodiscard]] static constexpr auto FromPacked(Packed p) noexcept
  {
    constexpr uint64_t kLower32BitsMask = 0xFFFF'FFFFU;
    const uint64_t raw = p.get();
    const uint32_t index = static_cast<uint32_t>(raw >> 32);
    const uint32_t generation = static_cast<uint32_t>(raw & kLower32BitsMask);

    return VersionedBindlessHandle {
      BindlessHeapIndex { index },
      Generation { generation },
    };
  }

  //! Pack this handle into a 64-bit representation for storage.
  /*!
   @return Packed value with index in high 32 bits, generation in low 32 bits
  */
  [[nodiscard]] constexpr auto ToPacked() const noexcept
  {
    const uint64_t high = index_.get();
    const uint64_t low = generation_.get();
    return Packed { (high << 32) | low };
  }

  //! Extract the shader-visible bindless index.
  [[nodiscard]] constexpr auto ToBindlessHandle() const noexcept
  {
    return index_;
  }

  //! Get the generation counter value.
  [[nodiscard]] constexpr auto GenerationValue() const noexcept
  {
    return generation_;
  }

  //! Check if this handle represents a valid (non-sentinel) index.
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return static_cast<uint32_t>(index_.get()) != kInvalidBindlessIndex;
  }

  //! Three-way comparison: orders by index first, then generation.
  constexpr auto operator<=>(
    const VersionedBindlessHandle& other) const noexcept
  {
    const auto lhs_idx = index_.get();
    const auto rhs_idx = other.index_.get();
    if (auto cmp = lhs_idx <=> rhs_idx; cmp != 0) {
      return cmp;
    }
    const auto lhs_gen = generation_.get();
    const auto rhs_gen = other.generation_.get();
    return lhs_gen <=> rhs_gen;
  }

  [[nodiscard]] constexpr auto operator==(
    const VersionedBindlessHandle& other) const noexcept
  {
    return index_.get() == other.index_.get()
      && generation_.get() == other.generation_.get();
  }

private:
  BindlessHeapIndex index_ { kInvalidBindlessHeapIndex };
  Generation generation_ { 0U };
};

//! Convert a VersionedBindlessHandle to a human-readable string.
OXGN_CORE_NDAPI auto to_string(const VersionedBindlessHandle& h) -> std::string;

//! Convert a VersionedBindlessHandle::Generation to a human-readable string.
OXGN_CORE_NDAPI auto to_string(VersionedBindlessHandle::Generation gen)
  -> std::string;

//! Explicit hasher for VersionedBindlessHandle.
/*!
 The packed 64-bit representation of this type is an implementation detail and
 may change; to avoid silently coupling callers to that representation, creating
 ODR/ABI surprises, or enabling implicit global behavior via a `std::hash`
 specialization, hashing is kept explicit and opt-in — use this functor when you
 need a version-aware hash (index and generation) for unordered containers.
 */
struct VersionedBindlessHandleHash {
  //! Hash a versioned handle using its packed representation.
  [[nodiscard]] auto operator()(const VersionedBindlessHandle& h) const noexcept
  {
    // Hash the underlying packed numeric value.
    return std::hash<uint64_t> {}(h.ToPacked().get());
  }
};

//! Explicit namespace with concise aliases for the bindless numeric types to
//! improve ergonomic at use sites.
namespace bindless {
  using HeapIndex = BindlessHeapIndex;
  using ShaderVisibleIndex = ShaderVisibleIndex;
  using VersionedHandle = VersionedBindlessHandle;
  using Count = BindlessItemCount;
  using Capacity = BindlessHeapCapacity;
  using Generation = VersionedBindlessHandle::Generation;

  //! Maximum exclusive bindless handle value. This sentinel marks the upper
  //! bound (exclusive) for shader-visible bindless indices and is chosen to
  //! match the underlying 32-bit storage.
  inline constexpr auto kMaxHeapIndex = HeapIndex {
    (std::numeric_limits<uint32_t>::max)(),
  };

  //! Maximum exclusive count of bindless handles. Valid counts are in the
  //! range [0, kMaxCount). Matches the underlying 32-bit storage.
  inline constexpr auto kMaxCount = Count {
    (std::numeric_limits<uint32_t>::max)(),
  };

  //! Maximum exclusive capacity for bindless handle containers/allocators.
  //! Valid capacities are in the range [0, kMaxCapacity). Matches the
  //! underlying 32-bit storage.
  inline constexpr auto kMaxCapacity = Capacity {
    (std::numeric_limits<uint32_t>::max)(),
  };
} // namespace bindless

} // namespace oxygen
