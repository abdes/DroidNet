// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for live scene synchronization.
/// </summary>
public static class LiveSyncDiagnosticCodes
{
    public const string NotRunning = DiagnosticCodes.LiveSyncPrefix + "NotRunning";

    public const string RuntimeFaulted = DiagnosticCodes.LiveSyncPrefix + "RuntimeFaulted";

    public const string Cancelled = DiagnosticCodes.LiveSyncPrefix + "Cancelled";

    public const string TransformRejected = DiagnosticCodes.LiveSyncPrefix + "TRANSFORM.Rejected";

    public const string TransformFailed = DiagnosticCodes.LiveSyncPrefix + "TRANSFORM.Failed";

    public const string GeometryRejected = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.Rejected";

    public const string GeometryFailed = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.Failed";

    public const string GeometryUnresolvedAtRuntime = DiagnosticCodes.LiveSyncPrefix + "GEOMETRY.UnresolvedAtRuntime";

    public const string CameraRejected = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Rejected";

    public const string CameraUnsupported = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Unsupported";

    public const string CameraFailed = DiagnosticCodes.LiveSyncPrefix + "CAMERA.Failed";

    public const string LightRejected = DiagnosticCodes.LiveSyncPrefix + "LIGHT.Rejected";

    public const string LightFailed = DiagnosticCodes.LiveSyncPrefix + "LIGHT.Failed";

    public const string MaterialUnsupported = DiagnosticCodes.LiveSyncPrefix + "MATERIAL.Unsupported";

    public const string EnvironmentAtmosphereUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Atmosphere.Unsupported";

    public const string EnvironmentSunUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Sun.Unsupported";

    public const string EnvironmentExposureUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Exposure.Unsupported";

    public const string EnvironmentToneMappingUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.ToneMapping.Unsupported";

    public const string EnvironmentBackgroundUnsupported = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Background.Unsupported";

    public const string EnvironmentRejected = DiagnosticCodes.LiveSyncPrefix + "ENVIRONMENT.Rejected";
}
