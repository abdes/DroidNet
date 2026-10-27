// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Nodes;

namespace Oxygen.Editor.SceneInspector.Components.Instances;

/// <summary>A kept change as the caution box of the Scene Instance section lists it (scene instances 8.10).</summary>
public sealed class KeptChangeRow
{
    /// <summary>Initializes a new instance of the <see cref="KeptChangeRow"/> class.</summary>
    /// <param name="change">The kept change.</param>
    public KeptChangeRow(KeptChange change)
    {
        ArgumentNullException.ThrowIfNull(change);
        this.Change = change;
    }

    /// <summary>Gets the kept change.</summary>
    public KeptChange Change { get; }

    /// <summary>Gets the node's last known names, such as <c>Body/Door_FL</c>.</summary>
    public string Names => this.Change.Names;

    /// <summary>Gets the properties the change sets, and the source a swap carried it from.</summary>
    public string Detail => this.Change.CarriedFrom is { } carried
        ? $"{string.Join(", ", this.Change.Properties)} · carried from {carried.Name}"
        : string.Join(", ", this.Change.Properties);
}
