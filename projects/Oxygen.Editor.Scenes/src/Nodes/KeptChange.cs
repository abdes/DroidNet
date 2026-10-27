// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>A kept change: an instance's changes of a node its source no longer holds (scene instances 3).</summary>
/// <param name="Path">The node path the change names.</param>
/// <param name="Changes">The changes.</param>
public sealed record KeptChange(NodePath Path, InstancedNodeChanges Changes)
{
    /// <summary>Gets the node's last known names, or its path when they were never known.</summary>
    public string Names => this.Changes.Names ?? this.Path.ToString();

    /// <summary>Gets the source a swap carried the change over from, or <see langword="null"/> when made on the current one.</summary>
    public AssetPath? CarriedFrom => this.Changes.CarriedFrom;

    /// <summary>Gets the properties the change sets, each once: a component property, or a script slot's parameter.</summary>
    public IReadOnlyList<string> Properties
        => [.. this.Changes.Components.OrderBy(static entry => entry.Key, StringComparer.Ordinal)
                .SelectMany(static entry => entry.Value.All.Select(static value => value.Property.Qualified()))
                .Distinct(StringComparer.Ordinal),
            .. this.Changes.Parameters.SelectMany(static slot => slot.Value.Keys.Select(name => string.Format(CultureInfo.InvariantCulture, "Script {0:D} / {1}", slot.Key, name)))];

    /// <summary>Counts the values a node's changes set.</summary>
    /// <param name="changes">The node's changes.</param>
    /// <returns>The number of component values and script parameters changed.</returns>
    public static int CountValues(InstancedNodeChanges changes)
    {
        ArgumentNullException.ThrowIfNull(changes);
        return changes.Components.Values.Sum(static component => component.All.Count()) + changes.Parameters.Values.Sum(static values => values.Count);
    }
}
