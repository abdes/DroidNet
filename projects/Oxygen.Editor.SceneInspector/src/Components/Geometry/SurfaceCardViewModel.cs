// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
/// One surface of the selected geometry, as a card of the Surfaces list: its Material, Visibility and Cast Shadows, each
/// the selected nodes' override or the asset's value.
/// </summary>
/// <remarks>
/// The card only shows values and forwards edits; <see cref="GeometryViewModel"/> builds them, and updates the card from
/// the scene and the inventory after each one. A value the nodes disagree on is <see langword="null"/>.
/// </remarks>
public sealed partial class SurfaceCardViewModel : ObservableObject
{
    private const string GeometryMaterialText = "Geometry default";

    private readonly GeometryViewModel owner;
    private bool? visible;
    private bool? castsShadows;

    /// <summary>Initializes a new instance of the <see cref="SurfaceCardViewModel"/> class.</summary>
    /// <param name="owner">The geometry section that edits the surface.</param>
    /// <param name="target">The surface, with the inventory revision it is shown from.</param>
    internal SurfaceCardViewModel(GeometryViewModel owner, SurfaceTarget target)
    {
        this.owner = owner;
        this.Target = target;
    }

    /// <summary>Gets the surface, with the geometry and inventory revision it is shown from.</summary>
    public SurfaceTarget Target { get; private set; }

    /// <summary>Gets the surface's name, unique in the list.</summary>
    [ObservableProperty]
    public partial string Name { get; private set; } = string.Empty;

    /// <summary>Gets the collapsed card's line: the material, then Hidden or No shadows when that value is Off.</summary>
    [ObservableProperty]
    public partial string Summary { get; private set; } = string.Empty;

    /// <summary>Gets or sets a value indicating whether the card shows its rows.</summary>
    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    /// <summary>Gets a value indicating whether the surface can be edited now.</summary>
    [ObservableProperty]
    public partial bool CanEdit { get; private set; }

    /// <summary>Gets how many selected nodes override any value of the surface.</summary>
    [ObservableProperty]
    public partial OverrideState OverrideState { get; private set; }

    /// <summary>Gets the card reset's tooltip, which lists the values it resets.</summary>
    [ObservableProperty]
    public partial string ResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets the material button's text.</summary>
    [ObservableProperty]
    public partial string MaterialLabel { get; private set; } = GeometryMaterialText;

    /// <summary>Gets the material thumbnail's glyph: mixed, or a material.</summary>
    [ObservableProperty]
    public partial string MaterialGlyph { get; private set; } = string.Empty;

    /// <summary>Gets how many selected nodes override the material.</summary>
    [ObservableProperty]
    public partial OverrideState MaterialOverrideState { get; private set; }

    /// <summary>Gets the material reset's tooltip.</summary>
    [ObservableProperty]
    public partial string MaterialResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets how many selected nodes override Visibility.</summary>
    [ObservableProperty]
    public partial OverrideState VisibleOverrideState { get; private set; }

    /// <summary>Gets the Visibility reset's tooltip.</summary>
    [ObservableProperty]
    public partial string VisibleResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets how many selected nodes override Cast Shadows.</summary>
    [ObservableProperty]
    public partial OverrideState CastsShadowsOverrideState { get; private set; }

    /// <summary>Gets the Cast Shadows reset's tooltip.</summary>
    [ObservableProperty]
    public partial string CastsShadowsResetToolTip { get; private set; } = string.Empty;

    /// <summary>Gets the materials the picker offers.</summary>
    public AssetPickerModel MaterialChoices => this.owner.MaterialChoices;

    /// <summary>Gets or sets the surface's Visibility on the selected nodes; setting it sets it on every one.</summary>
    public bool? Visible
    {
        get => this.visible;
        set
        {
            if (value is { } shown && value != this.visible)
            {
                _ = this.owner.SetSurfaceFlagAsync(this, SceneSchema.Geometry.Surface.Visibility.Id, shown, "Edit Surface Visibility");
            }
        }
    }

    /// <summary>Gets or sets the surface's Cast Shadows on the selected nodes; setting it sets it on every one.</summary>
    public bool? CastsShadows
    {
        get => this.castsShadows;
        set
        {
            if (value is { } casts && value != this.castsShadows)
            {
                _ = this.owner.SetSurfaceFlagAsync(this, SceneSchema.Geometry.Surface.CastsShadows.Id, casts, "Edit Surface Cast Shadows");
            }
        }
    }

    /// <summary>Applies a picked material to the surface on every selected node.</summary>
    /// <param name="choice">The picked material.</param>
    /// <returns>Completion after the edit and the card's update.</returns>
    public Task ApplyMaterialAsync(AssetChoice choice) => this.owner.ApplyMaterialAsync(this, choice);

    /// <summary>Refreshes the material picker for this surface before it opens.</summary>
    /// <returns>Completion after the picker lists the current materials.</returns>
    public Task PrepareMaterialPickerAsync() => this.owner.PrepareMaterialPickerAsync(this);

    /// <summary>Shows the surface's values on the selected nodes.</summary>
    /// <param name="target">The surface, from the current inventory.</param>
    /// <param name="surface">The surface's inventory entry, with its asset defaults.</param>
    /// <param name="name">The surface's name in the list.</param>
    /// <param name="nodes">The values and changes of each selected node's geometry.</param>
    /// <param name="materialName">Names a material.</param>
    /// <param name="canEdit">Whether the surface can be edited now.</param>
    internal void Update(SurfaceTarget target, GeometrySurface surface, string name, IReadOnlyList<GeometryOverrides> nodes, Func<AssetPath, string> materialName, bool canEdit)
    {
        this.Target = target;
        this.Name = name;
        this.CanEdit = canEdit;
        var part = target.Part;

        var materials = nodes.Select(overrides => overrides.Values.TryGet(SceneSchema.Geometry.Surface.Material, part, out var material) ? material : null).Distinct().ToList();
        var material = materials.Count == 1 ? materials[0] : null;
        this.MaterialLabel = materials.Count > 1 ? "--" : material is null ? GeometryMaterialText : materialName(material);
        this.MaterialGlyph = materials.Count > 1 ? "" : "";
        (this.MaterialOverrideState, var materialOverrides) = StateOf(nodes, SceneSchema.Geometry.Surface.Material.Id, part);
        this.MaterialResetToolTip = ResetText("Reset to the geometry's material", materialOverrides, nodes.Count);

        var shown = nodes.Select(overrides => overrides.Values.TryGet(SceneSchema.Geometry.Surface.Visibility, part, out var value) ? value : surface.Visible).ToList();
        var casting = nodes.Select(overrides => overrides.Values.TryGet(SceneSchema.Geometry.Surface.CastsShadows, part, out var value) ? value : surface.CastsShadows).ToList();
        this.visible = Agreed(shown);
        this.castsShadows = Agreed(casting);
        (this.VisibleOverrideState, var visibleOverrides) = StateOf(nodes, SceneSchema.Geometry.Surface.Visibility.Id, part);
        (this.CastsShadowsOverrideState, var castsOverrides) = StateOf(nodes, SceneSchema.Geometry.Surface.CastsShadows.Id, part);
        this.VisibleResetToolTip = ResetText($"Reset to the asset value ({OnOff(surface.Visible)})", visibleOverrides, nodes.Count);
        this.CastsShadowsResetToolTip = ResetText($"Reset to the asset value ({OnOff(surface.CastsShadows)})", castsOverrides, nodes.Count);

        // The switches show the scene's values even when an edit was refused after they moved.
        this.OnPropertyChanged(nameof(this.Visible));
        this.OnPropertyChanged(nameof(this.CastsShadows));

        this.Summary = string.Join(" · ", new[] { this.MaterialLabel, OffText("Hidden", shown), OffText("No shadows", casting) }.Where(static text => text.Length > 0));

        var overridden = SurfaceLabels.Rows.Where(row => nodes.Any(overrides => overrides.Changes.Find(row.Property, part) is not null)).Select(static row => row.Label).ToList();
        var anyOverride = nodes.Count(overrides => overrides.Changes.Parts.ContainsKey(part));
        this.OverrideState = anyOverride == 0 ? OverrideState.None : anyOverride == nodes.Count ? OverrideState.Full : OverrideState.Partial;
        this.ResetToolTip = overridden.Count == 0 ? string.Empty : ResetText($"Reset {Join(overridden)} to the asset's values", anyOverride, nodes.Count);
    }

    [RelayCommand]
    private Task ResetAsync() => this.owner.ResetSurfaceAsync(this);

    [RelayCommand]
    private Task ResetMaterialAsync() => this.owner.ResetSurfaceValueAsync(this, SceneSchema.Geometry.Surface.Material.Id, "Reset Surface Material");

    [RelayCommand]
    private Task ResetVisibleAsync() => this.owner.ResetSurfaceValueAsync(this, SceneSchema.Geometry.Surface.Visibility.Id, "Reset Surface Visibility");

    [RelayCommand]
    private Task ResetCastsShadowsAsync() => this.owner.ResetSurfaceValueAsync(this, SceneSchema.Geometry.Surface.CastsShadows.Id, "Reset Surface Cast Shadows");

    private static (OverrideState State, int Count) StateOf(IReadOnlyList<GeometryOverrides> nodes, PropertyId property, PartId part)
    {
        var count = nodes.Count(overrides => overrides.Changes.Find(property, part) is not null);
        return (count == 0 ? OverrideState.None : count == nodes.Count ? OverrideState.Full : OverrideState.Partial, count);
    }

    private static bool? Agreed(List<bool> values) => values.Count > 0 && values.TrueForAll(value => value == values[0]) ? values[0] : null;

    private static string OffText(string text, List<bool> values)
    {
        var off = values.Count(static value => !value);
        return off == 0 ? string.Empty : off == values.Count ? text : string.Create(CultureInfo.CurrentCulture, $"{text} on {off}");
    }

    private static string ResetText(string text, int overriding, int count)
        => overriding == count ? text : string.Create(CultureInfo.CurrentCulture, $"{text}, on the {overriding} of {count} nodes that change it");

    private static string OnOff(bool value) => value ? "On" : "Off";

    private static string Join(List<string> names)
        => names.Count == 1 ? names[0] : string.Join(", ", names.Take(names.Count - 1)) + " and " + names[^1];
}
