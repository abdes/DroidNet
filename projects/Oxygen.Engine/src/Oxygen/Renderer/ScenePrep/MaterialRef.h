//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/MaterialAsset.h>

namespace oxygen::engine::sceneprep {

//! Lightweight renderer-facing reference to a material with stable provenance.
/*!
 MaterialRef decouples material *provenance* from the resolved payload:

 - `source_asset_key` identifies what the scene/authoring referenced.
 - `resolved_asset_key` identifies what material payload is currently bound.
 - `resolved_asset` holds the resolved material data used for rendering.

 `resolved_asset` may be null to represent "missing/unresolved" material.
 Downstream systems (e.g. MaterialBinder) must handle that explicitly.

 Ownership:
 - The asset system owns material lifetimes.
 - MaterialRef holds a shared_ptr to keep the resolved asset alive for the
   duration of the render item snapshot.
*/
struct MaterialRef {
  oxygen::data::AssetKey source_asset_key {};
  oxygen::data::AssetKey resolved_asset_key {};
  std::shared_ptr<const oxygen::data::MaterialAsset> resolved_asset;

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return resolved_asset != nullptr;
  }

  explicit operator bool() const noexcept { return IsValid(); }
};

} // namespace oxygen::engine::sceneprep
