//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Scene/Types/Flags.h>

const char* oxygen::scene::to_string(const SceneNodeFlags& value) noexcept
{
    switch (value) {
    case SceneNodeFlags::kVisible:
        return "Visible";
    case SceneNodeFlags::kStatic:
        return "Static";
    case SceneNodeFlags::kCastsShadows:
        return "CastsShadows";
    case SceneNodeFlags::kReceivesShadows:
        return "ReceivesShadows";
    case SceneNodeFlags::kRayCastingSelectable:
        return "RayCastingSelectable";
    case SceneNodeFlags::kIgnoreParentTransform:
        return "IgnoreParentTransform";

    case SceneNodeFlags::kCount:
        break;
    }
    return "__NotSupported__";
}
