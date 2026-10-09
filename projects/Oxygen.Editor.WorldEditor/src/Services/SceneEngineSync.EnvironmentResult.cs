// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Services;

/// <summary>Reports one environment application outcome for every authored environment field.</summary>
public sealed partial class SceneEngineSync
{
    private static EnvironmentSyncResult EnvironmentResult(SyncOutcome environment)
    {
        var fields = new Dictionary<string, SyncOutcome>(StringComparer.Ordinal);
        foreach (var field in new[]
        {
            nameof(SceneEnvironmentData.AtmosphereEnabled),
            nameof(SceneEnvironmentData.SkyAtmosphere),
            nameof(SceneEnvironmentData.PostProcess),
            nameof(SceneEnvironmentData.Fog),
            nameof(SceneEnvironmentData.SkySphere),
            nameof(SceneEnvironmentData.SkyLight),
            nameof(SceneEnvironmentData.Background),
        })
        {
            fields[field] = environment with
            {
                Scope = environment.Scope with { ComponentType = nameof(SceneEnvironmentData), ComponentName = field },
            };
        }

        return new(Worst(fields.Values.Select(value => value.Status)), fields);
    }
}
