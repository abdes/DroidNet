//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Core/Types/Frame.h>

namespace oxygen::data {
class GeometryAsset;
class MaterialAsset;
class Mesh;
class MeshView;
} // namespace oxygen::data

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen {
class ResolvedView;
} // namespace oxygen

namespace oxygen::vortex::sceneprep {
struct RenderItemData;
class RenderItemProto;
class ScenePrepContext;
class ScenePrepState;
} // namespace oxygen::vortex::sceneprep
