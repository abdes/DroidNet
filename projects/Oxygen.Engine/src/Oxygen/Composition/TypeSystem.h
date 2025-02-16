//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Composition/api_export.h>

namespace oxygen {

using TypeId = uint64_t;

class TypeRegistry {
public:
    OXYGEN_COMP_API static auto Get() -> TypeRegistry&;

    OXYGEN_COMP_API TypeRegistry();
    OXYGEN_COMP_API ~TypeRegistry();

    OXYGEN_MAKE_NON_COPYABLE(TypeRegistry)
    OXYGEN_DEFAULT_MOVABLE(TypeRegistry)

    OXYGEN_COMP_API auto RegisterType(const char* name) const -> TypeId;
    OXYGEN_COMP_API auto GetTypeId(const char* name) const -> TypeId;

private:
    class Impl;
    // Raw pointer, because smart pointers do not work well across DLL boundaries.
    Impl* impl_;
};

} // namespace oxygen
