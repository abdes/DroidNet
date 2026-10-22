// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>Projects current native surfaces and captures picker targets across asynchronous catalog work.</summary>
public sealed partial class GeometryViewModel
{
    private CancellationTokenSource? surfaceRefresh;
    private MaterialPickerEdit? materialEdit;

    /// <summary>Gets the existing surfaces of the selected geometry.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<SurfaceChoice> Surfaces { get; set; } = [];

    /// <summary>Gets or sets the surface whose assignment is shown.</summary>
    [ObservableProperty]
    public partial SurfaceChoice? SelectedSurface { get; set; }

    /// <summary>Gets a concise inventory or unresolved-assignment status.</summary>
    [ObservableProperty]
    public partial string SurfaceNotice { get; set; } = string.Empty;

    /// <summary>Gets whether the selection has overrides that no longer resolve against its geometry.</summary>
    [ObservableProperty]
    public partial bool HasUnresolvedSurfaces { get; set; }

    /// <summary>Gets whether current native inventory is being read.</summary>
    [ObservableProperty]
    public partial bool IsSurfaceLoading { get; set; }

    /// <summary>Gets whether the geometry needs an explicit surface selector.</summary>
    public bool ShowSurfaceSelector => this.Surfaces.Count > 1;

    /// <summary>Gets whether an existing native surface can be targeted.</summary>
    public bool CanEditSurfaceMaterial => this.SelectedSurface is not null && !this.IsSurfaceLoading;

    /// <summary>Gets whether surface inventory failed to load for the selected geometry.</summary>
    [ObservableProperty]
    public partial bool IsSurfaceInventoryFailed { get; set; }

    /// <summary>Gets whether the material value should be shown in its error state.</summary>
    public bool HasSurfaceError => this.HasUnresolvedSurfaces || this.IsSurfaceInventoryFailed;

    /// <summary>Gets the material value tooltip: the assignment, followed by any surface status and its remedy.</summary>
    public string MaterialToolTip => this.SurfaceNotice.Length == 0
        ? this.MaterialButtonLabel
        : this.MaterialButtonLabel + System.Environment.NewLine + this.SurfaceNotice;

    /// <summary>Resets the selection's overrides of surfaces their geometry does not have, as one undoable edit.</summary>
    /// <returns>Completion after the edit and a fresh surface projection.</returns>
    [RelayCommand]
    public async Task ClearUnresolvedSurfacesAsync()
    {
        if (this.disposed || !this.IsInputEnabled
            || this.selectedItems is not { Count: > 0 } selected || this.authoringProvider?.Invoke() is not { } authoring)
        {
            return;
        }

        var project = this.session.Context;
        var nodes = selected.ToList();
        var built = await SurfaceEdits.ClearUnresolvedAsync(this.surfaces, project, authoring.Scene, nodes.ConvertAll(static node => node.Id)).ConfigureAwait(true);
        if (built.Edit is { } edit)
        {
            _ = await authoring.ExecuteAsync(edit).ConfigureAwait(true);
        }
        if (this.IsCurrentSelection(nodes))
        {
            await this.RefreshSurfacesAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Refreshes native inventory without changing authored assignments.</summary>
    /// <returns>Completion after current metadata or its unavailable state is presented.</returns>
    [RelayCommand]
    public async Task RefreshSurfacesAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.surfaceRefresh?.Cancel();
        var request = new CancellationTokenSource();
        this.surfaceRefresh = request;
        var nodes = this.selectedItems?.ToList() ?? [];
        var geometryUri = nodes.FirstOrDefault()?.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry;
        var project = this.session.Context;
        this.IsSurfaceLoading = true;
        try
        {
            if (nodes.Count == 0 || geometryUri is null)
            {
                this.SetSurfaceMaterialsUnavailable("Select geometry to view its surfaces.");
                return;
            }

            // The inspected scene is a document of the active project's workspace; a node of another scene is stale.
            if (this.authoringProvider?.Invoke() is not { } authoring || nodes.Exists(node => !ReferenceEquals(node.Scene, authoring.Scene)))
            {
                this.SetSurfaceMaterialsUnavailable("The selection is no longer in the inspected scene.");
                return;
            }

            if (nodes.Any(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry != geometryUri))
            {
                this.SetSurfaceMaterialsUnavailable("Select instances of the same geometry to edit surface materials together.");
                return;
            }

            var inventory = await this.surfaces.ReadAsync(project, geometryUri, request.Token).ConfigureAwait(true);
            if (request.IsCancellationRequested || !this.IsCurrentSelection(nodes)
                || !ReferenceEquals(project, this.session.Context)
                || nodes.Exists(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry != geometryUri))
            {
                return;
            }

            if (inventory is null)
            {
                this.SetSurfaceMaterialsFailed("Surfaces unavailable. Cook this geometry, then refresh its surfaces.");
                return;
            }

            var selected = this.SelectedSurface?.Target;
            var labels = inventory.Surfaces.GroupBy(static surface => surface.DisplayName, StringComparer.Ordinal)
                .ToDictionary(static group => group.Key, static group => group.Count(), StringComparer.Ordinal);
            this.Surfaces = [.. inventory.Surfaces.Select((surface, index) => new SurfaceChoice(
                new(geometryUri, surface.SurfaceId, inventory.LayoutRevision),
                string.IsNullOrWhiteSpace(surface.DisplayName) ? $"Surface {index + 1}" : labels[surface.DisplayName] > 1 ? $"{surface.DisplayName} ({index + 1})" : surface.DisplayName))];
            this.SelectedSurface = this.Surfaces.FirstOrDefault(surface => selected is not null && surface.Target.Geometry == selected.Geometry && surface.Target.SurfaceId == selected.SurfaceId)
                ?? this.Surfaces.FirstOrDefault();
            var surfaceIds = inventory.Surfaces.Select(static surface => surface.SurfaceId).ToHashSet();
            var unresolved = nodes.SelectMany(static node => node.Components.OfType<GeometryComponent>().SelectMany(static geometry => geometry.Overrides.Parts.Keys))
                .Count(part => !surfaceIds.Contains(part.Id));
            this.HasUnresolvedSurfaces = unresolved != 0;
            this.IsSurfaceInventoryFailed = false;
            this.SurfaceNotice = unresolved == 1
                ? "One surface override targets a surface this geometry does not have. Clear unresolved (✕) removes it; Undo restores it."
                : unresolved > 1 ? $"{unresolved} surface overrides target surfaces this geometry does not have. Clear unresolved (✕) removes them; Undo restores them."
                : this.Surfaces.Count == 0 ? "This geometry has no surfaces." : string.Empty;
            this.UpdateSelectedSurfaceDisplay();
        }
        catch (OperationCanceledException) when (request.IsCancellationRequested)
        {
        }
        catch (OperationCanceledException)
        {
            if (!this.disposed && ReferenceEquals(this.surfaceRefresh, request))
            {
                this.SetSurfaceMaterialsFailed("Surfaces unavailable. Refresh them after the project is ready.");
            }
        }
        catch (Exception error) when (error is IOException or InvalidDataException or FormatException or InvalidOperationException)
        {
            if (!this.disposed && ReferenceEquals(this.surfaceRefresh, request))
            {
                this.SetSurfaceMaterialsFailed("Surfaces unavailable. " + error.Message);
            }
        }
        finally
        {
            if (ReferenceEquals(this.surfaceRefresh, request))
            {
                this.surfaceRefresh = null;
                this.IsSurfaceLoading = false;
            }

            request.Dispose();
        }
    }

    private void SetSurfaceMaterialsFailed(string message)
    {
        this.SetSurfaceMaterialsUnavailable(message);
        this.IsSurfaceInventoryFailed = true;
    }

    private void SetSurfaceMaterialsUnavailable(string message)
    {
        this.HasUnresolvedSurfaces = false;
        this.IsSurfaceInventoryFailed = false;
        this.Surfaces = [];
        this.SelectedSurface = null;
        this.SurfaceNotice = message;
        this.UpdateSelectedSurfaceDisplay();
    }

    private void UpdateSelectedSurfaceDisplay()
    {
        if (this.SelectedSurface is not { } selected)
        {
            this.IsMaterialMixed = false;
            this.SelectedMaterial = null;
            this.SelectedMaterialName = "Unavailable";
            return;
        }

        var materials = (this.selectedItems ?? []).Select(node => node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } geometry
                && geometry.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, selected.Target.Part, out var material)
                ? material
                : null)
            .Distinct().ToArray();
        this.IsMaterialMixed = materials.Length > 1;
        this.SelectedMaterial = this.IsMaterialMixed ? null : materials.FirstOrDefault();
        this.SelectedMaterialName = this.IsMaterialMixed ? "--"
            : materials.FirstOrDefault() is { } material ? this.ResolveMaterialDisplayName(material) : "Geometry default";
    }

    partial void OnSelectedSurfaceChanged(SurfaceChoice? value)
    {
        if (value?.Target != this.materialEdit?.Target)
        {
            this.materialEdit = null;
        }

        this.UpdateSelectedSurfaceDisplay();
        this.OnPropertyChanged(nameof(this.CanEditSurfaceMaterial));
    }

    partial void OnSurfacesChanged(IReadOnlyList<SurfaceChoice> value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.ShowSurfaceSelector));
    }

    partial void OnSurfaceNoticeChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialToolTip));
    }

    partial void OnHasUnresolvedSurfacesChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasSurfaceError));
    }

    partial void OnIsSurfaceInventoryFailedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasSurfaceError));
    }

    partial void OnIsSurfaceLoadingChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.CanEditSurfaceMaterial));
    }

    private sealed record MaterialPickerEdit(SceneAuthoring Authoring, List<SceneNode> Nodes, SurfaceTarget Target);
}
