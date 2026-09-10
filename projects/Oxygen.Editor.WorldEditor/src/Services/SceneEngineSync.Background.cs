// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>Keeps background application and field outcomes independent from other environment systems.</summary>
public sealed partial class SceneEngineSync
{
    private static EnvironmentSyncResult EnvironmentResult(SyncOutcome sun, SyncOutcome environment, SyncOutcome background)
    {
        var fields = new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)
        {
            [nameof(SceneEnvironmentData.AtmosphereEnabled)] = environment,
            [nameof(SceneEnvironmentData.SunNodeId)] = sun,
            [nameof(SceneEnvironmentData.ExposureMode)] = environment,
            [nameof(SceneEnvironmentData.ManualExposureEv)] = environment,
            [nameof(SceneEnvironmentData.ExposureCompensation)] = environment,
            [nameof(SceneEnvironmentData.ToneMapping)] = environment,
            [nameof(SceneEnvironmentData.BackgroundColor)] = background,
            [nameof(SceneEnvironmentData.SkyAtmosphere)] = environment,
            [nameof(SceneEnvironmentData.PostProcess)] = environment,
        };
        foreach (var (field, outcome) in fields.ToArray())
        {
            fields[field] = outcome with
            {
                Scope = outcome.Scope with { ComponentType = nameof(SceneEnvironmentData), ComponentName = field },
            };
        }

        return new(Worst(fields.Values.Select(value => value.Status)), fields);
    }

    private async Task<SyncOutcome> SyncBackgroundAsync(Scene scene, SceneEnvironmentData environment, CancellationToken cancellationToken, WorldDispatch? dispatchOverride = null)
    {
        var outcome = await this.ExecuteSceneSyncAsync(
            scene,
            SceneOperationKinds.EditEnvironment,
            LiveSyncDiagnosticCodes.EnvironmentBackgroundRejected,
            LiveSyncDiagnosticCodes.EnvironmentBackgroundFailed,
            world => world.Execute(new RuntimeSetBackgroundColor(environment.BackgroundColor)),
            cancellationToken,
            dispatchOverride).ConfigureAwait(false);
        return outcome.Status == SyncStatus.Unsupported
            ? outcome with { Code = LiveSyncDiagnosticCodes.EnvironmentBackgroundUnsupported } : outcome;
    }
}
