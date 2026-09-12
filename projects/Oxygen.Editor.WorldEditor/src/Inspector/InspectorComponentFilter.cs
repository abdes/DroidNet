// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace Oxygen.Editor.World.Inspector;

/// <summary>A component type available for filtering the current node selection.</summary>
/// <param name="componentType">The component type, independent of any selected instance.</param>
/// <param name="label">The friendly type label.</param>
public sealed partial class InspectorComponentFilter(Type componentType, string label) : ObservableObject
{
    /// <summary>Gets the component type represented by this row.</summary>
    public Type ComponentType { get; } = componentType;

    /// <summary>Gets the friendly component type label.</summary>
    public string Label { get; } = label;

    /// <summary>Gets or sets the explanation when this type cannot be edited across the current selection.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ToolTip))]
    public partial string UnavailableReason { get; set; } = string.Empty;

    /// <summary>Gets the full label and any availability explanation for the row tooltip.</summary>
    public string ToolTip => this.UnavailableReason.Length == 0 ? this.Label : $"{this.Label}: {this.UnavailableReason}";

    /// <summary>Gets or sets a value indicating whether the component is present on every selected node.</summary>
    [ObservableProperty]
    public partial bool IsAvailable { get; set; }

    /// <summary>Gets or sets a compact explanation of partial availability.</summary>
    [ObservableProperty]
    public partial string StatusLabel { get; set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether this type currently filters the property sections.</summary>
    [ObservableProperty]
    public partial bool IsSelected { get; set; }
}
