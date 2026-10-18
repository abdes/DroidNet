// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.World.Components;

/// <summary>
/// One script attached to a node: the script, when it runs relative to other scripts, and the parameter values that
/// differ from the script's declared defaults.
/// </summary>
public sealed record ScriptSlot
{
    /// <summary>Gets the stable slot identity, cooked as the slot id.</summary>
    public Guid Id { get; init; } = Guid.NewGuid();

    /// <summary>Gets the script asset identity, such as <c>asset:///Content/Player/move.oscript</c>; null until one is
    /// picked. A reference to a script that no longer exists stays until changed.</summary>
    public Uri? Script { get; init; }

    /// <summary>Gets the execution order; lower runs earlier, across all nodes.</summary>
    public int ExecutionOrder { get; init; }

    /// <summary>Gets the parameter values that differ from the declared defaults, by parameter name.</summary>
    public ImmutableSortedDictionary<string, ScriptValue> Overrides { get; init; }
        = ImmutableSortedDictionary.Create<string, ScriptValue>(StringComparer.Ordinal);

    /// <summary>Gets a slot with one parameter value set.</summary>
    /// <param name="name">The parameter name.</param>
    /// <param name="value">The value.</param>
    /// <returns>The updated slot.</returns>
    public ScriptSlot WithOverride(string name, ScriptValue value)
    {
        ArgumentException.ThrowIfNullOrEmpty(name);
        ArgumentNullException.ThrowIfNull(value);
        return this with { Overrides = this.Overrides.SetItem(name, value) };
    }

    /// <summary>Gets a slot with parameter values removed, so they take their declared defaults.</summary>
    /// <param name="names">The parameter names.</param>
    /// <returns>The updated slot.</returns>
    public ScriptSlot WithoutOverrides(IEnumerable<string> names)
    {
        ArgumentNullException.ThrowIfNull(names);
        return this with { Overrides = this.Overrides.RemoveRange(names) };
    }

    /// <inheritdoc />
    public bool Equals(ScriptSlot? other)
        => other is not null
            && this.Id == other.Id
            && Equals(this.Script, other.Script)
            && this.ExecutionOrder == other.ExecutionOrder
            && this.Overrides.Count == other.Overrides.Count
            && this.Overrides.All(entry => other.Overrides.TryGetValue(entry.Key, out var value) && entry.Value.Equals(value));

    /// <inheritdoc />
    public override int GetHashCode() => HashCode.Combine(this.Id, this.Script, this.ExecutionOrder, this.Overrides.Count);
}
