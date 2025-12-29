//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Data/AssetKey.h>

namespace oxygen::data {
class Mesh;
} // namespace oxygen::data

namespace oxygen::engine::sceneprep {

//! Lightweight renderer-facing reference to an LOD mesh with stable identity.
/*!
 GeometryRef packages the stable identity `(AssetKey, lod_index)` together with
 the resolved LOD mesh pointer. This allows renderer subsystems (e.g.
 GeometryUploader) to intern and validate resources based on stable identity,
 while still accessing the mesh data for upload.

 Ownership:
 - The geometry asset/cache owns the mesh lifetime.
 - GeometryRef holds a shared_ptr to keep the mesh alive for the duration of
   the render item snapshot.
*/
struct GeometryRef {
  oxygen::data::AssetKey asset_key {};
  std::uint32_t lod_index { 0U };
  std::shared_ptr<const oxygen::data::Mesh> mesh;

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return mesh != nullptr;
  }
};

} // namespace oxygen::engine::sceneprep
