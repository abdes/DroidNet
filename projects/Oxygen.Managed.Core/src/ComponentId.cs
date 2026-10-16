// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;

namespace Oxygen.Managed.Core;

/// <summary>
///     Identifies a component of a scene node. It is authored in the scene file and is unique within its node.
/// </summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<ComponentId>))]
public readonly record struct ComponentId(Guid Value) : IGuidId<ComponentId>
{
    /// <summary>
    ///     Gets the id that identifies no component.
    /// </summary>
    public static ComponentId Empty { get; }

    /// <summary>
    ///     Gets a value indicating whether this id identifies no component.
    /// </summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>
    ///     Creates a new, unique component id.
    /// </summary>
    /// <returns>The new id.</returns>
    public static ComponentId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static ComponentId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
