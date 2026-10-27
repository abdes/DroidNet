// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Properties;

/// <summary>The asset whose inventory lists a node's parts, such as the geometry that lists its surfaces.</summary>
internal static class PartOwners
{
    /// <summary>Resolves the asset a part of a node belongs to now.</summary>
    /// <param name="scope">The scene and its ports.</param>
    /// <param name="node">The node of the scene: the node itself, or the instance root of <paramref name="path"/>.</param>
    /// <param name="path">The node below the instance root, or <see langword="null"/> for the node itself.</param>
    /// <param name="partKind">The part's kind.</param>
    /// <returns>
    /// For a node of the scene, its component's asset. For a node an instance brings in, the geometry its instance's
    /// structure gives it, which no change replaces. <see langword="null"/> when the node has no such asset, or its
    /// instance's structure is not read.
    /// </returns>
    public static AssetPath? Resolve(SceneEditScope scope, SceneNode node, NodePath? path, string partKind)
    {
        ArgumentNullException.ThrowIfNull(scope);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentException.ThrowIfNullOrWhiteSpace(partKind);
        if (path is null)
        {
            return node.Components.OfType<IOverridableComponent>().Select(component => component.PartOwner(partKind)).FirstOrDefault(static owner => owner is not null);
        }

        return string.Equals(partKind, SceneSchema.Geometry.Surface.Kind, StringComparison.Ordinal)
            && node.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance
            ? scope.StructureOf?.Invoke(instance.Source)?.Find(path)?.Geometry
            : null;
    }
}
