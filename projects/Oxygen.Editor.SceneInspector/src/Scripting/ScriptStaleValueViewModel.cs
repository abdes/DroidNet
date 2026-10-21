// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.Scripting;

/// <summary>
/// A value a slot holds for a parameter its script no longer declares, or declares with another type. The cook fails
/// until it is removed.
/// </summary>
public sealed partial class ScriptStaleValueViewModel : ObservableObject
{
    private readonly ScriptSlotCardViewModel card;

    /// <summary>Initializes a new instance of the <see cref="ScriptStaleValueViewModel"/> class.</summary>
    /// <param name="card">The slot card.</param>
    /// <param name="name">The parameter name.</param>
    /// <param name="value">The stored value.</param>
    internal ScriptStaleValueViewModel(ScriptSlotCardViewModel card, string name, ScriptValue value)
    {
        this.card = card;
        this.Name = name;
        this.TypeText = ScriptingComponent.GetTypeName(value.Kind);
        this.ValueText = Describe(card, value);
    }

    /// <summary>Gets the parameter name.</summary>
    public string Name { get; }

    /// <summary>Gets the stored value's type, as written in scene files.</summary>
    public string TypeText { get; }

    /// <summary>Gets the stored value, as shown to the user.</summary>
    public string ValueText { get; }

    /// <summary>Gets a value indicating whether the slot can be edited.</summary>
    public bool CanEdit => this.card.CanEdit;

    /// <summary>Refreshes edit availability after the section's state changed.</summary>
    internal void RefreshCanEdit() => this.OnPropertyChanged(nameof(this.CanEdit));

    private static string Describe(ScriptSlotCardViewModel card, ScriptValue value)
        => value.Kind switch
        {
            ScriptValueKind.BoolValue => value.BoolValue ? "true" : "false",
            ScriptValueKind.Int32Value => value.IntValue.ToString(CultureInfo.CurrentCulture),
            ScriptValueKind.FloatValue => value.FloatValue.ToString("0.###", CultureInfo.CurrentCulture),
            ScriptValueKind.StringValue => '"' + value.StringValue + '"',
            ScriptValueKind.Vec2Value or ScriptValueKind.Vec3Value or ScriptValueKind.Vec4Value => "("
                + string.Join(", ", value.GetVectorComponents().Select(static component => component.ToString("0.###", CultureInfo.CurrentCulture))) + ")",
            ScriptValueKind.AssetValue => value.Asset is { } asset ? Path.GetFileNameWithoutExtension(asset.VirtualPath) : "None",
            ScriptValueKind.NodeValue => card.Owner.DescribeNode(value.NodeId) ?? "Missing node",
            _ => value.ToString(),
        };

    [RelayCommand]
    private Task RemoveAsync() => this.card.RemoveValueAsync(this.Name);
}
