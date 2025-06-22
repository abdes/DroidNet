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

namespace oxygen {

class Object {
public:
  OXGN_COM_API Object();
  OXGN_COM_API virtual ~Object();

  // All components should implement proper copy and move semantics to handle
  // copying and moving as appropriate.
  OXYGEN_DEFAULT_COPYABLE(Object)
  OXYGEN_DEFAULT_MOVABLE(Object)

  [[nodiscard]] virtual auto GetTypeId() const -> TypeId = 0;
  [[nodiscard]] virtual auto GetTypeName() const -> const char* = 0;
};

} // namespace oxygen

#if defined(OXYGEN_MSVC_VERSION)
#  define OXYGEN_TYPE_NAME_IMPL() __FUNCSIG__
#elif defined(OXYGEN_GNUC_VERSION_CHECK) || defined(OXYGEN_CLANG_VERSION)
#  define OXYGEN_TYPE_NAME_IMPL_() __PRETTY_FUNCTION__
#else
#  define OXYGEN_TYPE_NAME_IMPL_() #arg_type
#endif

#define OXYGEN_TYPED(arg_type)                                                 \
public:                                                                        \
  inline static constexpr auto ClassTypeName()                                 \
  {                                                                            \
    return OXYGEN_TYPE_NAME_IMPL();                                            \
  }                                                                            \
  inline static auto ClassTypeId() -> ::oxygen::TypeId                         \
  {                                                                            \
    static ::oxygen::TypeId typeId                                             \
      = ::oxygen::TypeRegistry::Get().RegisterType(arg_type::ClassTypeName()); \
    return typeId;                                                             \
  }                                                                            \
  auto GetTypeName() const -> const char* override { return ClassTypeName(); } \
  inline auto GetTypeId() const -> ::oxygen::TypeId override                   \
  {                                                                            \
    return ClassTypeId();                                                      \
  }                                                                            \
                                                                               \
private:
