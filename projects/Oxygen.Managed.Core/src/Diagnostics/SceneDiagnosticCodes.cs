// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for scene authoring validation.
/// </summary>
public static class SceneDiagnosticCodes
{
    /// <summary>Transform scale has a zero axis.</summary>
    public const string TransformScaleZeroAxis = DiagnosticCodes.ScenePrefix + "TransformComponent.Scale.ZeroAxis";

    /// <summary>Transform field is not finite.</summary>
    public const string TransformFieldNotFinite = DiagnosticCodes.ScenePrefix + "TransformComponent.Field.NotFinite";

    /// <summary>Geometry reference is required.</summary>
    public const string GeometryReferenceRequired = DiagnosticCodes.ScenePrefix + "GeometryComponent.Geometry.Required";

    /// <summary>Perspective camera near/far planes are invalid.</summary>
    public const string PerspectiveCameraNearFarInvalid = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.NearFar.Invalid";

    /// <summary>Perspective camera near plane is not positive.</summary>
    public const string PerspectiveCameraNearPlaneNonPositive = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.NearPlane.NonPositive";

    /// <summary>Perspective camera aspect ratio is not positive.</summary>
    public const string PerspectiveCameraAspectRatioNonPositive = DiagnosticCodes.ScenePrefix + "PerspectiveCamera.AspectRatio.NonPositive";

    /// <summary>Directional light sun exclusivity was violated.</summary>
    public const string DirectionalLightSunExclusivity = DiagnosticCodes.ScenePrefix + "DirectionalLight.Sun.Exclusivity";

    /// <summary>Directional light field is not finite.</summary>
    public const string DirectionalLightFieldNotFinite = DiagnosticCodes.ScenePrefix + "DirectionalLight.Field.NotFinite";

    /// <summary>Environment sun reference is stale.</summary>
    public const string EnvironmentSunRefStale = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.SunRefStale";

    /// <summary>Environment exposure mode is invalid.</summary>
    public const string EnvironmentExposureModeInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ExposureMode.Invalid";

    /// <summary>Environment manual exposure value is invalid.</summary>
    public const string EnvironmentManualExposureInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ManualExposure.Invalid";

    /// <summary>Environment tone-mapping mode is invalid.</summary>
    public const string EnvironmentToneMappingInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ToneMapping.Invalid";

    /// <summary>Environment exposure compensation is invalid.</summary>
    public const string EnvironmentExposureCompensationInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.ExposureCompensation.Invalid";

    /// <summary>Environment background color is invalid.</summary>
    public const string EnvironmentBackgroundColorInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.BackgroundColor.Invalid";

    /// <summary>Environment sky-atmosphere value is invalid.</summary>
    public const string EnvironmentSkyAtmosphereInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.SkyAtmosphere.Invalid";

    /// <summary>Component add operation was denied.</summary>
    public const string ComponentAddDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.AddDenied";

    /// <summary>Component remove operation was denied.</summary>
    public const string ComponentRemoveDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.RemoveDenied";
}
