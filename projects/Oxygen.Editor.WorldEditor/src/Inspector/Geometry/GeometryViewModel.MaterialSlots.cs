// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>Projects current native slots and captures picker targets across asynchronous catalog work.</summary>
public sealed partial class GeometryViewModel
{
    private CancellationTokenSource? slotRefresh;
    private MaterialPickerEdit? materialEdit;

    /// <summary>Gets the existing slots of the selected geometry.</summary>
    [ObservableProperty]
    public partial IReadOnlyList<MaterialSlotChoice> MaterialSlots { get; set; } = [];

    /// <summary>Gets or sets the slot whose assignment is shown.</summary>
    [ObservableProperty]
    public partial MaterialSlotChoice? SelectedMaterialSlot { get; set; }

    /// <summary>Gets a concise inventory or unresolved-assignment status.</summary>
    [ObservableProperty]
    public partial string MaterialSlotNotice { get; set; } = string.Empty;

    /// <summary>Gets whether the selection has overrides that no longer resolve against its geometry.</summary>
    [ObservableProperty]
    public partial bool HasUnresolvedMaterialSlots { get; set; }

    /// <summary>Gets whether current native inventory is being read.</summary>
    [ObservableProperty]
    public partial bool IsMaterialSlotLoading { get; set; }

    /// <summary>Gets whether the geometry needs an explicit slot selector.</summary>
    public bool ShowMaterialSlotSelector => this.MaterialSlots.Count > 1;

    /// <summary>Gets whether an existing native slot can be targeted.</summary>
    public bool CanEditMaterialSlot => this.SelectedMaterialSlot is not null && !this.IsMaterialSlotLoading;

    /// <summary>Gets whether slot inventory failed to load for the selected geometry.</summary>
    [ObservableProperty]
    public partial bool IsMaterialSlotInventoryFailed { get; set; }

    /// <summary>Gets whether the material value should be shown in its error state.</summary>
    public bool HasMaterialSlotError => this.HasUnresolvedMaterialSlots || this.IsMaterialSlotInventoryFailed;

    /// <summary>Gets the material value tooltip: the assignment, followed by any slot status and its remedy.</summary>
    public string MaterialToolTip => this.MaterialSlotNotice.Length == 0
        ? this.MaterialButtonLabel
        : this.MaterialButtonLabel + System.Environment.NewLine + this.MaterialSlotNotice;

    /// <summary>Clears the selection's unresolved material overrides as one undoable edit.</summary>
    /// <returns>Completion after the edit and a fresh slot projection.</returns>
    [RelayCommand]
    public async Task ClearUnresolvedMaterialSlotsAsync()
    {
        if (this.disposed || !this.IsInputEnabled || this.projectContexts.ActiveProject is not { } project
            || this.selectedItems is not { Count: > 0 } selected || this.authoringProvider?.Invoke() is not { } authoring)
        {
            return;
        }

        var nodes = selected.ToList();
        var built = await MaterialSlotEdits.ClearUnresolvedAsync(this.materialSlots, project, authoring.Scene, nodes.ConvertAll(static node => node.Id)).ConfigureAwait(true);
        if (built.Edit is { } edit)
        {
            _ = await authoring.ExecuteAsync(edit).ConfigureAwait(true);
        }
        if (this.IsCurrentSelection(nodes))
        {
            await this.RefreshMaterialSlotsAsync().ConfigureAwait(true);
        }
    }

    /// <summary>Refreshes native inventory without changing authored assignments.</summary>
    /// <returns>Completion after current metadata or its unavailable state is presented.</returns>
    [RelayCommand]
    public async Task RefreshMaterialSlotsAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.slotRefresh?.Cancel();
        var request = new CancellationTokenSource();
        this.slotRefresh = request;
        var nodes = this.selectedItems?.ToList() ?? [];
        var geometryUri = nodes.FirstOrDefault()?.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry;
        var project = this.projectContexts.ActiveProject;
        this.IsMaterialSlotLoading = true;
        try
        {
            if (nodes.Count == 0 || geometryUri is null || project is null)
            {
                this.SetMaterialSlotsUnavailable("Select geometry in an active project to view its material slots.");
                return;
            }

            // The inspected scene is a document of the active project's workspace; a node of another scene is stale.
            if (this.authoringProvider?.Invoke() is not { } authoring || nodes.Exists(node => !ReferenceEquals(node.Scene, authoring.Scene)))
            {
                this.SetMaterialSlotsUnavailable("The selection is no longer in the inspected scene.");
                return;
            }

            if (nodes.Any(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry != geometryUri))
            {
                this.SetMaterialSlotsUnavailable("Select instances of the same geometry to edit material slots together.");
                return;
            }

            var inventory = await this.materialSlots.ReadAsync(project, geometryUri, request.Token).ConfigureAwait(true);
            if (request.IsCancellationRequested || !this.IsCurrentSelection(nodes)
                || !ReferenceEquals(project, this.projectContexts.ActiveProject)
                || nodes.Exists(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry != geometryUri))
            {
                return;
            }

            if (inventory is null)
            {
                this.SetMaterialSlotsFailed("Material slots unavailable. Cook this geometry, then refresh its slots.");
                return;
            }

            var selected = this.SelectedMaterialSlot?.Target;
            var labels = inventory.Slots.GroupBy(static slot => slot.DisplayName, StringComparer.Ordinal)
                .ToDictionary(static group => group.Key, static group => group.Count(), StringComparer.Ordinal);
            this.MaterialSlots = [.. inventory.Slots.Select((slot, index) => new MaterialSlotChoice(
                new(geometryUri, slot.SlotId, inventory.LayoutRevision),
                string.IsNullOrWhiteSpace(slot.DisplayName) ? $"Slot {index + 1}" : labels[slot.DisplayName] > 1 ? $"{slot.DisplayName} ({index + 1})" : slot.DisplayName))];
            this.SelectedMaterialSlot = this.MaterialSlots.FirstOrDefault(slot => selected is not null && slot.Target.GeometryUri == selected.GeometryUri && slot.Target.SlotId == selected.SlotId)
                ?? this.MaterialSlots.FirstOrDefault();
            var slotIds = inventory.Slots.Select(static slot => slot.SlotId).ToHashSet();
            var unresolved = nodes.SelectMany(static node => node.Components.OfType<GeometryComponent>().SelectMany(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>()))
                .Count(slot => slot.Target.GeometryUri != geometryUri || !slotIds.Contains(slot.Target.SlotId));
            this.HasUnresolvedMaterialSlots = unresolved != 0;
            this.IsMaterialSlotInventoryFailed = false;
            this.MaterialSlotNotice = unresolved == 1
                ? "One material assignment targets another geometry or a missing slot. Clear unresolved (✕) removes it; Undo restores it."
                : unresolved > 1 ? $"{unresolved} material assignments target another geometry or a missing slot. Clear unresolved (✕) removes them; Undo restores them."
                : this.MaterialSlots.Count == 0 ? "This geometry has no material slots." : string.Empty;
            this.UpdateSelectedMaterialSlotDisplay();
        }
        catch (OperationCanceledException) when (request.IsCancellationRequested)
        {
        }
        catch (OperationCanceledException)
        {
            if (!this.disposed && ReferenceEquals(this.slotRefresh, request))
            {
                this.SetMaterialSlotsFailed("Material slots unavailable. Refresh them after the project is ready.");
            }
        }
        catch (Exception error) when (error is IOException or InvalidDataException or FormatException or InvalidOperationException)
        {
            if (!this.disposed && ReferenceEquals(this.slotRefresh, request))
            {
                this.SetMaterialSlotsFailed("Material slots unavailable. " + error.Message);
            }
        }
        finally
        {
            if (ReferenceEquals(this.slotRefresh, request))
            {
                this.slotRefresh = null;
                this.IsMaterialSlotLoading = false;
            }

            request.Dispose();
        }
    }

    private void SetMaterialSlotsFailed(string message)
    {
        this.SetMaterialSlotsUnavailable(message);
        this.IsMaterialSlotInventoryFailed = true;
    }

    private void SetMaterialSlotsUnavailable(string message)
    {
        this.HasUnresolvedMaterialSlots = false;
        this.IsMaterialSlotInventoryFailed = false;
        this.MaterialSlots = [];
        this.SelectedMaterialSlot = null;
        this.MaterialSlotNotice = message;
        this.UpdateSelectedMaterialSlotDisplay();
    }

    private void UpdateSelectedMaterialSlotDisplay()
    {
        if (this.SelectedMaterialSlot is not { } selected)
        {
            this.IsMaterialMixed = false;
            this.SelectedMaterialUriString = null;
            this.SelectedMaterialName = "Unavailable";
            return;
        }

        var materials = (this.selectedItems ?? []).Select(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()
            ?.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault(slot => slot.Target.GeometryUri == selected.Target.GeometryUri && slot.Target.SlotId == selected.Target.SlotId)?.Material)
            .Distinct().ToArray();
        this.IsMaterialMixed = materials.Length > 1;
        this.SelectedMaterialUriString = this.IsMaterialMixed ? null : materials.FirstOrDefault()?.ToString();
        this.SelectedMaterialName = this.IsMaterialMixed ? "--"
            : materials.FirstOrDefault() is { } material ? this.ResolveMaterialDisplayName(material) : "Geometry default";
    }

    partial void OnSelectedMaterialSlotChanged(MaterialSlotChoice? value)
    {
        if (value?.Target != this.materialEdit?.Target)
        {
            this.materialEdit = null;
        }

        this.UpdateSelectedMaterialSlotDisplay();
        this.OnPropertyChanged(nameof(this.CanEditMaterialSlot));
    }

    partial void OnMaterialSlotsChanged(IReadOnlyList<MaterialSlotChoice> value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.ShowMaterialSlotSelector));
    }

    partial void OnMaterialSlotNoticeChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialToolTip));
    }

    partial void OnHasUnresolvedMaterialSlotsChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasMaterialSlotError));
    }

    partial void OnIsMaterialSlotInventoryFailedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.HasMaterialSlotError));
    }

    partial void OnIsMaterialSlotLoadingChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.CanEditMaterialSlot));
    }

    private sealed record MaterialPickerEdit(SceneAuthoring Authoring, List<SceneNode> Nodes, MaterialSlotTarget Target);
}
