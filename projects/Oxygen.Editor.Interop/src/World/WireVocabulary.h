//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Commands/PropertyKeys.h>
#include <Commands/TransformPropertyApplier.h>
#include <Commands/NodePropertyApplier.h>
#include <Commands/PerspectiveCameraPropertyApplier.h>
#include <Commands/OrthographicCameraPropertyApplier.h>
#include <Commands/DirectionalLightPropertyApplier.h>
#include <Commands/LocalLightPropertyApplier.h>
#include <Commands/LocalFogVolumePropertyApplier.h>

#pragma managed

//! The editor module's property wire vocabulary, published to managed code.
/*!
 Every member is defined from the native constant the matching
 `IComponentPropertyApplier` dispatches on, so managed code holds no copy of
 the numbering and a native renumbering flows through at compile time.
*/
namespace Oxygen::Interop {

  /// <summary>
  /// Stable wire identifier for the component class an editor property targets.
  /// Mirrors <c>oxygen::interop::module::ComponentId</c>.
  /// </summary>
  /// <remarks>
  /// <para>
  /// This is the coarse dimension of the property-pipeline wire vocabulary.
  /// The fine dimension is a component-local field id owned by the matching
  /// native applier class.
  /// </para>
  /// <para>
  /// Adding a new component class is one new entry here, one native applier,
  /// and one registration call. Adding a new property to an existing component
  /// touches neither this enum nor the transport.
  /// </para>
  /// </remarks>
  public enum class EngineComponentId : System::UInt16 {
    /// <summary>Sentinel; not a valid wire id.</summary>
    Invalid = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kInvalid),
    /// <summary><c>oxygen::scene::TransformComponent</c>.</summary>
    Transform = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kTransform),
    /// <summary><c>oxygen::scene::PerspectiveCamera</c>.</summary>
    PerspectiveCamera = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kPerspectiveCamera),
    /// <summary><c>oxygen::scene::DirectionalLight</c>.</summary>
    DirectionalLight = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kDirectionalLight),
    /// <summary><c>oxygen::scene::OrthographicCamera</c>.</summary>
    OrthographicCamera = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kOrthographicCamera),
    /// <summary><c>oxygen::scene::PointLight</c>.</summary>
    PointLight = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kPointLight),
    /// <summary><c>oxygen::scene::SpotLight</c>.</summary>
    SpotLight = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kSpotLight),
    /// <summary>Authored node rendering flags, applied as local scene-node values.</summary>
    Node = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kNode),
    /// <summary><c>oxygen::scene::environment::LocalFogVolume</c>.</summary>
    LocalFogVolume = static_cast<System::UInt16>(
      ::oxygen::interop::module::ComponentId::kLocalFogVolume),
  };

  /// <summary>
  /// Component-local field ids for <see cref="EngineComponentId.Transform"/>.
  /// Mirrors <c>oxygen::interop::module::TransformField</c>.
  /// </summary>
  public enum class TransformField : System::UInt16 {
    /// <summary>Local position X.</summary>
    PositionX = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kPositionX),
    /// <summary>Local position Y.</summary>
    PositionY = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kPositionY),
    /// <summary>Local position Z.</summary>
    PositionZ = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kPositionZ),
    /// <summary>Local rotation X (Euler degrees).</summary>
    RotationXDegrees = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kRotationXDegrees),
    /// <summary>Local rotation Y (Euler degrees).</summary>
    RotationYDegrees = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kRotationYDegrees),
    /// <summary>Local rotation Z (Euler degrees).</summary>
    RotationZDegrees = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kRotationZDegrees),
    /// <summary>Local scale X.</summary>
    ScaleX = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kScaleX),
    /// <summary>Local scale Y.</summary>
    ScaleY = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kScaleY),
    /// <summary>Local scale Z.</summary>
    ScaleZ = static_cast<System::UInt16>(
      ::oxygen::interop::module::TransformField::kScaleZ),
  };

  /// <summary>
  /// Field ids for <see cref="EngineComponentId.Node"/> rendering flags.
  /// Mirrors <c>oxygen::interop::module::NodeField</c>.
  /// </summary>
  public enum class NodeField : System::UInt16 {
    /// <summary>Scene visibility.</summary>
    Visible = static_cast<System::UInt16>(
      ::oxygen::interop::module::NodeField::kVisible),
    /// <summary>Geometry casts shadows.</summary>
    CastsShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::NodeField::kCastsShadows),
    /// <summary>Geometry receives shadows.</summary>
    ReceivesShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::NodeField::kReceivesShadows),
  };

  /// <summary>
  /// Component-local field ids for <see cref="EngineComponentId.PerspectiveCamera"/>.
  /// Mirrors <c>oxygen::interop::module::PerspectiveCameraField</c>.
  /// </summary>
  public enum class PerspectiveCameraField : System::UInt16 {
    /// <summary>Vertical field of view, in radians.</summary>
    FieldOfViewYRadians = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kFieldOfViewYRadians),
    /// <summary>Viewport aspect ratio.</summary>
    AspectRatio = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kAspectRatio),
    /// <summary>Near clipping plane, in meters.</summary>
    NearPlane = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kNearPlane),
    /// <summary>Far clipping plane, in meters.</summary>
    FarPlane = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kFarPlane),
    /// <summary>Physical aperture as an f-number.</summary>
    ApertureF = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kApertureF),
    /// <summary>Shutter rate in reciprocal seconds.</summary>
    ShutterRate = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kShutterRate),
    /// <summary>ISO sensor sensitivity.</summary>
    Iso = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kIso),
    /// <summary>Auto or Fixed authored aspect policy.</summary>
    AspectMode = static_cast<System::UInt16>(
      ::oxygen::interop::module::PerspectiveCameraField::kAspectMode),
  };

  /// <summary>
  /// Component-local field ids for <see cref="EngineComponentId.OrthographicCamera"/>.
  /// Mirrors <c>oxygen::interop::module::OrthographicCameraField</c>.
  /// </summary>
  public enum class OrthographicCameraField : System::UInt16 {
    /// <summary>Half of the visible height, in meters.</summary>
    OrthographicSize = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kOrthographicSize),
    /// <summary>Retained Fixed aspect ratio (width / height).</summary>
    AspectRatio = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kAspectRatio),
    /// <summary>Near clipping plane, in meters.</summary>
    NearPlane = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kNearPlane),
    /// <summary>Far clipping plane, in meters.</summary>
    FarPlane = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kFarPlane),
    /// <summary>Physical aperture as an f-number.</summary>
    ApertureF = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kApertureF),
    /// <summary>Shutter rate in reciprocal seconds.</summary>
    ShutterRate = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kShutterRate),
    /// <summary>ISO sensor sensitivity.</summary>
    Iso = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kIso),
    /// <summary>Auto or Fixed authored aspect policy.</summary>
    AspectMode = static_cast<System::UInt16>(
      ::oxygen::interop::module::OrthographicCameraField::kAspectMode),
  };

  /// <summary>
  /// Component-local field ids for <see cref="EngineComponentId.DirectionalLight"/>.
  /// Mirrors <c>oxygen::interop::module::DirectionalLightField</c>.
  /// </summary>
  public enum class DirectionalLightField : System::UInt16 {
    /// <summary>Linear red color multiplier.</summary>
    ColorR = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kColorR),
    /// <summary>Linear green color multiplier.</summary>
    ColorG = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kColorG),
    /// <summary>Linear blue color multiplier.</summary>
    ColorB = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kColorB),
    /// <summary>World-lighting participation flag.</summary>
    AffectsWorld = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kAffectsWorld),
    /// <summary>Shadow-casting flag.</summary>
    CastsShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCastsShadows),
    /// <summary>Shadow depth bias.</summary>
    ShadowBias = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kShadowBias),
    /// <summary>Shadow normal bias.</summary>
    ShadowNormalBias = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kShadowNormalBias),
    /// <summary>Contact shadow flag.</summary>
    ContactShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kContactShadows),
    /// <summary>Shadow resolution hint enum value.</summary>
    ShadowResolutionHint = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kShadowResolutionHint),
    /// <summary>Exposure compensation in EV.</summary>
    ExposureCompensation = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kExposureCompensation),
    /// <summary>Directional light illuminance in lux.</summary>
    IntensityLux = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kIntensityLux),
    /// <summary>Directional light angular size in radians.</summary>
    AngularSizeRadians = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kAngularSizeRadians),
    /// <summary>Cascade count.</summary>
    CascadeCount = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCascadeCount),
    /// <summary>Cascade split mode enum value.</summary>
    SplitMode = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kSplitMode),
    /// <summary>Maximum shadow distance.</summary>
    MaxShadowDistance = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kMaxShadowDistance),
    /// <summary>First cascade distance.</summary>
    CascadeDistance0 = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCascadeDistance0),
    /// <summary>Second cascade distance.</summary>
    CascadeDistance1 = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCascadeDistance1),
    /// <summary>Third cascade distance.</summary>
    CascadeDistance2 = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCascadeDistance2),
    /// <summary>Fourth cascade distance.</summary>
    CascadeDistance3 = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kCascadeDistance3),
    /// <summary>Generated CSM distribution exponent.</summary>
    DistributionExponent = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kDistributionExponent),
    /// <summary>Cascade transition fraction.</summary>
    TransitionFraction = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kTransitionFraction),
    /// <summary>Shadow distance fadeout fraction.</summary>
    DistanceFadeoutFraction = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kDistanceFadeoutFraction),
    /// <summary>Explicit None/Primary/Secondary atmosphere assignment.</summary>
    AtmosphereLightSlot = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kAtmosphereLightSlot),
    /// <summary>Per-pixel atmosphere transmittance.</summary>
    UsePerPixelAtmosphereTransmittance = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kUsePerPixelAtmosphereTransmittance),
    /// <summary>Red disk luminance multiplier.</summary>
    DiskScaleR = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kDiskScaleR),
    /// <summary>Green disk luminance multiplier.</summary>
    DiskScaleG = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kDiskScaleG),
    /// <summary>Blue disk luminance multiplier.</summary>
    DiskScaleB = static_cast<System::UInt16>(
      ::oxygen::interop::module::DirectionalLightField::kDiskScaleB),
  };

  /// <summary>
  /// Component-local field ids shared by <see cref="EngineComponentId.PointLight"/> and
  /// <see cref="EngineComponentId.SpotLight"/>; the common ids match <see cref="DirectionalLightField"/>.
  /// Mirrors <c>oxygen::interop::module::LocalLightField</c>.
  /// </summary>
  public enum class LocalLightField : System::UInt16 {
    /// <summary>Linear red color multiplier.</summary>
    ColorR = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kColorR),
    /// <summary>Linear green color multiplier.</summary>
    ColorG = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kColorG),
    /// <summary>Linear blue color multiplier.</summary>
    ColorB = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kColorB),
    /// <summary>Whether the light contributes to scene lighting.</summary>
    AffectsWorld = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kAffectsWorld),
    /// <summary>Whether the light casts shadows.</summary>
    CastsShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kCastsShadows),
    /// <summary>Shadow depth bias.</summary>
    ShadowBias = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kShadowBias),
    /// <summary>Shadow normal bias.</summary>
    ShadowNormalBias = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kShadowNormalBias),
    /// <summary>Whether contact shadows are enabled.</summary>
    ContactShadows = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kContactShadows),
    /// <summary>Shadow-map resolution hint.</summary>
    ShadowResolutionHint = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kShadowResolutionHint),
    /// <summary>Light exposure compensation in EV.</summary>
    ExposureCompensation = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kExposureCompensation),
    /// <summary>Luminous flux, in lumens.</summary>
    LuminousFluxLumens = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kLuminousFluxLm),
    /// <summary>Influence range, in meters.</summary>
    Range = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kRange),
    /// <summary>Emitter source radius, in meters.</summary>
    SourceRadius = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kSourceRadius),
    /// <summary>Spot inner cone half-angle, in radians.</summary>
    InnerConeAngleRadians = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kInnerConeAngleRadians),
    /// <summary>Spot outer cone half-angle, in radians.</summary>
    OuterConeAngleRadians = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalLightField::kOuterConeAngleRadians),
  };

  /// <summary>
  /// Component-local field ids of <see cref="EngineComponentId.LocalFogVolume"/>; they match the
  /// native <c>LocalFogVolumeField</c> enum.
  /// </summary>
  public enum class LocalFogVolumeField : System::UInt16 {
    /// <summary>Volume enablement, 0 or 1.</summary>
    Enabled = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kEnabled),
    /// <summary>Extinction at the volume's center.</summary>
    RadialFogExtinction = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kRadialFogExtinction),
    /// <summary>Extinction of the height-based density.</summary>
    HeightFogExtinction = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kHeightFogExtinction),
    /// <summary>Height falloff.</summary>
    HeightFogFalloff = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kHeightFogFalloff),
    /// <summary>Height offset relative to the volume's center.</summary>
    HeightFogOffset = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kHeightFogOffset),
    /// <summary>Phase anisotropy.</summary>
    FogPhaseG = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogPhaseG),
    /// <summary>Albedo red channel.</summary>
    FogAlbedoR = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogAlbedoR),
    /// <summary>Albedo green channel.</summary>
    FogAlbedoG = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogAlbedoG),
    /// <summary>Albedo blue channel.</summary>
    FogAlbedoB = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogAlbedoB),
    /// <summary>Emissive red channel.</summary>
    FogEmissiveR = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogEmissiveR),
    /// <summary>Emissive green channel.</summary>
    FogEmissiveG = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogEmissiveG),
    /// <summary>Emissive blue channel.</summary>
    FogEmissiveB = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kFogEmissiveB),
    /// <summary>Composition order, an integer from -127 to 127.</summary>
    SortPriority = static_cast<System::UInt16>(
      ::oxygen::interop::module::LocalFogVolumeField::kSortPriority),
  };

} // namespace Oxygen::Interop
