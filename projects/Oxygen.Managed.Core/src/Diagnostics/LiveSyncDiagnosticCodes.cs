// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for live scene synchronization.
/// </summary>
public static class LiveSyncDiagnosticCodes
{
    /// <summary>Runtime engine is not running.</summary>
    public const string NotRunning = DiagnosticCodes.LiveSyncPrefix + "NotRunning";

    /// <summary>Runtime engine is faulted.</summary>
    public const string RuntimeFaulted = DiagnosticCodes.LiveSyncPrefix + "RuntimeFaulted";

    /// <summary>Live sync operation was cancelled.</summary>
    public const string Cancelled = DiagnosticCodes.LiveSyncPrefix + "Cancelled";

    /// <summary>Transform live sync was rejected.</summary>
    public const string TransformRejected = DiagnosticCodes.LiveSyncPrefix + "TRANSFORM.Rejected";

    /// <summary>Transform live sync failed.</summary>
    public const string TransformFailed = DiagnosticCodes.LiveSyncPrefix + "TRANSFORM.Failed";

    /// <summary>Geometry live sync was rejected.</summary>
    public const string GeometryRejected = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.Rejected";

    /// <summary>Geometry live sync failed.</summary>
    public const string GeometryFailed = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.Failed";

    /// <summary>Geometry asset was unresolved at runtime.</summary>
    public const string GeometryUnresolvedAtRuntime = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.UnresolvedAtRuntime";

    /// <summary>Camera live sync was rejected.</summary>
    public const string CameraRejected = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Rejected";

    /// <summary>Camera live sync is unsupported.</summary>
    public const string CameraUnsupported = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Unsupported";

    /// <summary>Camera live sync failed.</summary>
    public const string CameraFailed = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Failed";

    /// <summary>Light live sync was rejected.</summary>
    public const string LightRejected = DiagnosticCodes.LiveSyncPrefix + "LIGHT.Rejected";

    /// <summary>Light live sync failed.</summary>
    public const string LightFailed = DiagnosticCodes.LiveSyncPrefix + "LIGHT.Failed";

    /// <summary>Material live sync was rejected.</summary>
    public const string MaterialRejected = DiagnosticCodes.LiveSyncPrefix + "MATERIAL.Rejected";

    /// <summary>Material live sync failed.</summary>
    public const string MaterialFailed = DiagnosticCodes.LiveSyncPrefix + "MATERIAL.Failed";

    /// <summary>Environment atmosphere live sync is unsupported.</summary>
    public const string EnvironmentAtmosphereUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Atmosphere.Unsupported";

    /// <summary>Environment sun live sync is unsupported.</summary>
    public const string EnvironmentSunUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Sun.Unsupported";

    /// <summary>Environment exposure live sync is unsupported.</summary>
    public const string EnvironmentExposureUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Exposure.Unsupported";

    /// <summary>Environment tone-mapping live sync is unsupported.</summary>
    public const string EnvironmentToneMappingUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.ToneMapping.Unsupported";

    /// <summary>Environment background live sync is unsupported.</summary>
    public const string EnvironmentBackgroundUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Background.Unsupported";

    /// <summary>Environment live sync was rejected.</summary>
    public const string EnvironmentRejected = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Rejected";

    /// <summary>Environment live sync failed.</summary>
    public const string EnvironmentFailed = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Failed";
}
