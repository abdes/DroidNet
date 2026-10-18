// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Re-points the node references held by copied nodes. A reference to a node copied in the same operation points at
/// that node's copy; any other reference is kept, so it still names the original node, or shows as missing in a scene
/// that does not contain it.
/// </summary>
internal static class CopiedNodeReferences
{
    /// <summary>Re-points the node references of a copied node and every node below it.</summary>
    /// <param name="root">The copied subtree root.</param>
    /// <param name="copies">Maps each copied node identity to the identity of its copy.</param>
    public static void RemapSubtree(SceneNode root, IReadOnlyDictionary<NodeId, NodeId> copies)
    {
        ArgumentNullException.ThrowIfNull(root);
        ArgumentNullException.ThrowIfNull(copies);
        foreach (var scripting in new[] { root }.Concat(root.Descendants()).SelectMany(static node => node.Components.OfType<ScriptingComponent>()))
        {
            if (Remap(scripting.Slots, copies) is { } slots)
            {
                scripting.Slots = slots;
            }
        }
    }

    /// <summary>Re-points the node values of script slots.</summary>
    /// <param name="slots">The slots.</param>
    /// <param name="copies">Maps each copied node identity to the identity of its copy.</param>
    /// <returns>The re-pointed slots, or null when nothing changed.</returns>
    public static ImmutableArray<ScriptSlot>? Remap(ImmutableArray<ScriptSlot> slots, IReadOnlyDictionary<NodeId, NodeId> copies)
    {
        ArgumentNullException.ThrowIfNull(copies);
        var changed = false;
        var result = slots.Select(slot =>
        {
            var updated = slot;
            foreach (var (name, value) in slot.Overrides)
            {
                if (value.Kind == ScriptValueKind.NodeValue && copies.TryGetValue(value.NodeId, out var copy))
                {
                    updated = updated.WithOverride(name, ScriptValue.FromNode(copy));
                }
            }

            changed |= !ReferenceEquals(updated, slot);
            return updated;
        }).ToImmutableArray();
        return changed ? result : null;
    }
}
