// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
/// The path of a node an instance brings in, below its instance root: one node ID per nesting level, shallowest first.
/// </summary>
/// <remarks>
/// Each ID names a node of the scene the previous level instances, the first one a node of the instance root's source.
/// Every ID but the last names an instance root. The empty path names the instance root itself. Paths compare by
/// value, and <see cref="ToString"/> is their canonical form: the IDs joined by <c>/</c>.
/// </remarks>
public sealed class NodePath : IEquatable<NodePath>
{
    private const char Separator = '/';

    private NodePath(ImmutableArray<NodeId> nodes)
    {
        this.Nodes = nodes;
    }

    /// <summary>
    /// Gets the path with no node.
    /// </summary>
    public static NodePath Empty { get; } = new([]);

    /// <summary>
    /// Gets the node IDs, shallowest first.
    /// </summary>
    public ImmutableArray<NodeId> Nodes { get; }

    /// <summary>
    /// Gets a value indicating whether the path names no node.
    /// </summary>
    public bool IsEmpty => this.Nodes.IsEmpty;

    /// <summary>
    /// Gets the number of nesting levels.
    /// </summary>
    public int Count => this.Nodes.Length;

    /// <summary>
    /// Creates a path.
    /// </summary>
    /// <param name="nodes">The node IDs, shallowest first.</param>
    /// <returns>The path.</returns>
    /// <exception cref="ArgumentException">A node ID is empty.</exception>
    public static NodePath Of(params IEnumerable<NodeId> nodes)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        var ids = nodes.ToImmutableArray();
        return ids.Any(static id => id.IsEmpty)
            ? throw new ArgumentException("A node path cannot hold an empty node ID.", nameof(nodes))
            : ids.IsEmpty ? Empty : new(ids);
    }

    /// <summary>
    /// Reads a path from its canonical form.
    /// </summary>
    /// <param name="text">The node IDs joined by <c>/</c>; empty for the empty path.</param>
    /// <param name="path">The path.</param>
    /// <returns>Whether the text is a path.</returns>
    public static bool TryParse(string? text, [NotNullWhen(true)] out NodePath? path)
    {
        path = null;
        if (text is null)
        {
            return false;
        }

        if (text.Length == 0)
        {
            path = Empty;
            return true;
        }

        var ids = ImmutableArray.CreateBuilder<NodeId>();
        foreach (var element in text.Split(Separator))
        {
            if (!Guid.TryParseExact(element, "D", out var value) || value == Guid.Empty)
            {
                return false;
            }

            ids.Add(NodeId.FromValue(value));
        }

        path = new(ids.ToImmutable());
        return true;
    }

    /// <summary>
    /// Gets the path one level deeper.
    /// </summary>
    /// <param name="node">The node of the last level's scene.</param>
    /// <returns>The longer path.</returns>
    public NodePath Append(NodeId node)
        => node.IsEmpty ? throw new ArgumentException("A node path cannot hold an empty node ID.", nameof(node)) : new(this.Nodes.Add(node));

    /// <inheritdoc/>
    public bool Equals(NodePath? other) => other is not null && this.Nodes.SequenceEqual(other.Nodes);

    /// <inheritdoc/>
    public override bool Equals(object? obj) => this.Equals(obj as NodePath);

    /// <inheritdoc/>
    public override int GetHashCode()
    {
        var hash = default(HashCode);
        foreach (var node in this.Nodes)
        {
            hash.Add(node);
        }

        return hash.ToHashCode();
    }

    /// <summary>
    /// Gets the canonical form: the node IDs joined by <c>/</c>.
    /// </summary>
    /// <returns>The canonical form.</returns>
    public override string ToString() => string.Join(Separator, this.Nodes);
}
