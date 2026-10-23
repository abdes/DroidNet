// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for scene authoring validation.
/// </summary>
public static class SceneDiagnosticCodes
{
    /// <summary>A new scene could not be created: its name was rejected, a file exists there, or it could not be written.</summary>
    public const string CreateFailed = DiagnosticCodes.ScenePrefix + "Asset.Create.Failed";

    /// <summary>The native surface inventory is currently unavailable.</summary>
    public const string SurfaceInventoryUnavailable = DiagnosticCodes.ScenePrefix + "Surface.Inventory.Unavailable";

    /// <summary>A numeric property value is not finite.</summary>
    public const string PropertyValueNotFinite = DiagnosticCodes.ScenePrefix + "Property.Value.NotFinite";

    /// <summary>A finite numeric property value is outside the range the scene schema declares for the property.</summary>
    public const string PropertyValueOutOfRange = DiagnosticCodes.ScenePrefix + "Property.Value.OutOfRange";

    /// <summary>An enumeration property value is not a member the scene schema declares.</summary>
    public const string PropertyValueUndefined = DiagnosticCodes.ScenePrefix + "Property.Value.Undefined";

    /// <summary>Geometry reference is required.</summary>
    public const string GeometryReferenceRequired = DiagnosticCodes.ScenePrefix + "GeometryComponent.Geometry.Required";

    /// <summary>Directional light sun exclusivity was violated.</summary>
    public const string DirectionalLightSunExclusivity = DiagnosticCodes.ScenePrefix + "DirectionalLight.Sun.Exclusivity";

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

    /// <summary>Environment fog value is invalid.</summary>
    public const string EnvironmentFogInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.Fog.Invalid";

    /// <summary>Environment backdrop, sky sphere or sky light value is invalid.</summary>
    public const string EnvironmentSkyInvalid = DiagnosticCodes.ScenePrefix + "ENVIRONMENT.Sky.Invalid";

    /// <summary>Component add operation was denied.</summary>
    public const string ComponentAddDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.AddDenied";

    /// <summary>Component remove operation was denied.</summary>
    public const string ComponentRemoveDenied = DiagnosticCodes.ScenePrefix + "COMPONENT.RemoveDenied";
}
