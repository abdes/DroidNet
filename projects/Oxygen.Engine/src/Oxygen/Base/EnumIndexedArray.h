//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <concepts>
#include <ranges>
#include <type_traits>

#include <compare>
#include <stdexcept>

#include <Oxygen/Base/NoStd.h>

namespace oxygen {

//! Concept: EnumWithCount
/*!
 Constrains enums used with helpers in this header. An enum satisfying
 `EnumWithCount` must expose `kFirst` and `kCount` enumerators where `kFirst`
 has underlying value `0` and `kCount` is greater than zero. This enables dense
 indexing and iteration across the enum range.

 @tparam E The enum type being tested.
*/
template<typename E>
concept EnumWithCount = std::is_enum_v<E> // must be an enum
  // Must have a count value, and it must be > 0
  && requires { { E::kCount } -> std::same_as<E>; }
  && (nostd::to_underlying(E::kCount) > 0)
  // must have a kFirst symbolic value, with underlying `0`
  && requires { { E::kFirst } -> std::same_as<E>; }
  && (nostd::to_underlying(E::kFirst) == 0);

//! EnumAsIndex: strongly-typed enum index wrapper
/*!
 A compact, strongly-typed index wrapper for enums that satisfy `EnumWithCount`.
 Use `EnumAsIndex<Enum>` to hold a numeric index derived from an enum while
 preserving type-safety and providing checked operations. The class supports
 constexpr usage, runtime-checked construction, and a consteval overload for
 compile-time validation.

 ### Usage Example

 ```cpp
 for (auto idx = EnumAsIndex<MyEnum>::Checked(MyEnum::kFirst);
      idx < EnumAsIndex<MyEnum>::end(); ++idx) {
   auto e = idx.to_enum();
 }
 ```

 @tparam Enum Enum type satisfying `EnumWithCount`.
*/
template <EnumWithCount Enum> class EnumAsIndex {
public:
  // Underlying integral type of the enum and the raw index type used to store
  // numeric indices. Keep these aliases to reduce repetitive casts and make
  // future migration easier.
  using RawIndexType = std::size_t;

  // Only allow construction from Enum
  constexpr explicit EnumAsIndex(Enum id) noexcept
    : value_(static_cast<RawIndexType>(id))
  {
    if (!std::is_constant_evaluated()) {
      if (value_ >= static_cast<RawIndexType>(Enum::kCount)) {
        std::terminate();
      }
    }
  }

  // Constexpr runtime-checked overload (keeps previous runtime behaviour). This
  // is selected by existing call sites that pass an Enum at runtime.
  [[nodiscard]] constexpr static EnumAsIndex Checked(Enum id) noexcept
  {
    return EnumAsIndex(id);
  }

  //! Consteval template overload for compile-time use. Call as
  //! `EnumAsIndex<MyEnum>::Checked<MyEnum::kFirst>()` to get a compile-time
  //! validated index without invoking the runtime-checking public ctor.
  template <Enum V> consteval static EnumAsIndex Checked()
  {
    static_assert(nostd::to_underlying(V) < nostd::to_underlying(Enum::kCount),
      "EnumAsIndex out of range");
    return EnumAsIndex(static_cast<RawIndexType>(nostd::to_underlying(V)), 0);
  }

  // Deleted default and raw-size constructors enforce invariants.
  constexpr EnumAsIndex() = delete;
  constexpr EnumAsIndex(RawIndexType) = delete;

  // Access underlying numeric index.
  [[nodiscard]] constexpr auto get() const noexcept { return value_; }

  [[nodiscard]] constexpr auto to_enum() const noexcept
  {
    return static_cast<Enum>(value_);
  }

  //! True when this index refers to a valid enum value (not the End()
  //! sentinel).
  [[nodiscard]] constexpr auto IsValid() const noexcept
  {
    return value_ < static_cast<RawIndexType>(Enum::kCount);
  }

  static constexpr EnumAsIndex begin() noexcept
  {
    return EnumAsIndex(static_cast<std::size_t>(Enum::kFirst), 0);
  }

  //! One-past-end sentinel to support idiomatic loops: for (EnumAsIndex<E>
  //! p{E::kFirst}; p < EnumAsIndex<E>::End(); ++p)
  static constexpr EnumAsIndex end() noexcept
  {
    return EnumAsIndex(static_cast<std::size_t>(Enum::kCount), 0);
  }

  // Explicit three-way compare (portable across toolchains).
  constexpr std::strong_ordering operator<=>(EnumAsIndex other) const noexcept
  {
    return value_ <=> other.value_;
  }

  // Pre-increment
  constexpr EnumAsIndex& operator++() noexcept
  {
    value_ = CheckedFromValueAndOffset(value_, 1);
    return *this;
  }

  // Post-increment
  constexpr EnumAsIndex operator++(int) noexcept
  {
    EnumAsIndex tmp = *this;
    ++(*this);
    return tmp;
  }

  // Pre-decrement
  constexpr EnumAsIndex& operator--() noexcept
  {
    value_ = CheckedFromValueAndOffset(value_, -1);
    return *this;
  }

  // Post-decrement
  constexpr EnumAsIndex operator--(int) noexcept
  {
    EnumAsIndex tmp = *this;
    --(*this);
    return tmp;
  }

  constexpr auto& operator+=(std::ptrdiff_t off) noexcept
  {
    value_ = CheckedFromValueAndOffset(value_, off);
    return *this;
  }

  constexpr auto& operator-=(std::ptrdiff_t off) noexcept
  {
    value_ = CheckedFromValueAndOffset(value_, -off);
    return *this;
  }

  [[nodiscard]] friend constexpr auto operator+(
    EnumAsIndex p, std::ptrdiff_t off) noexcept
  {
    return EnumAsIndex(CheckedFromValueAndOffset(p.get(), off), 0);
  }

  [[nodiscard]] friend constexpr auto operator-(
    EnumAsIndex p, std::ptrdiff_t off) noexcept
  {
    return EnumAsIndex(CheckedFromValueAndOffset(p.get(), -off), 0);
  }

  //! For algorithms that need 'how many steps apart'; enables
  //! `std::ranges::distance`-style usage.
  [[nodiscard]] friend constexpr auto operator-(
    EnumAsIndex a, EnumAsIndex b) noexcept
  {
    return static_cast<std::ptrdiff_t>(a.value_)
      - static_cast<std::ptrdiff_t>(b.value_);
  }

private:
  RawIndexType value_;

  // Private constructor from raw value to allow creating the one-past-end
  // sentinel (value == Enum::kCount).
  constexpr explicit EnumAsIndex(std::size_t raw, int) noexcept
    : value_(raw)
  {
  }

  // Helper to validate a raw signed value and cast to the RawIndexType. This
  // centralizes the consteval vs runtime handling so we don't duplicate the
  // same branches in every operator implementation.
  static constexpr RawIndexType CheckedFromValueAndOffset(
    RawIndexType value, std::ptrdiff_t off) noexcept
  {
    const auto raw = static_cast<std::ptrdiff_t>(value) + off;
    if (!std::is_constant_evaluated()) {
      if (!(raw >= 0 && raw <= static_cast<std::ptrdiff_t>(Enum::kCount))) {
        std::terminate();
      }
    }
    return static_cast<RawIndexType>(raw);
  }
};

//! Class template deduction guide: allow `EnumAsIndex{MyEnum::kFirst}` to
//! deduce the template parameter and prefer the enum-based constructor.
template <EnumWithCount E> EnumAsIndex(E) -> EnumAsIndex<E>;

//! Namespace-visible equality for EnumAsIndex templates. Placed at namespace
//! scope so ADL/lookup finds it in contexts where std::equal_to is used.
template <EnumWithCount E>
constexpr bool operator==(
  EnumAsIndex<E> const& a, EnumAsIndex<E> const& b) noexcept
{
  return a.get() == b.get();
}

} // namespace oxygen

// Implement a full iterator + view that yields `EnumAsIndex<Enum>` values.
namespace oxygen {

//! EnumAsIndexIterator
/*!
 Iterator adapter that yields `EnumAsIndex<Enum>` values. Models a random-access
 iterator and is compatible with `EnumAsIndexView`. Use this iterator when
 iterating enum ranges in algorithms or range-based for loops.

 @tparam Enum Enum type satisfying `EnumWithCount`.
*/
template <EnumWithCount Enum> class EnumAsIndexIterator {
public:
  using value_type = EnumAsIndex<Enum>;
  using difference_type = std::ptrdiff_t;
  using reference = value_type; // dereference returns prvalue wrapper
  using pointer = void;
  using iterator_category = std::random_access_iterator_tag;
  using iterator_concept = std::random_access_iterator_tag;

  constexpr EnumAsIndexIterator() noexcept = default;
  constexpr EnumAsIndexIterator(EnumAsIndexIterator const&) noexcept = default;
  constexpr EnumAsIndexIterator(EnumAsIndexIterator&&) noexcept = default;
  constexpr EnumAsIndexIterator& operator=(EnumAsIndexIterator const&) noexcept
    = default;
  constexpr EnumAsIndexIterator& operator=(EnumAsIndexIterator&&) noexcept
    = default;
  constexpr explicit EnumAsIndexIterator(std::size_t raw) noexcept
    : raw_(raw)
  {
  }

  constexpr reference operator*() const noexcept
  {
    return EnumAsIndex<Enum>::Checked(static_cast<Enum>(raw_));
  }

  constexpr EnumAsIndexIterator& operator++() noexcept
  {
    ++raw_;
    return *this;
  }
  constexpr EnumAsIndexIterator operator++(int) noexcept
  {
    auto tmp = *this;
    ++*this;
    return tmp;
  }
  constexpr EnumAsIndexIterator& operator--() noexcept
  {
    --raw_;
    return *this;
  }
  constexpr EnumAsIndexIterator operator--(int) noexcept
  {
    auto tmp = *this;
    --*this;
    return tmp;
  }

  constexpr EnumAsIndexIterator& operator+=(difference_type off) noexcept
  {
    raw_ = static_cast<std::size_t>(static_cast<difference_type>(raw_) + off);
    return *this;
  }
  constexpr EnumAsIndexIterator& operator-=(difference_type off) noexcept
  {
    raw_ = static_cast<std::size_t>(static_cast<difference_type>(raw_) - off);
    return *this;
  }

  friend constexpr EnumAsIndexIterator operator+(
    EnumAsIndexIterator it, difference_type off) noexcept
  {
    it += off;
    return it;
  }
  friend constexpr EnumAsIndexIterator operator-(
    EnumAsIndexIterator it, difference_type off) noexcept
  {
    it -= off;
    return it;
  }
  friend constexpr difference_type operator-(
    EnumAsIndexIterator a, EnumAsIndexIterator b) noexcept
  {
    return static_cast<difference_type>(a.raw_)
      - static_cast<difference_type>(b.raw_);
  }

  constexpr std::strong_ordering operator<=>(
    EnumAsIndexIterator other) const noexcept
  {
    return raw_ <=> other.raw_;
  }
  friend constexpr bool operator==(
    EnumAsIndexIterator a, EnumAsIndexIterator b) noexcept
  {
    return a.raw_ == b.raw_;
  }

private:
  std::size_t raw_ = 0;
};

//! EnumAsIndexView
/*!
 Range view that yields `EnumAsIndex<Enum>` across the enum's valid values.
 Prefer `enum_as_index<Enum>` for a convenient constexpr instance usable in
 range-based loops and algorithms.

 ### Example Usage:

 ```cpp
   for (auto idx : enum_as_index<MyEnum>) {
     // idx is EnumAsIndex<MyEnum>
   }
     ```

 @tparam Enum Enum type satisfying `EnumWithCount`.
*/
template <EnumWithCount Enum>
class EnumAsIndexView
  : public std::ranges::view_interface<EnumAsIndexView<Enum>> {
public:
  using iterator = EnumAsIndexIterator<Enum>;
  using sentinel = iterator;

  constexpr EnumAsIndexView() = default;
  constexpr EnumAsIndexView(EnumAsIndexView const&) noexcept = default;
  constexpr EnumAsIndexView(EnumAsIndexView&&) noexcept = default;
  constexpr EnumAsIndexView& operator=(EnumAsIndexView const&) noexcept
    = default;
  constexpr EnumAsIndexView& operator=(EnumAsIndexView&&) noexcept = default;
  constexpr iterator begin() noexcept
  {
    return iterator(static_cast<std::size_t>(Enum::kFirst));
  }
  constexpr iterator end() noexcept
  {
    return iterator(static_cast<std::size_t>(Enum::kCount));
  }
  constexpr iterator begin() const noexcept
  {
    return iterator(static_cast<std::size_t>(Enum::kFirst));
  }
  constexpr iterator end() const noexcept
  {
    return iterator(static_cast<std::size_t>(Enum::kCount));
  }
  constexpr std::size_t size() const noexcept
  {
    return static_cast<std::size_t>(Enum::kCount);
  }
  constexpr bool empty() const noexcept { return size() == 0; }
};

} // namespace oxygen

namespace std::ranges {
template <oxygen::EnumWithCount E>
inline constexpr bool enable_view<oxygen::EnumAsIndexView<E>> = true;

template <oxygen::EnumWithCount E>
inline constexpr bool enable_borrowed_range<oxygen::EnumAsIndexView<E>> = true;
}

//! `enum_as_index` convenience variable
/*!
 Constexpr view instance that yields `EnumAsIndex<Enum>`. Use in range-based
 loops to iterate enum values safely and readably.
*/
template <oxygen::EnumWithCount Enum>
inline constexpr auto enum_as_index = oxygen::EnumAsIndexView<Enum> {};

//! Provide std::hash specialization for EnumAsIndex so it can be used in hashed
//! containers. This is a template so any EnumAsIndex<Enum> is hashable.
template <typename Enum> struct std::hash<oxygen::EnumAsIndex<Enum>> {
  constexpr std::size_t operator()(oxygen::EnumAsIndex<Enum> idx) const noexcept
  {
    return static_cast<std::size_t>(idx.get());
  }
}; // namespace std

namespace oxygen {

//! EnumIndexedArray
/*!
 Lightweight wrapper around `std::array` that lets callers index using an enum
 type (or a dedicated index wrapper) without manual casts. The array size is
 derived from `Enum::kCount` via the `enum_as_index` view to avoid mismatches
 and keep declarations concise.

 Key Features
 - Strongly-typed enum indexing without casts.
 - `at()` overloads that perform range checks and throw `std::out_of_range`.
 - Overloads supporting `Enum`, `EnumAsIndex<Enum>`, numeric indices, and any
   named index type exposing a `get()` returning `std::size_t`.

 Usage Example
 ```cpp
 enum class MyEnum { kFirst = 0, A = 0, B = 1, kCount = 2 };
 Oxygen::EnumIndexedArray<MyEnum, int> arr{};
 arr[MyEnum::A] = 10;
 auto v = arr.at(MyEnum::B);
 ```

 Performance Characteristics
 - Time Complexity: O(1) for index and `at()` access (checked).
 - Memory: same as underlying `std::array<T, N>`.
 - Optimization: Avoids casts and improves readability; `operator[]` overloads
   are noexcept and suitable for hot paths.

 @tparam Enum Enum type satisfying `EnumWithCount`.
 @tparam T Value type stored in the array.
*/
template <EnumWithCount Enum, typename T> struct EnumIndexedArray {
  // Derive array size from the enum's kCount to avoid redundant template
  // parameters and accidental mismatches. Prefer deriving the size from the
  // enum_as_index view so the array size always matches the view's size.
  // std::ranges::size on the view is constexpr and maps directly to
  // Enum::kCount, but using the view makes the relationship explicit and less
  // error-prone.
  std::array<T, std::ranges::size(enum_as_index<Enum>)> data;

  // Index with enum (use underlying_type for portability)
  constexpr T& operator[](Enum e) noexcept
  {
    return data[static_cast<std::size_t>(nostd::to_underlying(e))];
  }
  constexpr T const& operator[](Enum e) const noexcept
  {
    return data[static_cast<std::size_t>(nostd::to_underlying(e))];
  }

  // Index with numeric index
  constexpr T& operator[](std::size_t i) noexcept { return data[i]; }
  constexpr T const& operator[](std::size_t i) const noexcept
  {
    return data[i];
  }

  // Checked accessors that match std::array::at(): throw std::out_of_range on
  // invalid index. Provide overloads for numeric indices, enum values and the
  // EnumAsIndex wrapper as well as named-index types exposing `get()`.
  constexpr T& at(std::size_t i)
  {
    if (i >= data.size()) {
      throw std::out_of_range("EnumIndexedArray::at: index out of range");
    }
    return data[i];
  }

  constexpr T const& at(std::size_t i) const
  {
    if (i >= data.size()) {
      throw std::out_of_range("EnumIndexedArray::at: index out of range");
    }
    return data[i];
  }

  constexpr T& at(Enum e)
  {
    return at(static_cast<std::size_t>(nostd::to_underlying(e)));
  }

  constexpr T const& at(Enum e) const
  {
    return at(static_cast<std::size_t>(nostd::to_underlying(e)));
  }

  constexpr T& at(EnumAsIndex<Enum> idx)
  {
    auto i = idx.get();
    if (i >= data.size()) {
      throw std::out_of_range("EnumIndexedArray::at: index out of range");
    }
    return data[i];
  }

  constexpr T const& at(EnumAsIndex<Enum> idx) const
  {
    auto i = idx.get();
    if (i >= data.size()) {
      throw std::out_of_range("EnumIndexedArray::at: index out of range");
    }
    return data[i];
  }

  template <typename NamedIndex>
  constexpr T& at(NamedIndex idx)
    requires requires(NamedIndex n) {
      { n.get() } -> std::convertible_to<std::size_t>;
    }
  {
    return at(idx.get());
  }

  template <typename NamedIndex>
  constexpr T const& at(NamedIndex idx) const
    requires requires(NamedIndex n) {
      { n.get() } -> std::convertible_to<std::size_t>;
    }
  {
    return at(idx.get());
  }

  // Support a named index wrapper with `get()` accessor (see PhaseIndex below)
  template <typename NamedIndex>
  constexpr T& operator[](NamedIndex idx) noexcept
  {
    return data[idx.get()];
  }
  template <typename NamedIndex>
  constexpr T const& operator[](NamedIndex idx) const noexcept
  {
    return data[idx.get()];
  }

  // Explicit overload for the enum-index wrapper (EnumAsIndex<Enum>) with a
  // debug assertion to prevent accidental indexing using the End() sentinel.
  // This is a no-op in release builds but helps catch sentinel misuse during
  // development. This overload is generic for any EnumAsIndex matching the
  // array's Enum template parameter.
  constexpr T& operator[](EnumAsIndex<Enum> idx) noexcept
  {
    return data[idx.get()];
  }
  constexpr T const& operator[](EnumAsIndex<Enum> idx) const noexcept
  {
    return data[idx.get()];
  }

  constexpr auto size() const noexcept { return data.size(); }
  constexpr auto begin() noexcept { return data.begin(); }
  constexpr auto end() noexcept { return data.end(); }
  constexpr auto begin() const noexcept { return data.begin(); }
  constexpr auto end() const noexcept { return data.end(); }
};

} // namespace oxygen
