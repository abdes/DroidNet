// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Instances;

/// <summary>
/// Retargets the references to the nodes of an unpacked instance (scene instances 6.8, 8.7): the starting camera, the
/// hidden and locked nodes, and the node values of scripts and of instance changes.
/// </summary>
internal static class UnpackedReferences
{
    /// <summary>Maps a reference to a node of the instance to its copy, once the instance is unpacked.</summary>
    /// <param name="instance">The instance root.</param>
    /// <param name="copies">The unpacked nodes.</param>
    /// <returns>
    /// The copy of the deepest node of the reference's path that has one, with the rest of the path below it; null for a
    /// reference that names no node of the instance.
    /// </returns>
    public static Func<NodeReference, NodeReference?> Forward(NodeId instance, IReadOnlyList<UnpackedNode> copies)
    {
        var copyOf = copies.ToDictionary(static node => node.Path, static node => node.Copy);
        var pathOf = copies.ToDictionary(static node => node.Copy, static node => node.Path);
        return reference =>
        {
            // A node below a copy that stays an instance root is still a node of the instance, one level down.
            var path = reference.Node == instance ? reference.Path
                : !reference.IsOwned && pathOf.TryGetValue(reference.Node, out var unpacked) ? NodePath.Of(unpacked.Nodes.AddRange(reference.Path.Nodes))
                : NodePath.Empty;
            for (var count = path.Count; count > 0; count--)
            {
                if (copyOf.TryGetValue(NodePath.Of(path.Nodes.Take(count)), out var copy))
                {
                    return new NodeReference(copy, NodePath.Of(path.Nodes.Skip(count)));
                }
            }

            return null;
        };
    }

    /// <summary>Maps a reference to an unpacked node back to the node of the instance it was made of.</summary>
    /// <param name="instance">The instance root.</param>
    /// <param name="copies">The unpacked nodes.</param>
    /// <returns>The reference by the instance root and the node's path; null for a reference that names no copy.</returns>
    public static Func<NodeReference, NodeReference?> Backward(NodeId instance, IReadOnlyList<UnpackedNode> copies)
    {
        var pathOf = copies.ToDictionary(static node => node.Copy, static node => node.Path);
        return reference => pathOf.TryGetValue(reference.Node, out var path)
            ? new NodeReference(instance, NodePath.Of(path.Nodes.AddRange(reference.Path.Nodes)))
            : null;
    }

    /// <summary>Retargets every reference of the scene and its editing state that the map names another node for.</summary>
    /// <param name="scope">The edit's scope.</param>
    /// <param name="map">The node a reference names now, or null when it names the same node.</param>
    /// <returns>What changed.</returns>
    public static IReadOnlyList<SceneChange> Retarget(SceneEditScope scope, Func<NodeReference, NodeReference?> map)
    {
        var changes = new List<SceneChange>();
        var scene = scope.Scene;
        if (scene.StartingCamera is { } camera && map(camera) is { } starting && starting != camera)
        {
            scene.SetStartingCamera(starting);
            changes.Add(new SceneChange.StartingCameraChanged());
        }

        foreach (var node in scene.AllNodes)
        {
            foreach (var scripting in node.Components.OfType<ScriptingComponent>())
            {
                var slots = scripting.Slots.Select(slot => Retarget(slot, map)).ToArray();
                if (!slots.SequenceEqual(scripting.Slots))
                {
                    scripting.Slots = [.. slots];
                    changes.Add(new SceneChange.ScriptSlotsChanged(node.Id));
                }
            }

            foreach (var instance in node.Components.OfType<SceneInstanceComponent>())
            {
                var retargeted = instance.Changes;
                foreach (var (path, nodeChanges) in instance.Changes.Nodes)
                {
                    retargeted = retargeted.With(path, nodeChanges.MapNodes(map));
                }

                if (!retargeted.Equals(instance.Changes))
                {
                    instance.Changes = retargeted;
                    changes.Add(new SceneChange.OverridesChanged(node.Id, []));
                }
            }
        }

        if (scope.Interaction?.Retarget(map) is { Count: > 0 } hidden)
        {
            changes.Add(new SceneChange.EditorVisibilityChanged(hidden));
        }

        return changes;
    }

    private static ScriptSlot Retarget(ScriptSlot slot, Func<NodeReference, NodeReference?> map)
    {
        var retargeted = slot;
        foreach (var (name, value) in slot.Overrides)
        {
            if (value.Kind == ScriptValueKind.NodeValue && value.Node is { } reference && map(reference) is { } target && target != reference)
            {
                retargeted = retargeted.WithOverride(name, ScriptValue.FromNode(target));
            }
        }

        return retargeted;
    }
}
