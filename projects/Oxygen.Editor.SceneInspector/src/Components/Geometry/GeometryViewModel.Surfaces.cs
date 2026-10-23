// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Geometry;

/// <summary>
/// The Surfaces list: one card per surface of the selected geometry's current inventory, and the changes the selection
/// keeps for surfaces the geometry no longer has.
/// </summary>
/// <remarks>
/// The list shows only when every selected node uses one geometry, since surface IDs belong to it. Cards and kept changes
/// are updated in place from the scene after every edit, undo and redo; the inventory is read again only when the
/// selection, its geometry or the geometry's cook changes, or when the author refreshes it.
/// </remarks>
public sealed partial class GeometryViewModel
{
    private CancellationTokenSource? surfaceRefresh;
    private GeometrySurfaceMetadata? inventory;
    private ProjectContext? inventoryProject;
    private MaterialPickerEdit? materialEdit;
    private AssetPath? pickedMaterial;

    /// <summary>Gets the cards of the selected geometry's surfaces.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<SurfaceCardViewModel> Surfaces { get; private set; } = [];

    /// <summary>Gets the changes the selection keeps for surfaces its geometry no longer has.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<KeptSurfaceChangesViewModel> KeptChanges { get; private set; } = [];

    /// <summary>Gets why the list is empty or unavailable, or nothing.</summary>
    [ObservableProperty]
    public partial string SurfaceNotice { get; set; } = string.Empty;

    /// <summary>Gets whether the current inventory is being read.</summary>
    [ObservableProperty]
    public partial bool IsSurfaceLoading { get; set; }

    /// <summary>Gets whether the surface inventory failed to load for the selected geometry.</summary>
    [ObservableProperty]
    public partial bool IsSurfaceInventoryFailed { get; set; }

    /// <summary>Gets the number of surfaces, shown after the list's title.</summary>
    public string SurfaceCount => this.Surfaces.Count.ToString(CultureInfo.CurrentCulture);

    /// <summary>Gets whether the selection keeps changes for surfaces its geometry no longer has.</summary>
    public bool HasKeptChanges => this.KeptChanges.Count > 0;

    /// <summary>Gets whether the list has a notice to show.</summary>
    public bool HasSurfaceNotice => this.SurfaceNotice.Length > 0;

    /// <summary>Resets the selection's overrides of surfaces their geometry does not have, as one undoable edit.</summary>
    /// <returns>Completion after the edit and a fresh surface projection.</returns>
    [RelayCommand]
    public async Task ClearUnresolvedSurfacesAsync()
    {
        if (!this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            return;
        }

        var built = await SurfaceEdits.ClearUnresolvedAsync(this.surfaces, this.session.Context, authoring.Scene, nodes.ConvertAll(static node => node.Id)).ConfigureAwait(true);
        await this.ExecuteSurfaceEditAsync(nodes, authoring, built).ConfigureAwait(true);
    }

    /// <summary>Reads the selected geometry's surface inventory again, without changing authored values.</summary>
    /// <returns>Completion after the current surfaces or their unavailable state are shown.</returns>
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
        var geometryUri = GeometryOf(nodes.FirstOrDefault())?.Geometry;
        var project = this.session.Context;
        this.IsSurfaceLoading = true;
        try
        {
            if (nodes.Count == 0 || geometryUri is null)
            {
                this.SetSurfacesUnavailable("Select geometry to view its surfaces.");
                return;
            }

            // The inspected scene is a document of the active project's workspace; a node of another scene is stale.
            if (this.authoringProvider?.Invoke() is not { } authoring || nodes.Exists(node => !ReferenceEquals(node.Scene, authoring.Scene)))
            {
                this.SetSurfacesUnavailable("The selection is no longer in the inspected scene.");
                return;
            }

            if (nodes.Exists(node => GeometryOf(node)?.Geometry != geometryUri))
            {
                this.SetSurfacesUnavailable("Select nodes that share one geometry.");
                return;
            }

            var read = await this.surfaces.ReadAsync(project, geometryUri, request.Token).ConfigureAwait(true);
            if (request.IsCancellationRequested || !this.IsCurrentSelection(nodes)
                || !ReferenceEquals(project, this.session.Context)
                || nodes.Exists(node => GeometryOf(node)?.Geometry != geometryUri))
            {
                return;
            }

            if (read is null)
            {
                this.SetSurfacesFailed("Surfaces unavailable. Cook this geometry, then refresh its surfaces.");
                return;
            }

            this.inventory = read;
            this.inventoryProject = project;
            this.LevelOfDetail.ShowError(error: null);
            this.IsSurfaceInventoryFailed = false;
            this.SurfaceNotice = read.Surfaces.IsEmpty ? "This geometry has no surfaces." : string.Empty;
        }
        catch (OperationCanceledException) when (request.IsCancellationRequested)
        {
        }
        catch (OperationCanceledException)
        {
            if (!this.disposed && ReferenceEquals(this.surfaceRefresh, request))
            {
                this.SetSurfacesFailed("Surfaces unavailable. Refresh them after the project is ready.");
            }
        }
        catch (Exception error) when (error is IOException or InvalidDataException or FormatException or InvalidOperationException)
        {
            if (!this.disposed && ReferenceEquals(this.surfaceRefresh, request))
            {
                this.SetSurfacesFailed("Surfaces unavailable. " + error.Message);
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

    /// <summary>Sets a flag of one surface on every selected node.</summary>
    /// <param name="card">The surface.</param>
    /// <param name="property">The flag, Visibility or Cast Shadows.</param>
    /// <param name="value">The value.</param>
    /// <param name="label">The history label.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task SetSurfaceFlagAsync(SurfaceCardViewModel card, PropertyId property, bool value, string label)
    {
        if (!this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            this.ProjectSurfaces();
            return;
        }

        var built = await SurfaceEdits.SetAsync(this.surfaces, this.session.Context, nodes.ConvertAll(static node => node.Id), card.Target, property, value, label).ConfigureAwait(true);
        await this.ExecuteSurfaceEditAsync(nodes, authoring, built).ConfigureAwait(true);
    }

    /// <summary>Resets one value of a surface on the selected nodes that override it.</summary>
    /// <param name="card">The surface.</param>
    /// <param name="property">The value's property.</param>
    /// <param name="label">The history label.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task ResetSurfaceValueAsync(SurfaceCardViewModel card, PropertyId property, string label)
    {
        if (this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            var reset = new SetOverrides(nodes.ConvertAll(static node => node.Id), [new OverrideWrite(property, card.Target.Part, LayoutRevision: null, Value: null)], label);
            await this.ExecuteSurfaceEditAsync(nodes, authoring, new SurfaceEdit(reset, Error: null)).ConfigureAwait(true);
        }
    }

    /// <summary>Resets every value of a surface on the selected nodes, as one undoable edit.</summary>
    /// <param name="card">The surface.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task ResetSurfaceAsync(SurfaceCardViewModel card)
    {
        if (this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            var reset = SurfaceEdits.Reset(nodes.ConvertAll(static node => node.Id), card.Target.Part, "Reset Surface");
            await this.ExecuteSurfaceEditAsync(nodes, authoring, new SurfaceEdit(reset, Error: null)).ConfigureAwait(true);
        }
    }

    /// <summary>Applies the changes kept for a lost surface to a current one.</summary>
    /// <param name="kept">The kept changes.</param>
    /// <param name="target">The surface that takes them.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task RetargetKeptChangesAsync(KeptSurfaceChangesViewModel kept, SurfaceCardViewModel target)
    {
        if (!this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            this.ProjectSurfaces();
            return;
        }

        var built = await SurfaceEdits.RetargetAsync(this.surfaces, this.session.Context, authoring.Scene, nodes.ConvertAll(static node => node.Id), kept.Part, target.Target).ConfigureAwait(true);
        await this.ExecuteSurfaceEditAsync(nodes, authoring, built).ConfigureAwait(true);
    }

    /// <summary>Removes the changes kept for a lost surface.</summary>
    /// <param name="kept">The kept changes.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task RemoveKeptChangesAsync(KeptSurfaceChangesViewModel kept)
    {
        if (this.TryGetSurfaceEditing(out var nodes, out var authoring))
        {
            var reset = SurfaceEdits.Reset(nodes.ConvertAll(static node => node.Id), kept.Part, "Remove Kept Surface Changes");
            await this.ExecuteSurfaceEditAsync(nodes, authoring, new SurfaceEdit(reset, Error: null)).ConfigureAwait(true);
        }
    }

    /// <summary>Captures the surface the material picker edits and lists the current materials, before the picker opens.</summary>
    /// <param name="card">The surface.</param>
    /// <returns>Completion after the picker lists the current materials.</returns>
    internal async Task PrepareMaterialPickerAsync(SurfaceCardViewModel card)
    {
        this.materialEdit = this.TryGetSurfaceEditing(out var nodes, out var authoring) ? new(authoring, nodes, card.Target) : null;
        var materials = this.materialEdit is null ? []
            : nodes.ConvertAll(node => GeometryOf(node)!.Overrides.TryGet(SceneSchema.Geometry.Surface.Material, card.Target.Part, out var value) ? value : null).Distinct().ToList();

        // The picker keeps listing the material the surface uses now, even when it is gone.
        this.pickedMaterial = materials.Count == 1 ? materials[0] : null;
        await this.assetProvider.RefreshAsync().ConfigureAwait(true);
        this.ApplyContentMaterials();
    }

    /// <summary>Applies a picked material to the surface the picker was opened for, on every selected node.</summary>
    /// <param name="card">The surface.</param>
    /// <param name="choice">The picked material.</param>
    /// <returns>Completion after the edit and the list's update.</returns>
    internal async Task ApplyMaterialAsync(SurfaceCardViewModel card, AssetChoice choice)
    {
        var picker = this.materialEdit;
        this.materialEdit = null;
        if (!this.IsInputEnabled || choice is not { IsEnabled: true, Asset: { } material } || picker is null
            || picker.Target != card.Target || !this.IsCurrentSelection(picker.Nodes)
            || !ReferenceEquals(this.authoringProvider?.Invoke(), picker.Authoring))
        {
            return;
        }

        var built = await SurfaceEdits.SetAsync(this.surfaces, this.session.Context, picker.Nodes.ConvertAll(static node => node.Id), picker.Target, SceneSchema.Geometry.Surface.Material.Id, material, "Edit Surface Material").ConfigureAwait(true);
        await this.ExecuteSurfaceEditAsync(picker.Nodes, picker.Authoring, built).ConfigureAwait(true);
    }

    /// <inheritdoc />
    protected override void OnInputEnabledChanged(bool enabled) => this.ProjectSurfaces();

    private static GeometryComponent? GeometryOf(SceneNode? node) => node?.Components.OfType<GeometryComponent>().FirstOrDefault();

    // Whether the inventory shown is the one of the nodes' shared geometry in the current project; a cook or reimport
    // refreshes it on its own.
    private bool ShowsInventoryOf(ICollection<SceneNode> nodes)
        => this.inventory is { } read && ReferenceEquals(this.inventoryProject, this.session.Context)
            && nodes.Count > 0 && nodes.All(node => GeometryOf(node)?.Geometry == read.GeometryUri);

    // The selected nodes and their scene's authoring, when the surfaces shown can be edited.
    private bool TryGetSurfaceEditing(out List<SceneNode> nodes, [NotNullWhen(true)] out SceneAuthoring? authoring)
    {
        nodes = this.selectedItems?.ToList() ?? [];
        authoring = this.authoringProvider?.Invoke();
        return !this.disposed && this.IsInputEnabled && !this.IsSurfaceLoading && this.inventory is not null
            && authoring is not null && nodes.Count > 0 && nodes.TrueForAll(node => GeometryOf(node)?.Geometry == this.inventory.GeometryUri);
    }

    // Runs a built edit; a stale surface reads the inventory again, anything else shows the scene as it now is.
    private async Task ExecuteSurfaceEditAsync(List<SceneNode> nodes, SceneAuthoring authoring, SurfaceEdit built)
    {
        if (built.Edit is { } edit)
        {
            _ = await authoring.ExecuteAsync(edit).ConfigureAwait(true);
        }

        if (!this.IsCurrentSelection(nodes))
        {
            return;
        }

        if (built.Error is not null)
        {
            await this.RefreshSurfacesAsync().ConfigureAwait(true);
        }
        else
        {
            this.ProjectSurfaces();
        }
    }

    // Updates the Level of detail rows, the cards and the kept changes from the inventory and the selected nodes'
    // overrides, keeping each row's and card's state.
    private void ProjectSurfaces()
    {
        var nodes = this.selectedItems?.ToList() ?? [];
        if (this.inventory is not { } read || nodes.Count == 0 || !nodes.TrueForAll(node => GeometryOf(node)?.Geometry == read.GeometryUri))
        {
            // The inventory is another selection's or geometry's; a refresh reads the current one.
            this.Surfaces = [];
            this.KeptChanges = [];
            this.LevelOfDetail.Clear();
            return;
        }

        var overrides = nodes.ConvertAll(node => GeometryOf(node)!.Overrides);
        var canEdit = this.IsInputEnabled && !this.IsSurfaceLoading;
        this.LevelOfDetail.Update(read, overrides, canEdit);
        var labels = read.Surfaces.GroupBy(static surface => surface.DisplayName, StringComparer.Ordinal)
            .ToDictionary(static group => group.Key, static group => group.Count(), StringComparer.Ordinal);
        var cards = new List<SurfaceCardViewModel>(read.Surfaces.Length);
        foreach (var (surface, index) in read.Surfaces.Select(static (surface, index) => (surface, index)))
        {
            var target = new SurfaceTarget(read.GeometryUri, surface.SurfaceId, read.LayoutRevision);
            var card = this.Surfaces.FirstOrDefault(existing => existing.Target.Part == target.Part) ?? new SurfaceCardViewModel(this, target);
            var name = string.IsNullOrWhiteSpace(surface.DisplayName) ? $"Surface {index + 1}"
                : labels[surface.DisplayName] > 1 ? $"{surface.DisplayName} ({index + 1})" : surface.DisplayName;
            card.Update(target, surface, name, overrides, this.ResolveMaterialDisplayName, canEdit);
            cards.Add(card);
        }

        if (!cards.SequenceEqual(this.Surfaces))
        {
            this.Surfaces = cards;
        }

        var current = read.Surfaces.Select(static surface => surface.SurfaceId).ToHashSet();
        var lost = overrides.SelectMany(static values => values.Parts.Keys)
            .Where(part => string.Equals(part.Kind, SceneSchema.Geometry.Surface.Kind, StringComparison.Ordinal) && !current.Contains(part.Id))
            .Distinct().ToList();
        var kept = lost.ConvertAll(part => this.KeptChanges.FirstOrDefault(existing => existing.Part == part) ?? new KeptSurfaceChangesViewModel(this, part));
        foreach (var entry in kept)
        {
            entry.Update(overrides, cards, this.ResolveMaterialDisplayName, canEdit);
        }

        if (!kept.SequenceEqual(this.KeptChanges))
        {
            this.KeptChanges = kept;
        }
    }

    private void SetSurfacesFailed(string message)
    {
        this.SetSurfacesUnavailable(message);
        this.IsSurfaceInventoryFailed = true;
    }

    private void SetSurfacesUnavailable(string message)
    {
        this.inventory = null;
        this.inventoryProject = null;
        this.materialEdit = null;
        this.IsSurfaceInventoryFailed = false;
        this.Surfaces = [];
        this.KeptChanges = [];
        this.LevelOfDetail.Clear();
        this.SurfaceNotice = message;
    }

    partial void OnSurfacesChanged(IReadOnlyList<SurfaceCardViewModel> value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.SurfaceCount));
    }

    partial void OnKeptChangesChanged(IReadOnlyList<KeptSurfaceChangesViewModel> value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasKeptChanges));
    }

    partial void OnSurfaceNoticeChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasSurfaceNotice));
    }

    partial void OnIsSurfaceLoadingChanged(bool value)
    {
        _ = value;
        this.ProjectSurfaces();
    }

    private sealed record MaterialPickerEdit(SceneAuthoring Authoring, List<SceneNode> Nodes, SurfaceTarget Target);
}
