//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Mesh types
enum class MeshType : uint8_t {
  // clang-format off
  kUnknown        = 0,

  kStandard       = 1,  //!< Standard static mesh (vertex/index buffers)
  kProcedural     = 2,  //!< Procedurally generated mesh (parameters blob)
  kSkinned        = 3,  //!< Skeletal/skinned mesh (bone weights, etc.)
  kMorphTarget    = 4,  //!< Mesh with morph/blend shapes
  kInstanced      = 5,  //!< Instanced mesh (for GPU instancing)
  kCollision      = 6,  //!< Collision/physics mesh
  kNavigation     = 7,  //!< Navigation mesh (navmesh)
  kBillboard      = 8,  //!< Billboard/impostor mesh
  kVoxel          = 9,  //!< Voxel/volumetric mesh

  //!< Maximum value sentinel
  kMaxMeshType    = kVoxel
  // clang-format on
};

static_assert(sizeof(std::underlying_type_t<MeshType>) <= sizeof(uint8_t),
  "MeshType enum must fit in uint8_t for compatibility with PAK format");

//! String representation of enum values in `MeshType`.
OXGN_DATA_NDAPI auto to_string(MeshType value) noexcept -> const char*;

} // namespace oxygen::data
