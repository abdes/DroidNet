// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Materials.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Managed.Assets.Authoring.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>The material's procedural grid section: whether the material has a grid, and its values.</summary>
/// <param name="edit">Applies an edit of the grid's values.</param>
/// <param name="toggled">Gives the material a grid or removes the one it has.</param>
public sealed partial class MaterialGridSection(Action<PropertyEdit> edit, Action<bool> toggled) : ObservableObject
{
    private static readonly MaterialProceduralGrid Defaults = MaterialProceduralGrid.EngineDefaults;
    private bool isRefreshing;

    /// <summary>Gets the grid's colors.</summary>
    public IReadOnlyList<MaterialGridColor> Colors { get; } =
    [
        new("Minor", MaterialGridProperties.MinorColor, Defaults.MinorColor!.Value, edit),
        new("Major", MaterialGridProperties.MajorColor, Defaults.MajorColor!.Value, edit),
        new("X axis", MaterialGridProperties.AxisColorX, Defaults.AxisColorX!.Value, edit),
        new("Y axis", MaterialGridProperties.AxisColorY, Defaults.AxisColorY!.Value, edit),
        new("Origin", MaterialGridProperties.OriginColor, Defaults.OriginColor!.Value, edit),
    ];

    /// <summary>Gets whether the grid's values show: a material without a grid has none.</summary>
    public Visibility ValuesVisibility => this.IsShown ? Visibility.Visible : Visibility.Collapsed;

    /// <summary>Gets the spacing row's mark.</summary>
    public OverrideState SpacingMark => Mark((float)this.SpacingX != Defaults.Spacing!.Value.X || (float)this.SpacingY != Defaults.Spacing!.Value.Y);

    /// <summary>Gets the major interval row's mark.</summary>
    public OverrideState MajorEveryMark => Mark(this.MajorEvery != Defaults.MajorEvery!.Value);

    /// <summary>Gets the thickness row's mark.</summary>
    public OverrideState ThicknessMark => Mark((float)this.LineThickness != Defaults.LineThickness!.Value
        || (float)this.MajorThickness != Defaults.MajorThickness!.Value
        || (float)this.AxisThickness != Defaults.AxisThickness!.Value);

    /// <summary>Gets the fade row's mark.</summary>
    public OverrideState FadeMark => Mark((float)this.FadeStart != Defaults.FadeStart!.Value || (float)this.FadeEnd != Defaults.FadeEnd!.Value);

    /// <summary>Gets or sets a value indicating whether the material has a grid.</summary>
    [ObservableProperty]
    [NotifyPropertyChangedFor(nameof(ValuesVisibility))]
    public partial bool IsShown { get; set; }

    /// <summary>Gets or sets the spacing along the first axis of a plane.</summary>
    [ObservableProperty]
    public partial double SpacingX { get; set; } = 1.0;

    /// <summary>Gets or sets the spacing along the second axis of a plane.</summary>
    [ObservableProperty]
    public partial double SpacingY { get; set; } = 1.0;

    /// <summary>Gets or sets the minor cells per major line.</summary>
    [ObservableProperty]
    public partial double MajorEvery { get; set; } = 10.0;

    /// <summary>Gets or sets the width of minor lines.</summary>
    [ObservableProperty]
    public partial double LineThickness { get; set; }

    /// <summary>Gets or sets the width of major lines.</summary>
    [ObservableProperty]
    public partial double MajorThickness { get; set; }

    /// <summary>Gets or sets the width of the axis lines.</summary>
    [ObservableProperty]
    public partial double AxisThickness { get; set; }

    /// <summary>Gets or sets the view distance at which the grid starts to fade.</summary>
    [ObservableProperty]
    public partial double FadeStart { get; set; }

    /// <summary>Gets or sets the view distance at which the grid is gone.</summary>
    [ObservableProperty]
    public partial double FadeEnd { get; set; }

    /// <summary>Shows the source's grid.</summary>
    /// <param name="grid">The grid, or <see langword="null"/> when the material has none.</param>
    public void Refresh(MaterialProceduralGrid? grid)
    {
        var shown = grid ?? Defaults;
        this.isRefreshing = true;
        try
        {
            this.IsShown = grid is not null;
            var spacing = shown.Spacing ?? Defaults.Spacing!.Value;
            this.SpacingX = spacing.X;
            this.SpacingY = spacing.Y;
            this.MajorEvery = shown.MajorEvery ?? Defaults.MajorEvery!.Value;
            this.LineThickness = shown.LineThickness ?? Defaults.LineThickness!.Value;
            this.MajorThickness = shown.MajorThickness ?? Defaults.MajorThickness!.Value;
            this.AxisThickness = shown.AxisThickness ?? Defaults.AxisThickness!.Value;
            this.FadeStart = shown.FadeStart ?? Defaults.FadeStart!.Value;
            this.FadeEnd = shown.FadeEnd ?? Defaults.FadeEnd!.Value;
            this.Colors[0].Refresh(shown.MinorColor);
            this.Colors[1].Refresh(shown.MajorColor);
            this.Colors[2].Refresh(shown.AxisColorX);
            this.Colors[3].Refresh(shown.AxisColorY);
            this.Colors[4].Refresh(shown.OriginColor);
        }
        finally
        {
            this.isRefreshing = false;
        }

        this.OnMarksChanged();
    }

    private static OverrideState Mark(bool differs) => differs ? OverrideState.Full : OverrideState.None;

    private void OnMarksChanged()
    {
        this.OnPropertyChanged(nameof(this.SpacingMark));
        this.OnPropertyChanged(nameof(this.MajorEveryMark));
        this.OnPropertyChanged(nameof(this.ThicknessMark));
        this.OnPropertyChanged(nameof(this.FadeMark));
    }

    private void Edited(PropertyEdit change)
    {
        if (!this.isRefreshing)
        {
            this.OnMarksChanged();
            edit(change);
        }
    }

    partial void OnIsShownChanged(bool value)
    {
        if (!this.isRefreshing)
        {
            toggled(value);
        }
    }

    partial void OnSpacingXChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.SpacingX, (float)value));

    partial void OnSpacingYChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.SpacingY, (float)value));

    // A value below one is no interval: it stays below one, for the edit to be refused.
    partial void OnMajorEveryChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.MajorEvery, value < 1.0 ? 0U : (uint)Math.Round(value)));

    partial void OnLineThicknessChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.LineThickness, (float)value));

    partial void OnMajorThicknessChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.MajorThickness, (float)value));

    partial void OnAxisThicknessChanged(double value) => this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.AxisThickness, (float)value));

    // A fade cannot end before it starts, so moving one end past the other carries it along.
    partial void OnFadeStartChanged(double value)
    {
        if (!this.isRefreshing && this.FadeEnd < value)
        {
            this.SetTogether(() => this.FadeEnd = value);
            this.Edited(new PropertyEdit.Builder().Set(MaterialGridProperties.FadeStart, (float)value).Set(MaterialGridProperties.FadeEnd, (float)value).Build());
            return;
        }

        this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.FadeStart, (float)value));
    }

    partial void OnFadeEndChanged(double value)
    {
        if (!this.isRefreshing && this.FadeStart > value)
        {
            this.SetTogether(() => this.FadeStart = value);
            this.Edited(new PropertyEdit.Builder().Set(MaterialGridProperties.FadeStart, (float)value).Set(MaterialGridProperties.FadeEnd, (float)value).Build());
            return;
        }

        this.Edited(PropertyEdit.SingleEdit(MaterialGridProperties.FadeEnd, (float)value));
    }

    [RelayCommand]
    private void ResetSpacing()
    {
        var spacing = Defaults.Spacing!.Value;
        this.SetTogether(() => (this.SpacingX, this.SpacingY) = (spacing.X, spacing.Y));
        this.Edited(new PropertyEdit.Builder().Set(MaterialGridProperties.SpacingX, spacing.X).Set(MaterialGridProperties.SpacingY, spacing.Y).Build());
    }

    [RelayCommand]
    private void ResetMajorEvery() => this.MajorEvery = Defaults.MajorEvery!.Value;

    [RelayCommand]
    private void ResetThickness()
    {
        var (line, major, axis) = (Defaults.LineThickness!.Value, Defaults.MajorThickness!.Value, Defaults.AxisThickness!.Value);
        this.SetTogether(() => (this.LineThickness, this.MajorThickness, this.AxisThickness) = (line, major, axis));
        this.Edited(new PropertyEdit.Builder()
            .Set(MaterialGridProperties.LineThickness, line)
            .Set(MaterialGridProperties.MajorThickness, major)
            .Set(MaterialGridProperties.AxisThickness, axis)
            .Build());
    }

    [RelayCommand]
    private void ResetFade()
    {
        var (start, end) = (Defaults.FadeStart!.Value, Defaults.FadeEnd!.Value);
        this.SetTogether(() => (this.FadeStart, this.FadeEnd) = (start, end));
        this.Edited(new PropertyEdit.Builder().Set(MaterialGridProperties.FadeStart, start).Set(MaterialGridProperties.FadeEnd, end).Build());
    }

    // Fields set together are one edit, so one history entry.
    private void SetTogether(Action set)
    {
        this.isRefreshing = true;
        try
        {
            set();
        }
        finally
        {
            this.isRefreshing = false;
        }
    }
}
