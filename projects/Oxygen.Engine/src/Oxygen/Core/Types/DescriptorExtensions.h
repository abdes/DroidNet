//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

namespace oxygen::data {

//! File extensions of cooked descriptors, the leaf extension of an asset's
//! virtual path.
inline constexpr std::string_view kMaterialDescriptorExtension = ".omat";
inline constexpr std::string_view kGeometryDescriptorExtension = ".ogeo";
inline constexpr std::string_view kSceneDescriptorExtension = ".oscene";
inline constexpr std::string_view kPhysicsSceneDescriptorExtension = ".opscene";
inline constexpr std::string_view kPhysicsMaterialDescriptorExtension
  = ".opmat";
inline constexpr std::string_view kCollisionShapeDescriptorExtension
  = ".ocshape";
inline constexpr std::string_view kScriptDescriptorExtension = ".oscript";
inline constexpr std::string_view kInputActionDescriptorExtension = ".oiact";
inline constexpr std::string_view kInputMappingContextDescriptorExtension
  = ".oimap";
inline constexpr std::string_view kTextureDescriptorExtension = ".otex";
inline constexpr std::string_view kBufferDescriptorExtension = ".obuf";
inline constexpr std::string_view kPhysicsResourceDescriptorExtension
  = ".opres";

} // namespace oxygen::data
