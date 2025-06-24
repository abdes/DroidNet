//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

using TypeId = uint64_t;
auto constexpr kInvalidTypeId = static_cast<TypeId>(-1);

class TypeRegistry {
public:
  OXGN_COM_API static auto Get() -> TypeRegistry&;

  OXGN_COM_API TypeRegistry();
  OXGN_COM_API ~TypeRegistry();

  OXYGEN_MAKE_NON_COPYABLE(TypeRegistry)
  OXYGEN_DEFAULT_MOVABLE(TypeRegistry)

  OXGN_COM_API auto RegisterType(const char* name) const -> TypeId;
  OXGN_COM_API auto GetTypeId(const char* name) const -> TypeId;
  OXGN_COM_API auto GetTypeName(TypeId id) const -> std::string_view;
  OXGN_COM_API auto GetTypeNamePretty(TypeId id) const -> std::string_view;

  OXGN_COM_API static auto ExtractQualifiedClassName(std::string_view signature)
    -> std::string_view;

private:
  class Impl;
  // Raw pointer, because smart pointers do not work well across DLL boundaries.
  Impl* impl_;
};

} // namespace oxygen
