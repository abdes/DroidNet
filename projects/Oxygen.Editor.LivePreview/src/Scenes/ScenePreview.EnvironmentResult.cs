// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Reports the engine's outcome for each authored environment field.</summary>
internal sealed partial class ScenePreview
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
