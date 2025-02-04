//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/TypeSystem.h"

namespace oxygen {

class Object {
public:
    Object() = default;
    virtual ~Object() = default;

    // All components should implement proper copy and move semantics to handle
    // copying and moving as appropriate.
    OXYGEN_DEFAULT_COPYABLE(Object)
    OXYGEN_DEFAULT_MOVABLE(Object)

    [[nodiscard]] virtual auto GetTypeId() const -> TypeId = 0;
    [[nodiscard]] virtual auto GetTypeName() const -> const char* = 0;
};

} // namespace oxygen

#define OXYGEN_TYPED(arg_type)                                                                              \
public:                                                                                                     \
    static constexpr const char* ClassTypeName() { return #arg_type; }                                      \
    inline static oxygen::TypeId ClassTypeId()                                                              \
    {                                                                                                       \
        static oxygen::TypeId typeId = oxygen::TypeRegistry::Get().RegisterType(arg_type::ClassTypeName()); \
        return typeId;                                                                                      \
    }                                                                                                       \
    const char* GetTypeName() const override { return ClassTypeName(); }                                    \
    inline oxygen::TypeId GetTypeId() const override { return ClassTypeId(); }                              \
                                                                                                            \
private:
