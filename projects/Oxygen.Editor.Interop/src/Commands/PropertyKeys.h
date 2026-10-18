//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <cstdint>

namespace oxygen::interop::module {

  //! Stable identifier for the component class an editor property targets.
  /*!
   This is the *coarse* dimension of the property pipeline §5.3 wire
   vocabulary. The fine dimension (component-local field id) is owned
   by each component's IComponentPropertyApplier and never appears in
   this enum.

   Property pipeline routing:
     managed PropertyId ─► PropertyEntry { component, field, value }
                          │
                          └─► PropertyApplierRegistry.Find(component)
                              └─► applier.Apply(node, entries…)

   Adding a new component class requires:
     1. one new entry here (assign next free integer; never reuse);
     2. one new IComponentPropertyApplier subclass with its own
        local field enum and a small static dispatch table;
     3. one registration call in PropertyApplierRegistry::Bootstrap.
  */
  enum class ComponentId : std::uint16_t {
    kInvalid = 0,

    //! oxygen::scene::TransformComponent (per-node local transform).
    kTransform = 1,

    //! oxygen::scene::PerspectiveCamera.
    kPerspectiveCamera = 2,

    //! oxygen::scene::DirectionalLight.
    kDirectionalLight = 3,

    //! oxygen::scene::OrthographicCamera.
    kOrthographicCamera = 4,

    //! oxygen::scene::PointLight.
    kPointLight = 5,

    //! oxygen::scene::SpotLight.
    kSpotLight = 6,

    //! Authored node rendering flags (local values on the scene node).
    kNode = 7,

    //! oxygen::scene::environment::LocalFogVolume.
    kLocalFogVolume = 8,
  };

  //! One scalar entry on the property-pipeline wire.
  /*!
   Property pipeline §5.3 transports a flat sequence of these entries
   per target node. SetPropertiesCommand groups them by `component`
   and hands each contiguous run to the matching applier; the applier
   alone knows how to translate `field` into a component setter.

   The payload is intentionally a single `float` for the initial
   slice — vectors/colors/quaternions decompose into per-axis floats
   on the managed side. When non-scalar payloads are added, this
   becomes a tagged value; callers and appliers extend together.
  */
  struct PropertyEntry {
    ComponentId component;
    std::uint16_t field;
    float value;
  };

  //! Component-local field ids for `TransformComponent` properties.
  enum class TransformField : std::uint16_t {
    kPositionX = 0,
    kPositionY = 1,
    kPositionZ = 2,
    kRotationXDegrees = 3,
    kRotationYDegrees = 4,
    kRotationZDegrees = 5,
    kScaleX = 6,
    kScaleY = 7,
    kScaleZ = 8,
    kCount,
  };

  //! Component-local field ids for authored node rendering flags.
  enum class NodeField : std::uint16_t {
    kVisible = 0,
    kCastsShadows = 1,
    kReceivesShadows = 2,
    kCount,
  };

  //! Component-local field ids for `PerspectiveCamera` properties.
  enum class PerspectiveCameraField : std::uint16_t {
    kFieldOfViewYRadians = 0,
    kAspectRatio = 1,
    kNearPlane = 2,
    kFarPlane = 3,
    kApertureF = 4,
    kShutterRate = 5,
    kIso = 6,
    kAspectMode = 7,
    kCount,
  };

  //! Component-local field ids for `OrthographicCamera` properties.
  enum class OrthographicCameraField : std::uint16_t {
    kOrthographicSize = 0,
    kAspectRatio = 1,
    kNearPlane = 2,
    kFarPlane = 3,
    kApertureF = 4,
    kShutterRate = 5,
    kIso = 6,
    kAspectMode = 7,
    kCount,
  };

  //! Component-local field ids for `DirectionalLight` properties.
  enum class DirectionalLightField : std::uint16_t {
    kColorR = 0,
    kColorG = 1,
    kColorB = 2,
    kAffectsWorld = 3,
    kCastsShadows = 5,
    kShadowBias = 6,
    kShadowNormalBias = 7,
    kContactShadows = 8,
    kShadowResolutionHint = 9,
    kExposureCompensation = 10,
    kIntensityLux = 11,
    kAngularSizeRadians = 12,
    kCascadeCount = 15,
    kSplitMode = 16,
    kMaxShadowDistance = 17,
    kCascadeDistance0 = 18,
    kCascadeDistance1 = 19,
    kCascadeDistance2 = 20,
    kCascadeDistance3 = 21,
    kDistributionExponent = 22,
    kTransitionFraction = 23,
    kDistanceFadeoutFraction = 24,
    kAtmosphereLightSlot = 25,
    kUsePerPixelAtmosphereTransmittance = 26,
    kDiskScaleR = 27,
    kDiskScaleG = 28,
    kDiskScaleB = 29,
    kCount = 30,
  };

  //! Field ids shared by point and spot lights; common ids match directional.
  enum class LocalLightField : std::uint16_t {
    kColorR = 0,
    kColorG = 1,
    kColorB = 2,
    kAffectsWorld = 3,
    kCastsShadows = 5,
    kShadowBias = 6,
    kShadowNormalBias = 7,
    kContactShadows = 8,
    kShadowResolutionHint = 9,
    kExposureCompensation = 10,
    kLuminousFluxLm = 11,
    kRange = 12,
    kSourceRadius = 13,
    kInnerConeAngleRadians = 14,
    kOuterConeAngleRadians = 15,
    kCount = 16,
  };

  //! Field ids of the local fog volume component.
  enum class LocalFogVolumeField : std::uint16_t {
    kEnabled = 0,
    kRadialFogExtinction = 1,
    kHeightFogExtinction = 2,
    kHeightFogFalloff = 3,
    kHeightFogOffset = 4,
    kFogPhaseG = 5,
    kFogAlbedoR = 6,
    kFogAlbedoG = 7,
    kFogAlbedoB = 8,
    kFogEmissiveR = 9,
    kFogEmissiveG = 10,
    kFogEmissiveB = 11,
    kSortPriority = 12,
    kCount = 13,
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
