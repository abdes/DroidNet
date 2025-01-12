//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/TypeSystem.h"

namespace oxygen {

class Object
{
 public:
  Object() = default;
  virtual ~Object() = default;

  // All components should implement proper copy and move semantics to handle
  // copying and moving as appropriate.
  Object(const Object&) = default;
  Object& operator=(const Object&) = default;
  Object(Object&&) = default;
  Object& operator=(Object&&) = default;

  [[nodiscard]] virtual auto TypeId() const -> TypeId = 0;
  [[nodiscard]] virtual auto TypeName() const -> const char* = 0;
};

} // namespace oxygen

#define OXYGEN_TYPED(arg_type)                                                                          \
 public:                                                                                                \
  static constexpr const char* ClassTypeName() { return #arg_type; }                                    \
  inline static oxygen::TypeId ClassTypeId()                                                            \
  {                                                                                                     \
    static oxygen::TypeId typeId = oxygen::TypeRegistry::Get().RegisterType(arg_type::ClassTypeName()); \
    return typeId;                                                                                      \
  }                                                                                                     \
  const constexpr char* TypeName() const override { return ClassTypeName(); }                           \
  inline oxygen::TypeId TypeId() const override { return ClassTypeId(); }                               \
                                                                                                        \
 private:
