//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

/*!
@file ResourceTypeList.h

## Compile-Time Type List and Type Indexing Utilities

Defines the generic compile-time type list and type-indexing utilities for
Oxygen Engine and general C++ metaprogramming. This file provides the `TypeList`
and `IndexOf` templates, which enable the creation of ordered type lists and
compile-time mapping from types to unique, stable, zero-based indices.

### Usage and Binary Compatibility Requirements

- Any set of types that require compile-time indexing or type-to-ID mapping
  should be listed in a single `TypeList` (e.g., `MyTypeList`).
- The order of types in the list determines their type index. **Never reorder
  existing types**; only append new types to the end to maintain binary
  compatibility for systems that depend on stable indices.
- Forward declare all types before defining the type list to avoid circular
  dependencies and enable use in headers.
- The type list must be visible to all code that needs to resolve type indices
  at compile time (e.g., registries, pools, handles, or other metaprogramming
  utilities).

### Example Usage

```cpp
// Forward declare all types
class Foo;
class Bar;
class Baz;

// Define the type list in a central header
using MyTypeList = oxygen::TypeList<
    Foo,
    Bar,
    Baz,
>;

// Get a type index at compile time
constexpr std::size_t foo_index = oxygen::IndexOf<Foo, MyTypeList>::value;
```

@warning Changing the order of types in the type list will break binary
         compatibility for all systems that depend on stable indices. Only
         append new types.
*/

namespace oxygen {

/*!
 Template metaprogramming-based resource type system. Provides compile-time
 unique resource type IDs without RTTI or runtime overhead.
*/
template <typename... Ts> struct TypeList { };

/*!
 Template metaprogramming helper to find the index of a type in a TypeList.

 Recursively searches through the TypeList at compile time and returns the
 zero-based index where the type is found. Generates a compile error if the type
 is not found, providing type safety.

 @tparam T The type to search for
 @tparam List The TypeList to search in
*/
template <typename T, typename List> struct IndexOf;

template <typename T, typename... Ts>
struct IndexOf<T, TypeList<T, Ts...>> : std::integral_constant<std::size_t, 0> {
};

template <typename T, typename U, typename... Ts>
struct IndexOf<T, TypeList<U, Ts...>>
  : std::integral_constant<std::size_t,
      1 + IndexOf<T, TypeList<Ts...>>::value> { };

/*!
 Template metaprogramming helper to apply a TypeList to a template.

 Transforms TypeList<A, B, C> into Template<A, B, C>, enabling the use of
 TypeList with other template metaprogramming utilities like std::tuple,
 std::variant, etc.

 @tparam Template The template to apply the TypeList to
 @tparam List The TypeList to apply

 ### Usage Examples

 ```cpp
 using MyTypes = TypeList<int, float, double>;
 using MyTuple = Apply_t<std::tuple, MyTypes>; // std::tuple<int, float, double>
 using MyVariant = Apply_t<std::variant, MyTypes>; // std::variant<int, float,
 double>
 ```
*/
template <template <typename...> class Template, typename List> struct Apply;

template <template <typename...> class Template, typename... Types>
struct Apply<Template, TypeList<Types...>> {
  using type = Template<Types...>;
};

//! Convenience alias for Apply<Template, List>::type
template <template <typename...> class Template, typename List>
using Apply_t = typename Apply<Template, List>::type;

} // namespace oxygen
