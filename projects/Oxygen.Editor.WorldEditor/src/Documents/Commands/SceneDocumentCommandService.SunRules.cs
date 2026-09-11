// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Keeps discrete sun authoring actions consistent with the native light contract.</summary>
public sealed partial class SceneDocumentCommandService
{
    private static void ApplyDirectionalSunRules(
        Scene scene,
        List<(SceneNode node, DirectionalLightComponent? light)> targets,
        DirectionalLightEdit edit)
    {
        if (edit.EnvironmentContribution.HasValue && !edit.EnvironmentContribution.Value)
        {
            foreach (var (node, light) in targets)
            {
                light!.IsSunLight = false;
                if (scene.Environment.SunNodeId == node.Id)
                {
                    scene.SetEnvironment(scene.Environment with { SunNodeId = null });
                }
            }

            return;
        }

        if (!edit.IsSunLight.HasValue)
        {
            return;
        }

        if (edit.IsSunLight.Value)
        {
            var sun = targets[0].node;
            ApplyExclusiveSun(scene, sun.Id);
            scene.SetEnvironment(scene.Environment with { SunNodeId = sun.Id });
        }
        else if (targets.Exists(target => target.node.Id == scene.Environment.SunNodeId))
        {
            scene.SetEnvironment(scene.Environment with { SunNodeId = null });
        }
    }

    private async Task<Guid?> PublishEnvironmentAndSunSyncAsync(
        SceneDocumentCommandContext context,
        SceneEnvironmentData environment,
        IReadOnlyList<DirectionalSunState> before,
        IReadOnlyList<DirectionalSunState> after,
        SceneSyncRevision revision)
    {
        var changed = IncludeDirectionalSunChangedNodes([], before, after);
        var lightResult = await this.SyncEditedNodesAsync(
            context,
            changed,
            SceneOperationKinds.EditDirectionalLight,
            node => this.sceneEngineSync.UpdatePropertiesAsync(context.Scene, node, BuildDirectionalLightPropertyEntries(node.Components.OfType<DirectionalLightComponent>().First()), revision)).ConfigureAwait(true);
        var environmentResult = await this.PublishEnvironmentSyncAsync(context, environment, revision).ConfigureAwait(true);
        return lightResult ?? environmentResult;
    }
}
