// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Runtime.Engine;
using Windows.UI;

namespace Oxygen.Editor.World.Workspace;

/// <summary>
///     The user's ground grid settings as the Settings flyout edits them. Every edit applies to all viewport panes and
///     is saved for the user, shared by every project.
/// </summary>
public sealed partial class GroundGridPreferences : ObservableObject
{
    private readonly PreviewSettingsService owner;

    /// <summary>Initializes a new instance of the <see cref="GroundGridPreferences"/> class.</summary>
    /// <param name="owner">The service that applies and saves the edits.</param>
    internal GroundGridPreferences(PreviewSettingsService owner)
    {
        this.owner = owner;
    }

    /// <summary>Gets or sets a value indicating whether views draw the grid at all.</summary>
    public bool Enabled
    {
        get => this.Value.Enabled;
        set => this.Request(this.Value with { Enabled = value });
    }

    /// <summary>Gets or sets the distance between adjacent grid lines, in world units.</summary>
    public float Spacing
    {
        get => this.Value.Spacing;
        set => this.Request(this.Value with { Spacing = value });
    }

    /// <summary>Gets or sets how many minor cells lie between major lines.</summary>
    public int MajorEvery
    {
        get => this.Value.MajorEvery;
        set => this.Request(this.Value with { MajorEvery = value });
    }

    /// <summary>Gets or sets the minor line thickness, as a fraction of a cell.</summary>
    public float LineThickness
    {
        get => this.Value.LineThickness;
        set => this.Request(this.Value with { LineThickness = value });
    }

    /// <summary>Gets or sets the major line thickness, as a fraction of a cell.</summary>
    public float MajorThickness
    {
        get => this.Value.MajorThickness;
        set => this.Request(this.Value with { MajorThickness = value });
    }

    /// <summary>Gets or sets the axis line thickness, as a fraction of a cell.</summary>
    public float AxisThickness
    {
        get => this.Value.AxisThickness;
        set => this.Request(this.Value with { AxisThickness = value });
    }

    /// <summary>Gets or sets the distance from the camera at which the grid starts to fade.</summary>
    public float FadeStart
    {
        get => this.Value.FadeStart;
        set => this.Request(this.Value with { FadeStart = value });
    }

    /// <summary>Gets or sets the exponent of the fade with distance.</summary>
    public float FadePower
    {
        get => this.Value.FadePower;
        set => this.Request(this.Value with { FadePower = value });
    }

    /// <summary>Gets or sets how much the lines brighten toward the horizon.</summary>
    public float HorizonBoost
    {
        get => this.Value.HorizonBoost;
        set => this.Request(this.Value with { HorizonBoost = value });
    }

    /// <summary>Gets or sets the X coordinate of the grid origin on the ground plane.</summary>
    public float OriginX
    {
        get => this.Value.OriginX;
        set => this.Request(this.Value with { OriginX = value });
    }

    /// <summary>Gets or sets the Y coordinate of the grid origin on the ground plane.</summary>
    public float OriginY
    {
        get => this.Value.OriginY;
        set => this.Request(this.Value with { OriginY = value });
    }

    /// <summary>Gets or sets a value indicating whether the grid eases after the camera.</summary>
    public bool SmoothMotion
    {
        get => this.Value.SmoothMotion;
        set => this.Request(this.Value with { SmoothMotion = value });
    }

    /// <summary>Gets or sets the easing time of smooth motion, in seconds.</summary>
    public float SmoothTime
    {
        get => this.Value.SmoothTime;
        set => this.Request(this.Value with { SmoothTime = value });
    }

    /// <summary>Gets or sets the minor line color.</summary>
    public Color MinorColor
    {
        get => ToColor(this.Value.MinorColor);
        set => this.Request(this.Value with { MinorColor = FromColor(value, this.Value.MinorColor) });
    }

    /// <summary>Gets or sets the major line color.</summary>
    public Color MajorColor
    {
        get => ToColor(this.Value.MajorColor);
        set => this.Request(this.Value with { MajorColor = FromColor(value, this.Value.MajorColor) });
    }

    /// <summary>Gets or sets the color of the X axis line.</summary>
    public Color AxisColorX
    {
        get => ToColor(this.Value.AxisColorX);
        set => this.Request(this.Value with { AxisColorX = FromColor(value, this.Value.AxisColorX) });
    }

    /// <summary>Gets or sets the color of the Y axis line.</summary>
    public Color AxisColorY
    {
        get => ToColor(this.Value.AxisColorY);
        set => this.Request(this.Value with { AxisColorY = FromColor(value, this.Value.AxisColorY) });
    }

    /// <summary>Gets or sets the color of the origin marker.</summary>
    public Color OriginColor
    {
        get => ToColor(this.Value.OriginColor);
        set => this.Request(this.Value with { OriginColor = FromColor(value, this.Value.OriginColor) });
    }

    /// <summary>Gets the settings in effect.</summary>
    internal GroundGridSettings Value { get; private set; } = new();

    /// <summary>Takes the settings in effect and updates every binding that shows a changed value.</summary>
    /// <param name="value">The applied settings.</param>
    internal void Accept(GroundGridSettings value)
    {
        if (value == this.Value)
        {
            return;
        }

        this.Value = value;
        this.Refresh();
    }

    /// <summary>Updates every binding, returning edits that were clamped or rejected to the value in effect.</summary>
    internal void Refresh() => this.OnPropertyChanged(string.Empty);

    /// <summary>A color edit keeps the stored channels when the picker shows the same 8-bit color.</summary>
    private static RuntimeColor FromColor(Color color, RuntimeColor current)
        => color == ToColor(current) ? current : new(color.R / 255f, color.G / 255f, color.B / 255f, color.A / 255f);

    private static Color ToColor(RuntimeColor color)
        => Color.FromArgb(ToByte(color.A), ToByte(color.R), ToByte(color.G), ToByte(color.B));

    private static byte ToByte(float channel) => (byte)Math.Round(Math.Clamp(channel, 0f, 1f) * 255f);

    /// <summary>Restores the engine's default grid.</summary>
    [RelayCommand]
    private void ResetToDefaults() => this.Request(new GroundGridSettings());

    private void Request(GroundGridSettings requested) => this.owner.ApplyGroundGrid(requested);
}
