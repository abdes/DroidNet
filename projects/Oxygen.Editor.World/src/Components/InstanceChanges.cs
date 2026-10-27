// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// Every change a scene makes below one instance root, per node path.
/// </summary>
/// <remarks>
/// The value is immutable: every change returns a new one, so the component replaces it as a whole and an edit's inverse
/// is the previous value. A node left without changes is removed. A change whose path no longer resolves is kept until
/// it is removed.
/// </remarks>
public sealed class InstanceChanges : IEquatable<InstanceChanges>
{
    private InstanceChanges(ImmutableDictionary<NodePath, InstancedNodeChanges> nodes)
    {
        this.Nodes = nodes;
    }

    /// <summary>
    /// Gets the changes that change nothing.
    /// </summary>
    public static InstanceChanges None { get; } = new([]);

    /// <summary>
    /// Gets the changes of each node path that has any.
    /// </summary>
    public ImmutableDictionary<NodePath, InstancedNodeChanges> Nodes { get; }

    /// <summary>
    /// Gets a value indicating whether nothing is changed.
    /// </summary>
    public bool IsEmpty => this.Nodes.IsEmpty;

    /// <summary>
    /// Gets the changed nodes in their canonical order: by the canonical form of their paths.
    /// </summary>
    public IEnumerable<KeyValuePair<NodePath, InstancedNodeChanges>> Ordered
        => this.Nodes.OrderBy(static entry => entry.Key.ToString(), StringComparer.Ordinal);

    /// <summary>
    /// Gets every asset a change references.
    /// </summary>
    public IEnumerable<AssetPath> Assets => this.Ordered.SelectMany(static entry => entry.Value.Assets);

    /// <summary>
    /// Gets the changes of one node.
    /// </summary>
    /// <param name="path">The node's path below the instance root.</param>
    /// <returns>The node's changes; empty when it is not changed.</returns>
    public InstancedNodeChanges Of(NodePath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return this.Nodes.GetValueOrDefault(path) ?? InstancedNodeChanges.None;
    }

    /// <summary>
    /// Replaces the changes of one node.
    /// </summary>
    /// <param name="path">The node's path below the instance root.</param>
    /// <param name="changes">The node's changes; empty removes them.</param>
    /// <returns>The changes with the node's.</returns>
    /// <exception cref="ArgumentException">The path is empty: the instance root is a node of the scene.</exception>
    public InstanceChanges With(NodePath path, InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(path);
        ArgumentNullException.ThrowIfNull(changes);
        if (path.IsEmpty)
        {
            throw new ArgumentException("A change names a node below the instance root.", nameof(path));
        }

        return new(changes.IsEmpty ? this.Nodes.Remove(path) : this.Nodes.SetItem(path, changes));
    }

    /// <summary>
    /// Replaces every asset a change references.
    /// </summary>
    /// <param name="map">Returns the asset that replaces one, or the same asset.</param>
    /// <returns>The changes with the replaced assets.</returns>
    public InstanceChanges MapAssets(Func<AssetPath, AssetPath> map)
    {
        ArgumentNullException.ThrowIfNull(map);
        var nodes = this.Nodes;
        foreach (var (path, changes) in this.Nodes)
        {
            var mapped = changes.MapAssets(map);
            nodes = ReferenceEquals(mapped, changes) ? nodes : nodes.SetItem(path, mapped);
        }

        return ReferenceEquals(nodes, this.Nodes) ? this : new(nodes);
    }

    /// <inheritdoc/>
    public bool Equals(InstanceChanges? other)
        => other is not null
            && this.Nodes.Count == other.Nodes.Count
            && this.Nodes.All(entry => other.Nodes.TryGetValue(entry.Key, out var changes) && entry.Value.Equals(changes));

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as InstanceChanges);

    /// <inheritdoc/>
    public override int GetHashCode() => this.Nodes.Count;
}
