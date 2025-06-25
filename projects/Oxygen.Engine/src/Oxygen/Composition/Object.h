//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Compilers.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/TypeSystem.h>
#include <Oxygen/Composition/api_export.h>
#include <string_view>

namespace oxygen {

//! Abstract base class for all type-aware objects in the Oxygen Engine.
/*!
 Provides a uniform interface for runtime type identification and reflection.
 All components, resources, and compositions derive from `Object` to enable
 type-safe queries and dynamic dispatch without relying on C++ RTTI.

 ### Key Features
 - **Type Identification**: Requires derived classes to implement `GetTypeId`,
   `GetTypeName`, and `GetTypeNamePretty` for runtime type queries.
 - **Copy/Move Support**: Supports copy and move semantics for flexible object
   management.

 ### Usage Patterns
 - Inherit from `Object` for any class that participates in the Oxygen type
   system.
 - Use the `OXYGEN_TYPED` macro to implement required type methods.

 @see OXYGEN_TYPED, Component, Composition
*/
class Object {
public:
  OXGN_COM_API Object();
  OXGN_COM_API virtual ~Object();

  OXYGEN_DEFAULT_COPYABLE(Object)
  OXYGEN_DEFAULT_MOVABLE(Object)

  [[nodiscard]] virtual auto GetTypeId() const noexcept -> TypeId = 0;
  [[nodiscard]] virtual auto GetTypeName() const noexcept -> std::string_view
    = 0;
  [[nodiscard]] virtual auto GetTypeNamePretty() const noexcept
    -> std::string_view
    = 0;
};

} // namespace oxygen

/*!
 @def OXYGEN_TYPED(arg_type)
 Declares all required type information and registration hooks for a class to be
 used in the Oxygen Engine type system. This macro must be placed inside the
 declaration of a class derived from `oxygen::Object`.

 @param arg_type The class name (required).

 ### Example Usage
 ```cpp
 class MyObject : public oxygen::Object {
   OXYGEN_TYPED(MyObject)
   // ...
 };
 ```

 ### Generated Code
 - Declares static and virtual type information accessors (`ClassTypeId`,
   `ClassTypeName`, `ClassTypeNamePretty`, `GetTypeId`, `GetTypeName`,
   `GetTypeNamePretty`)
 - Registers the type with the Oxygen type registry

 @see Object, Component, Composition
*/

#if defined(OXYGEN_MSVC_VERSION)
#  define OXYGEN_TYPE_NAME_IMPL() __FUNCSIG__
#elif defined(OXYGEN_GNUC_VERSION_CHECK) || defined(OXYGEN_CLANG_VERSION)
#  define OXYGEN_TYPE_NAME_IMPL_() __PRETTY_FUNCTION__
#else
#  define OXYGEN_TYPE_NAME_IMPL_() #arg_type
#endif

#define OXYGEN_TYPED(arg_type)                                                 \
public:                                                                        \
  inline static constexpr auto ClassTypeName() noexcept                        \
  {                                                                            \
    return OXYGEN_TYPE_NAME_IMPL();                                            \
  }                                                                            \
  inline static auto ClassTypeNamePretty() noexcept -> std::string_view        \
  {                                                                            \
    static std::string pretty {};                                              \
    if (pretty.empty()) {                                                      \
      pretty                                                                   \
        = ::oxygen::TypeRegistry::ExtractQualifiedClassName(ClassTypeName());  \
    }                                                                          \
    return pretty;                                                             \
  }                                                                            \
  inline static auto ClassTypeId() noexcept -> ::oxygen::TypeId                \
  {                                                                            \
    static ::oxygen::TypeId typeId                                             \
      = ::oxygen::TypeRegistry::Get().RegisterType(arg_type::ClassTypeName()); \
    return typeId;                                                             \
  }                                                                            \
  auto GetTypeName() const noexcept -> std::string_view override               \
  {                                                                            \
    return ClassTypeName();                                                    \
  }                                                                            \
  auto GetTypeNamePretty() const noexcept -> std::string_view override         \
  {                                                                            \
    return ClassTypeNamePretty();                                              \
  }                                                                            \
  inline auto GetTypeId() const noexcept -> ::oxygen::TypeId override          \
  {                                                                            \
    return ClassTypeId();                                                      \
  }                                                                            \
                                                                               \
private:
