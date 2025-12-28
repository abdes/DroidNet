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

namespace oxygen::engine::extraction {
struct RenderItemData;
} // namespace oxygen::engine::extraction

namespace oxygen::scene {
class Scene;
} // namespace oxygen::scene

namespace oxygen::core {
struct View;
} // namespace oxygen::core
