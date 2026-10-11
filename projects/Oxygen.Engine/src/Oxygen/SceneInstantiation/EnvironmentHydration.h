//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/SceneInstantiation/api_export.h>

namespace oxygen::content {
class IAssetLoader;
} // namespace oxygen::content

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::scene {
class SceneEnvironment;
} // namespace oxygen::scene

namespace oxygen::instantiation {

//! Source texture keys resolved for a scene's environment records. A zero key
//! means the record binds no texture.
struct EnvironmentTextureKeys {
  content::ResourceKey metering_mask {};
  content::ResourceKey fog_cubemap {};
  content::ResourceKey sky_light_cubemap {};
  content::ResourceKey sky_sphere_cubemap {};
};

//! Hydrates a runtime environment from a scene asset's environment records.
/*!
 Adds one system per enabled record. SkyAtmosphere and SkySphere are mutually
 exclusive; SkyAtmosphere wins when both are enabled.

 @param target Environment to populate.
 @param source_asset Scene whose environment block is read.
 @param textures Resolved texture keys; each is zero exactly when its record
 binds no texture.
 @throw std::invalid_argument if a record binds a texture without its key, or
 a key is given for a record that binds none.
*/
OXGN_SCNI_API auto HydrateEnvironment(scene::SceneEnvironment& target,
  const data::SceneAsset& source_asset, const EnvironmentTextureKeys& textures)
  -> void;

//! Builds the environment of a scene asset, resolving its textures from the
//! scene's source.
/*!
 @throw std::runtime_error if a bound texture cannot be resolved.
 @throw std::invalid_argument as HydrateEnvironment().
*/
OXGN_SCNI_NDAPI auto BuildEnvironment(const data::SceneAsset& asset,
  content::IAssetLoader& loader) -> std::unique_ptr<scene::SceneEnvironment>;

} // namespace oxygen::instantiation
