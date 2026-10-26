// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
}
