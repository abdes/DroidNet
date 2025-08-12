//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/Typed.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

//! Global type registry for mapping type names to unique TypeId values.
/*!
  ### Key Features
  - **Thread-safe**: All operations are safe for concurrent use.
  - **Idempotent registration**: Registering the same name returns the same
    TypeId.
  - **Fast lookup**: Efficient mapping from name to TypeId and vice versa.
  - **DLL-safe**: Designed for use across DLL boundaries.

  ### Usage Patterns
  Register a type and retrieve its TypeId:
  ```cpp
  auto id = oxygen::TypeRegistry::Get().RegisterType("MyType");
  ```
  Look up a type name from a TypeId:
  ```cpp
  auto name = oxygen::TypeRegistry::Get().GetTypeName(id);
  ```

  ### Architecture Notes
  - Type names are stored as string views; the registry owns the storage.
  - Not intended for direct use in hot loops; prefer caching TypeId values.

  @warning Do not use smart pointers for Impl; raw pointer is required for
           DLL boundary safety.

  @see RegisterType, GetTypeId, GetTypeName, GetTypeNamePretty
*/
class TypeRegistry {
public:
  //! Get the global TypeRegistry singleton instance.
  OXGN_COM_API static auto Get() -> TypeRegistry&;

  //! Construct a TypeRegistry (use Get() in practice).
  OXGN_COM_API TypeRegistry();

  //! Destroy the TypeRegistry.
  OXGN_COM_API ~TypeRegistry();

  OXYGEN_MAKE_NON_COPYABLE(TypeRegistry)
  OXYGEN_DEFAULT_MOVABLE(TypeRegistry)

  //! Register a type name and return its unique TypeId.
  /*!
   @param name Null-terminated type name string
   @return Unique TypeId for the given name

   @note Thread-safe and idempotent.
  */
  OXGN_COM_API auto RegisterType(const char* name) const -> TypeId;

  //! Look up the TypeId for a registered type name.
  /*!
   @param name Null-terminated type name string
   @return TypeId for the name, or kInvalidTypeId if not found

   @note Thread-safe.
  */
  OXGN_COM_API auto GetTypeId(const char* name) const -> TypeId;

  //! Look up the registered type name for a TypeId.
  /*!
   @param id TypeId to look up
   @return Registered type name, or empty string_view if not found

   @note Thread-safe.
  */
  OXGN_COM_API auto GetTypeName(TypeId id) const -> std::string_view;

  //! Get a pretty-printed (demangled) type name for a TypeId.
  /*!
   @param id TypeId to look up
   @return Demangled type name, or empty string_view if not found

   @note Thread-safe.
  */
  OXGN_COM_API auto GetTypeNamePretty(TypeId id) const -> std::string_view;

  //! Extract the qualified class name from a compiler signature string.
  /*!
   @param signature Compiler-generated function or type signature
   @return Qualified class name substring

   @note Used internally for demangling.
  */
  OXGN_COM_API static auto ExtractQualifiedClassName(
    std::string_view signature) noexcept -> std::string_view;

private:
  class Impl;
  // Raw pointer, because smart pointers do not work well across DLL boundaries.
  Impl* impl_;
};

} // namespace oxygen
