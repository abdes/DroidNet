// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Controls.PropertyEditors;
using Oxygen.Editor.Models;

namespace Oxygen.Editor.ModelEditor.Settings;

/// <summary>One surface of the selected mesh: whether it renders and casts shadows by default, each set or as the last
/// import gave it.</summary>
public sealed partial class SurfaceDefaultsRow : ObservableObject
{
    private readonly SelectedMeshGroup group;
    private bool isRefreshing;

    /// <summary>Initializes a new instance of the <see cref="SurfaceDefaultsRow"/> class.</summary>
    /// <param name="group">The Selected mesh group.</param>
    /// <param name="surface">The surface as the last import produced it.</param>
    internal SurfaceDefaultsRow(SelectedMeshGroup group, GeometrySurface surface)
    {
        this.group = group;
        this.Surface = surface;
    }

    /// <summary>Gets the surface as the last import produced it.</summary>
    public GeometrySurface Surface { get; }

    /// <summary>Gets the surface's name, as the geometry names it.</summary>
    public string Name => this.Surface.DisplayName;

    /// <summary>Gets the tooltip of the surface's name, which says what the name is.</summary>
    public string NameToolTip => $"The surface {this.Name} of the selected mesh";

    /// <summary>Gets or sets whether the surface renders by default.</summary>
    [ObservableProperty]
    public partial bool? Visible { get; set; }

    /// <summary>Gets or sets whether the surface casts shadows by default.</summary>
    [ObservableProperty]
    public partial bool? CastsShadows { get; set; }

    /// <summary>Gets whether the settings set the visibility.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ResetVisibleCommand))]
    public partial OverrideState VisibleOverride { get; private set; }

    /// <summary>Gets whether the settings set the shadow casting.</summary>
    [ObservableProperty]
    [NotifyCanExecuteChangedFor(nameof(ResetCastsShadowsCommand))]
    public partial OverrideState CastsShadowsOverride { get; private set; }

    /// <summary>Gets the tooltip of the rows' reset buttons.</summary>
    public string ResetToolTip => "Reset to what the import gives";

    /// <summary>Shows the defaults the settings hold, else the last import's.</summary>
    /// <param name="settings">The model's import settings.</param>
    internal void Refresh(JsonElement settings)
    {
        var anchor = this.group.Anchor;
        var visible = ModelGeometryDefaults.SurfaceFlagOf(settings, anchor, this.Surface.SurfaceId, ModelSurfaceFlag.Visible);
        var shadows = ModelGeometryDefaults.SurfaceFlagOf(settings, anchor, this.Surface.SurfaceId, ModelSurfaceFlag.CastsShadows);
        this.isRefreshing = true;
        try
        {
            this.Visible = visible ?? this.Surface.Visible;
            this.CastsShadows = shadows ?? this.Surface.CastsShadows;
        }
        finally
        {
            this.isRefreshing = false;
        }

        this.VisibleOverride = visible is null ? OverrideState.None : OverrideState.Full;
        this.CastsShadowsOverride = shadows is null ? OverrideState.None : OverrideState.Full;
    }

    partial void OnVisibleChanged(bool? value)
    {
        if (!this.isRefreshing && value is { } state)
        {
            this.group.WriteSurface(this.Surface, ModelSurfaceFlag.Visible, state);
        }
    }

    partial void OnCastsShadowsChanged(bool? value)
    {
        if (!this.isRefreshing && value is { } state)
        {
            this.group.WriteSurface(this.Surface, ModelSurfaceFlag.CastsShadows, state);
        }
    }

    private bool CanResetVisible() => this.VisibleOverride != OverrideState.None;

    [RelayCommand(CanExecute = nameof(CanResetVisible))]
    private void ResetVisible() => this.group.WriteSurface(this.Surface, ModelSurfaceFlag.Visible, value: null);

    private bool CanResetCastsShadows() => this.CastsShadowsOverride != OverrideState.None;

    [RelayCommand(CanExecute = nameof(CanResetCastsShadows))]
    private void ResetCastsShadows() => this.group.WriteSurface(this.Surface, ModelSurfaceFlag.CastsShadows, value: null);
}
