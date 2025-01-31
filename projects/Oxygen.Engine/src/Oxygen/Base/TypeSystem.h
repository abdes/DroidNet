//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include "Oxygen/Base/Macros.h"
#include "Oxygen/Base/api_export.h"

namespace oxygen {

using TypeId = uint32_t;

class TypeRegistry {
public:
    OXYGEN_BASE_API static auto Get() -> TypeRegistry&;

    OXYGEN_BASE_API TypeRegistry();
    OXYGEN_BASE_API ~TypeRegistry();

    OXYGEN_MAKE_NON_COPYABLE(TypeRegistry)
    OXYGEN_DEFAULT_MOVABLE(TypeRegistry)

    OXYGEN_BASE_API auto RegisterType(const char* name) const -> TypeId;
    OXYGEN_BASE_API auto GetTypeId(const char* name) const -> TypeId;

private:
    class Impl;
    // Raw pointer, because smart pointers do not work well across DLL boundaries.
    Impl* impl_;
};

} // namespace oxygen
