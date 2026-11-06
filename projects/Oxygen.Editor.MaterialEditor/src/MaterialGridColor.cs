// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>One of the procedural grid's colors.</summary>
/// <param name="label">The row's label.</param>
/// <param name="properties">The color's channels.</param>
/// <param name="fallback">The engine's default.</param>
/// <param name="edit">Applies an edit of the color.</param>
public sealed partial class MaterialGridColor(string label, MaterialColorProperties properties, Vector4 fallback, Action<PropertyEdit> edit) : ObservableObject
{
    private bool isRefreshing;

    /// <summary>Gets the row's label.</summary>
    public string Label { get; } = label;

    /// <summary>Gets the field's name, which labels a gesture's history entry.</summary>
    public string Field { get; } = $"grid {label} color";

    /// <summary>Gets the name of the row's picker for assistive technology.</summary>
    public string PickerName { get; } = $"Pick grid {label} color";

    /// <summary>Gets the row's mark.</summary>
    public OverrideState Mark => (float)this.Red != fallback.X || (float)this.Green != fallback.Y || (float)this.Blue != fallback.Z || (float)this.Alpha != fallback.W
        ? OverrideState.Full
        : OverrideState.None;

    /// <summary>Gets or sets the red channel.</summary>
    [ObservableProperty]
    public partial double Red { get; set; }

    /// <summary>Gets or sets the green channel.</summary>
    [ObservableProperty]
    public partial double Green { get; set; }

    /// <summary>Gets or sets the blue channel.</summary>
    [ObservableProperty]
    public partial double Blue { get; set; }

    /// <summary>Gets or sets the alpha channel.</summary>
    [ObservableProperty]
    public partial double Alpha { get; set; }

    /// <summary>Applies a picked color as one edit.</summary>
    /// <param name="color">The linear channels.</param>
    public void Set(ColorChannels color)
    {
        this.Show(new Vector4((float)color.Red, (float)color.Green, (float)color.Blue, (float)color.Alpha));
        this.OnPropertyChanged(nameof(this.Mark));
        edit(new PropertyEdit.Builder()
            .Set(properties.Channels[0], (float)color.Red)
            .Set(properties.Channels[1], (float)color.Green)
            .Set(properties.Channels[2], (float)color.Blue)
            .Set(properties.Channels[3], (float)color.Alpha)
            .Build());
    }

    /// <summary>Shows the source's color.</summary>
    /// <param name="color">The color, or <see langword="null"/> when the source takes the engine's default.</param>
    internal void Refresh(Vector4? color)
    {
        this.Show(color ?? fallback);
        this.OnPropertyChanged(nameof(this.Mark));
    }

    private void Show(Vector4 color)
    {
        this.isRefreshing = true;
        try
        {
            this.Red = color.X;
            this.Green = color.Y;
            this.Blue = color.Z;
            this.Alpha = color.W;
        }
        finally
        {
            this.isRefreshing = false;
        }
    }

    private void Edited(int channel, double value)
    {
        if (!this.isRefreshing)
        {
            this.OnPropertyChanged(nameof(this.Mark));
            edit(PropertyEdit.SingleEdit(properties.Channels[channel], (float)value));
        }
    }

    partial void OnRedChanged(double value) => this.Edited(0, value);

    partial void OnGreenChanged(double value) => this.Edited(1, value);

    partial void OnBlueChanged(double value) => this.Edited(2, value);

    partial void OnAlphaChanged(double value) => this.Edited(3, value);

    [RelayCommand]
    private void Reset() => this.Set(new ColorChannels(fallback.X, fallback.Y, fallback.Z, fallback.W));
}
