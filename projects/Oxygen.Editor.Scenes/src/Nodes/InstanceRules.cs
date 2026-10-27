// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>The rules every instance edit and every source picker follows (scene instances 8.3).</summary>
public static class InstanceRules
{
    /// <summary>
    /// Checks whether instancing a source in a scene would make a cycle: the source is the scene, or references it
    /// through the edges the cook dependency graph follows, so the scene could never cook.
    /// </summary>
    /// <param name="scene">The scene the instance would be in.</param>
    /// <param name="source">The instance's source: a model's source file or an authored scene.</param>
    /// <param name="references">The assets an asset references directly, such as a scene's instanced sources.</param>
    /// <returns><see langword="true"/> when the source is or reaches the scene.</returns>
    public static bool WouldCycle(AssetPath scene, AssetPath source, Func<AssetPath, IEnumerable<AssetPath>> references)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(references);
        var target = AssetFileTypes.ToIdentity(scene);
        var pending = new Queue<AssetPath>([AssetFileTypes.ToIdentity(source)]);
        var visited = new HashSet<AssetPath>();
        while (pending.TryDequeue(out var asset))
        {
            if (asset == target)
            {
                return true;
            }

            if (visited.Add(asset))
            {
                foreach (var referenced in references(asset))
                {
                    pending.Enqueue(AssetFileTypes.ToIdentity(referenced));
                }
            }
        }

        return false;
    }

    /// <summary>
    /// Checks whether a structural command (rename, delete, cut, duplicate, drag, reparent, reorder, paste into, a new
    /// child, adding or removing a component) must refuse its targets: a command over targets that hold a node an
    /// instance brought in is refused as a whole, since that node's structure belongs to the instance's source.
    /// </summary>
    /// <param name="scene">The scene the targets are in.</param>
    /// <param name="targets">The command's targets.</param>
    /// <param name="nameOf">The name of a node an instance brought in, when the caller knows it.</param>
    /// <returns>
    /// The <see cref="SceneEditCodes.InstancedStructure"/> error naming the first such node, its source and what to do
    /// instead; otherwise, <see langword="null"/>.
    /// </returns>
    public static SceneDiagnostic? RefusesStructure(Scene scene, IEnumerable<NodeReference> targets, Func<NodeReference, string?>? nameOf = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(targets);
        if (targets.FirstOrDefault(static target => !target.IsOwned) is not { } instanced)
        {
            return null;
        }

        var root = scene.AllNodes.FirstOrDefault(node => node.Id == instanced.Node);
        var rootName = root?.Metadata.Name ?? instanced.Node.ToString();
        var source = root?.Components.OfType<SceneInstanceComponent>().FirstOrDefault()?.Source.Name;
        var subject = nameOf?.Invoke(instanced) is { Length: > 0 } name ? $"'{name}'" : $"This node of {rootName}";
        var origin = source is null ? $"{subject} comes from its instance's source." : $"{subject} comes from {source}.";
        return SceneDiagnostic.Error(
            SceneEditCodes.InstancedStructure,
            $"{origin} Unpack {rootName} to change its structure.",
            instanced.Node);
    }
}
