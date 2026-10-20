// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Scripting;

/// <summary>Identifies one script slot of a node. It is the slot's stable identity, cooked as the slot id.</summary>
/// <param name="Value">The underlying value.</param>
[JsonConverter(typeof(GuidIdJsonConverter<ScriptSlotId>))]
public readonly record struct ScriptSlotId(Guid Value) : IGuidId<ScriptSlotId>
{
    /// <summary>Gets the id that identifies no slot.</summary>
    public static ScriptSlotId Empty { get; }

    /// <summary>Gets a value indicating whether this id identifies no slot.</summary>
    public bool IsEmpty => this.Value == Guid.Empty;

    /// <summary>Creates a new, unique slot id.</summary>
    /// <returns>The new id.</returns>
    public static ScriptSlotId New() => new(Guid.NewGuid());

    /// <inheritdoc/>
    public static ScriptSlotId FromValue(Guid value) => new(value);

    /// <inheritdoc/>
    public override string ToString() => this.Value.ToString("D");
}
