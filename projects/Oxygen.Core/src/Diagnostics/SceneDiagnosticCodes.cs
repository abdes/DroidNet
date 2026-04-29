// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for scene authoring validation.
/// </summary>
public static class SceneDiagnosticCodes
{
    public const string TransformScaleZeroAxis = DiagnosticCodes.ScenePrefix + "TransformComponent.Scale.ZeroAxis";

    public const string TransformFieldNotFinite = DiagnosticCodes.ScenePrefix + "TransformComponent.Field.NotFinite";

    public const string GeometryReferenceRequired = DiagnosticCodes.ScenePrefix + "GeometryComponent.Geometry.Required";

    public const string PerspectiveCameraNearFarInvalid = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.NearFar.Invalid";

    public const string PerspectiveCameraNearPlaneNonPositive = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.NearPlane.NonPositive";

    public const string PerspectiveCameraAspectRatioNonPositive = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.AspectRatio.NonPositive";

    public const string DirectionalLightSunExclusivity = DiagnosticCodes.ScenePrefix + "DirectionalLight.Sun.Exclusivity";

    public const string DirectionalLightFieldNotFinite = DiagnosticCodes.ScenePrefix + "DirectionalLight.Field.NotFinite";

    public const string EnvironmentSunRefStale = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.SunRefStale";

    public const string EnvironmentExposureModeInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ExposureMode.Invalid";

    public const string EnvironmentManualExposureInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ManualExposure.Invalid";

    public const string EnvironmentToneMappingInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ToneMapping.Invalid";

    public const string EnvironmentExposureCompensationInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ExposureCompensation.Invalid";

    public const string EnvironmentBackgroundColorInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.BackgroundColor.Invalid";

    public const string EnvironmentSkyAtmosphereInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.SkyAtmosphere.Invalid";

    public const string ComponentAddDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.AddDenied";

    public const string ComponentRemoveDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.RemoveDenied";
}
