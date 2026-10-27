// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>
/// Finds the nodes of an instance by their names, as a swap keeps changes (scene instances 6.5): a node's name path is
/// one step per node from below the instance root down to it, each the node's name and its order among the siblings
/// that have that name.
/// </summary>
public static class InstanceNamePaths
{
    /// <summary>Gets the name path of a node an instance of a source brings in.</summary>
    /// <param name="structure">The source's structure.</param>
    /// <param name="path">The node's path below the instance root.</param>
    /// <returns>The steps, shallowest first, or <see langword="null"/> when the source has no such node.</returns>
    public static IReadOnlyList<InstanceNameStep>? Of(SceneStructure structure, NodePath path)
    {
        ArgumentNullException.ThrowIfNull(structure);
        ArgumentNullException.ThrowIfNull(path);
        var steps = new List<InstanceNameStep>();
        for (var node = structure.Find(path); node is not null; node = node.Parent is { } parent ? structure.Find(parent) : null)
        {
            var ordinal = structure.ChildrenOf(node.Parent)
                .TakeWhile(sibling => !sibling.Path.Equals(node.Path))
                .Count(sibling => string.Equals(sibling.Name, node.Name, StringComparison.Ordinal));
            steps.Add(new InstanceNameStep(node.Name, ordinal));
            if (node.Parent is null)
            {
                steps.Reverse();
                return steps;
            }
        }

        // The node, or one of its ancestors, is not in the structure.
        return null;
    }

    /// <summary>Finds the node a name path names in a source.</summary>
    /// <param name="structure">The source's structure.</param>
    /// <param name="steps">The name path.</param>
    /// <returns>The node's path below the instance root, or <see langword="null"/> when no node has that name path.</returns>
    public static NodePath? Find(SceneStructure structure, IReadOnlyList<InstanceNameStep> steps)
    {
        ArgumentNullException.ThrowIfNull(structure);
        ArgumentNullException.ThrowIfNull(steps);
        NodePath? parent = null;
        foreach (var step in steps)
        {
            var match = structure.ChildrenOf(parent)
                .Where(node => string.Equals(node.Name, step.Name, StringComparison.Ordinal))
                .ElementAtOrDefault(step.Ordinal);
            if (match is null)
            {
                return null;
            }

            parent = match.Path;
        }

        return parent;
    }

    /// <summary>
    /// Writes a name path in the anchor notation, as <see cref="InstancedNodeChanges.Names"/> records it:
    /// <c>Body/Mirror[2]</c>. Same-named siblings are numbered from the second, unnamed ones from the first, and
    /// <c>/</c>, <c>[</c>, <c>#</c> and <c>\</c> in a name are escaped with <c>\</c>.
    /// </summary>
    /// <param name="steps">The name path.</param>
    /// <returns>The text.</returns>
    public static string Format(IReadOnlyList<InstanceNameStep> steps)
    {
        ArgumentNullException.ThrowIfNull(steps);
        var text = new StringBuilder();
        foreach (var step in steps)
        {
            if (text.Length != 0)
            {
                _ = text.Append('/');
            }

            foreach (var character in step.Name)
            {
                _ = character is '/' or '[' or '#' or '\\' ? text.Append('\\').Append(character) : text.Append(character);
            }

            var number = step.Name.Length == 0 ? step.Ordinal + 1 : step.Ordinal == 0 ? 0 : step.Ordinal + 1;
            if (number != 0)
            {
                _ = text.Append('[').Append(number.ToString(System.Globalization.CultureInfo.InvariantCulture)).Append(']');
            }
        }

        return text.ToString();
    }

    /// <summary>
    /// Carries an instance's changes from its source to another by name: each change's node is looked up in the new
    /// source by the name path it has in the old one and moves to that node's path. A change that finds no node is
    /// kept on its path, marked as carried from the old source.
    /// </summary>
    /// <param name="changes">The instance's changes.</param>
    /// <param name="from">The old source.</param>
    /// <param name="previous">The old source's structure, or <see langword="null"/> when it cannot be read: no change finds a node.</param>
    /// <param name="next">The new source's structure.</param>
    /// <returns>The changes on the new source; each records the names it was found by.</returns>
    public static InstanceChanges Carry(InstanceChanges changes, AssetPath from, SceneStructure? previous, SceneStructure next)
    {
        ArgumentNullException.ThrowIfNull(changes);
        ArgumentNullException.ThrowIfNull(from);
        ArgumentNullException.ThrowIfNull(next);
        var carried = InstanceChanges.None;
        var kept = new List<KeyValuePair<NodePath, InstancedNodeChanges>>();
        foreach (var (path, entry) in changes.Ordered)
        {
            var steps = previous is null ? null : Of(previous, path);
            var names = steps is null ? entry.Names : Format(steps);
            if (steps is not null && Find(next, steps) is { } target)
            {
                carried = carried.With(target, entry.WithNames(names).WithCarriedFrom(source: null));
            }
            else
            {
                kept.Add(KeyValuePair.Create(path, entry.WithNames(names).WithCarriedFrom(entry.CarriedFrom ?? from)));
            }
        }

        // A kept change on a path a found change moved to adds only the values the found one does not set.
        foreach (var (path, entry) in kept)
        {
            carried = carried.With(path, carried.Nodes.TryGetValue(path, out var found) ? Merge(found, entry) : entry);
        }

        return carried;
    }

    private static InstancedNodeChanges Merge(InstancedNodeChanges found, InstancedNodeChanges kept)
    {
        var merged = found;
        foreach (var (kind, values) in kept.Components)
        {
            var target = merged.Of(kind);
            foreach (var value in values.All)
            {
                target = target.Find(value.Property, value.Part) is null ? target.With(value) : target;
            }

            merged = merged.With(target);
        }

        foreach (var (slot, values) in kept.Parameters)
        {
            foreach (var (name, value) in values)
            {
                merged = merged.Parameters.GetValueOrDefault(slot)?.ContainsKey(name) == true ? merged : merged.WithParameter(slot, name, value);
            }
        }

        return merged;
    }
}
