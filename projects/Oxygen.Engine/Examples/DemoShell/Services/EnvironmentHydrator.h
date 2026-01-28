//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::data {
class SceneAsset;
} // namespace oxygen::data

namespace oxygen::scene {
class SceneEnvironment;
} // namespace oxygen::scene

namespace oxygen::scene::environment {
class SkyAtmosphere;
class SkySphere;
class SkyLight;
class Fog;
class VolumetricClouds;
class PostProcessVolume;
} // namespace oxygen::scene::environment

namespace oxygen::examples {

//! Stateless utility for populating runtime environment systems from assets.
/*!
 Provides a single translation layer from SceneAsset environment records to
 the runtime SceneEnvironment systems.

### Key Features

- **Single Responsibility**: Performs one-way hydration only.
- **Type Safe**: Uses overloads per system type.
- **Consistent Rule**: Enforces SkyAtmosphere vs SkySphere exclusivity.

### Usage Patterns

 Call from SceneLoaderService when building the runtime environment.

### Architecture Notes

 The hydrator does not own systems or assets. It only copies data.

 @see scene::SceneEnvironment
*/
class EnvironmentHydrator final {
public:
  EnvironmentHydrator() = delete;
  OXYGEN_MAKE_NON_COPYABLE(EnvironmentHydrator);
  OXYGEN_MAKE_NON_MOVABLE(EnvironmentHydrator);

  //! Hydrate a runtime environment from a scene asset.
  static void HydrateEnvironment(
    scene::SceneEnvironment& target_env, const data::SceneAsset& source_asset);

  //! Hydrate SkyAtmosphere runtime system from asset record.
  static void HydrateSystem(scene::environment::SkyAtmosphere& target,
    const data::pak::SkyAtmosphereEnvironmentRecord& source);

  //! Hydrate SkySphere runtime system from asset record.
  static void HydrateSystem(scene::environment::SkySphere& target,
    const data::pak::SkySphereEnvironmentRecord& source);

  //! Hydrate Fog runtime system from asset record.
  static void HydrateSystem(scene::environment::Fog& target,
    const data::pak::FogEnvironmentRecord& source);

  //! Hydrate SkyLight runtime system from asset record.
  static void HydrateSystem(scene::environment::SkyLight& target,
    const data::pak::SkyLightEnvironmentRecord& source);

  //! Hydrate VolumetricClouds runtime system from asset record.
  static void HydrateSystem(scene::environment::VolumetricClouds& target,
    const data::pak::VolumetricCloudsEnvironmentRecord& source);

  //! Hydrate PostProcessVolume runtime system from asset record.
  static void HydrateSystem(scene::environment::PostProcessVolume& target,
    const data::pak::PostProcessVolumeEnvironmentRecord& source);
};

} // namespace oxygen::examples
