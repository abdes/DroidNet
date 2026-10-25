// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Serialization;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World;

/// <summary>
/// The one way the scene model names a node: a node of the scene, or a node an instance brings in, as the cooked
/// scene's node reference does.
/// </summary>
/// <param name="Node">A node of the scene; an instance root when <paramref name="Path"/> is not empty.</param>
/// <param name="Path">The node below the instance root, or the empty path for the node itself.</param>
/// <remarks>
/// The canonical form is the node's ID followed by each path element, joined by <c>/</c>, so a reference to a node
/// of the scene is the node's ID alone.
/// </remarks>
[JsonConverter(typeof(NodeReferenceJsonConverter))]
public sealed record NodeReference(NodeId Node, NodePath Path)
{
    /// <summary>
    /// Gets the node.
    /// </summary>
    /// <exception cref="ArgumentException">The node ID is empty.</exception>
    public NodeId Node { get; init; } = Node.IsEmpty ? throw new ArgumentException("A node reference needs a node.", nameof(Node)) : Node;

    /// <summary>
    /// Gets the path below the node.
    /// </summary>
    public NodePath Path { get; init; } = Path ?? throw new ArgumentNullException(nameof(Path));

    /// <summary>
    /// Gets a value indicating whether the reference names a node of the scene itself.
    /// </summary>
    public bool IsOwned => this.Path.IsEmpty;

    /// <summary>
    /// Creates a reference to a node of the scene.
    /// </summary>
    /// <param name="node">The node.</param>
    /// <returns>The reference.</returns>
    public static NodeReference To(NodeId node) => new(node, NodePath.Empty);

    /// <summary>
    /// Reads a reference from its canonical form.
    /// </summary>
    /// <param name="text">The canonical form.</param>
    /// <param name="reference">The reference.</param>
    /// <returns>Whether the text is a reference.</returns>
    public static bool TryParse(string? text, [NotNullWhen(true)] out NodeReference? reference)
    {
        reference = null;
        if (string.IsNullOrEmpty(text))
        {
            return false;
        }

        var separator = text.IndexOf('/', StringComparison.Ordinal);
        var head = separator < 0 ? text : text[..separator];
        if (!Guid.TryParseExact(head, "D", out var node) || node == Guid.Empty
            || !NodePath.TryParse(separator < 0 ? string.Empty : text[(separator + 1)..], out var path)
            || (separator >= 0 && path.IsEmpty))
        {
            return false;
        }

        reference = new(NodeId.FromValue(node), path);
        return true;
    }

    /// <summary>
    /// Gets the canonical form.
    /// </summary>
    /// <returns>The node's ID, then each path element, joined by <c>/</c>.</returns>
    public override string ToString() => this.Path.IsEmpty ? this.Node.ToString() : $"{this.Node}/{this.Path}";
}
