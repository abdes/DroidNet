// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
///     Identifies a scene node. It is authored in the scene file and keeps its value for the node's lifetime.
/// </summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<NodeId>))]
public readonly record struct NodeId(Guid Value) : IGuidId<NodeId>
{
    /// <summary>
    ///     Gets the id that identifies no node.
    /// </summary>
    public static NodeId Empty { get; }

    /// <summary>
    ///     Gets a value indicating whether this id identifies no node.
    /// </summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>
    ///     Creates a new, unique node id.
    /// </summary>
    /// <returns>The new id.</returns>
    public static NodeId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static NodeId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
