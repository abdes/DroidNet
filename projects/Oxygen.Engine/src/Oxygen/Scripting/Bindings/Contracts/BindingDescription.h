//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <string_view>

struct lua_State;

namespace oxygen::scripting::bindings {

//! A Lua C function, as `lua_pushcfunction` takes it.
using LuaFunction = int (*)(lua_State*);

//! A function a binding pack exposes, with its Luau type.
/*!
 One row both registers the function and describes it, so a binding cannot
 be registered without a signature. `signature` is a Luau function type such
 as `(name: string) -> SceneNode?`; a method's signature leaves out `self`.
 `name` must have static storage: Luau keeps it as the function's debug name.
*/
struct BindingFunction {
  const char* name = nullptr;
  LuaFunction function = nullptr;
  std::string_view signature;
};

//! A non-function member with its Luau type: a constant, a nested record, or
//! a property a userdata resolves through `__index`.
struct BindingField {
  std::string_view name;
  std::string_view type;
};

//! A named Luau type a pack declares, such as an options record or a table a
//! function returns.
struct BindingTypeAlias {
  std::string_view name;
  //! The type, as written after `type <name> =`.
  std::string_view definition;
};

//! A userdata type whose methods and properties resolve through a metatable.
/*!
 `metatable` is the registry name the type's metatable is created under, so
 the coverage test can compare the live metatable with this description.
*/
struct BindingClass {
  std::string_view name;
  std::string_view metatable;
  std::span<const BindingFunction> methods;
  std::span<const BindingField> properties;
  //! Operators as Luau extern-type metamethods, such as `__mul`; signatures
  //! leave out `self`.
  std::span<const BindingFunction> metamethods;
};

//! What one registration adds under `oxygen`.
/*!
 `path` is the dotted member path below `oxygen`: empty for `oxygen` itself,
 `time` for `oxygen.time`. Classes and type aliases are global Luau types.
*/
struct BindingNamespaceDescription {
  std::string_view path;
  std::span<const BindingFunction> functions;
  std::span<const BindingField> fields;
  std::span<const BindingClass> classes;
  std::span<const BindingTypeAlias> types;
};

using NamespaceRegisterFn = void (*)(lua_State* state, int oxygen_table_index);
using NamespaceDescribeFn
  = auto (*)() -> std::span<const BindingNamespaceDescription>;

//! One registration under `oxygen` and the description of what it adds.
/*!
 `describe` returns the Luau types of everything `register_fn` adds. It is
 null only for a namespace declared untyped (`any`) in the generated
 definitions.
*/
struct BindingNamespace {
  const char* name { nullptr };
  NamespaceRegisterFn register_fn { nullptr };
  NamespaceDescribeFn describe { nullptr };
};

} // namespace oxygen::scripting::bindings
