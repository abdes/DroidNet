// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Partial edit for a transform component.
/// </summary>
/// <param name="Position">Optional local position.</param>
/// <param name="RotationEulerDegrees">Optional local rotation expressed as Euler degrees for inspector editing.</param>
/// <param name="Scale">Optional local scale.</param>
/// <param name="PositionX">Optional local position X axis.</param>
/// <param name="PositionY">Optional local position Y axis.</param>
/// <param name="PositionZ">Optional local position Z axis.</param>
/// <param name="RotationXDegrees">Optional local rotation X axis expressed as Euler degrees.</param>
/// <param name="RotationYDegrees">Optional local rotation Y axis expressed as Euler degrees.</param>
/// <param name="RotationZDegrees">Optional local rotation Z axis expressed as Euler degrees.</param>
/// <param name="ScaleX">Optional local scale X axis.</param>
/// <param name="ScaleY">Optional local scale Y axis.</param>
/// <param name="ScaleZ">Optional local scale Z axis.</param>
public sealed record TransformEdit(
    Optional<Vector3> Position,
    Optional<Vector3> RotationEulerDegrees,
    Optional<Vector3> Scale,
    Optional<float> PositionX = default,
    Optional<float> PositionY = default,
    Optional<float> PositionZ = default,
    Optional<float> RotationXDegrees = default,
    Optional<float> RotationYDegrees = default,
    Optional<float> RotationZDegrees = default,
    Optional<float> ScaleX = default,
    Optional<float> ScaleY = default,
    Optional<float> ScaleZ = default);

/// <summary>
/// Partial edit for a geometry component.
/// </summary>
/// <param name="GeometryUri">Optional geometry asset URI. A supplied null is rejected because geometry components must remain savable.</param>
public sealed record GeometryEdit(Optional<Uri?> GeometryUri);

/// <summary>
/// Partial edit for a perspective camera component.
/// </summary>
/// <param name="FieldOfViewDegrees">Optional vertical field of view in degrees.</param>
/// <param name="AspectRatio">Optional aspect ratio.</param>
/// <param name="NearPlane">Optional near plane.</param>
/// <param name="FarPlane">Optional far plane.</param>
public sealed record PerspectiveCameraEdit(
    Optional<float> FieldOfViewDegrees,
    Optional<float> AspectRatio,
    Optional<float> NearPlane,
    Optional<float> FarPlane);

/// <summary>
/// Partial edit for a directional light component.
/// </summary>
public sealed record DirectionalLightEdit(
    Optional<Vector3> Color,
    Optional<float> IntensityLux,
    Optional<bool> IsSunLight,
    Optional<bool> EnvironmentContribution,
    Optional<bool> CastsShadows,
    Optional<bool> AffectsWorld,
    Optional<float> AngularSizeRadians,
    Optional<float> ExposureCompensation);

/// <summary>
/// Partial edit for scene environment authoring data.
/// </summary>
public sealed record SceneEnvironmentEdit(
    Optional<bool> AtmosphereEnabled,
    Optional<Guid?> SunNodeId,
    Optional<ExposureMode> ExposureMode,
    Optional<float> ManualExposureEv,
    Optional<float> ExposureCompensation,
    Optional<ToneMappingMode> ToneMapping,
    Optional<Vector3> BackgroundColor,
    Optional<SkyAtmosphereEnvironmentData> SkyAtmosphere = default,
    Optional<PostProcessEnvironmentData> PostProcess = default);
