// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;

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

    /// <summary>Gets whether current native inventory is being read.</summary>
    [ObservableProperty]
    public partial bool IsMaterialSlotLoading { get; set; }

    /// <summary>Gets whether the geometry needs an explicit slot selector.</summary>
    public bool ShowMaterialSlotSelector => this.MaterialSlots.Count > 1;

    /// <summary>Gets whether an existing native slot can be targeted.</summary>
    public bool CanEditMaterialSlot => this.SelectedMaterialSlot is not null && !this.IsMaterialSlotLoading;

    /// <summary>Gets whether an inventory/status message needs presentation.</summary>
    public bool HasMaterialSlotNotice => this.MaterialSlotNotice.Length != 0;

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
        var geometryUri = nodes.FirstOrDefault()?.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri;
        var project = this.projectContexts.ActiveProject;
        this.IsMaterialSlotLoading = true;
        try
        {
            if (nodes.Count == 0 || geometryUri is null || project is null)
            {
                this.SetMaterialSlotsUnavailable("Select geometry in an active project to view its material slots.");
                return;
            }

            if (nodes.Any(node => node.Scene.Project.ProjectInfo.Id != project.ProjectId))
            {
                this.SetMaterialSlotsUnavailable("The originating project is no longer active.");
                return;
            }

            if (nodes.Any(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri != geometryUri))
            {
                this.SetMaterialSlotsUnavailable("Select instances of the same geometry to edit material slots together.");
                return;
            }

            this.MaterialSlotNotice = "Loading material slots…";
            var inventory = await this.materialSlots.ReadAsync(project, geometryUri, request.Token).ConfigureAwait(true);
            if (request.IsCancellationRequested || !this.IsCurrentSelection(nodes)
                || !ReferenceEquals(project, this.projectContexts.ActiveProject)
                || nodes.Any(node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri != geometryUri))
            {
                return;
            }

            if (inventory is null)
            {
                this.SetMaterialSlotsUnavailable("Material slots unavailable. Cook or refresh this geometry.");
                return;
            }

            var selected = this.SelectedMaterialSlot?.Target;
            var labels = inventory.Slots.GroupBy(static slot => slot.DisplayName, StringComparer.Ordinal)
                .ToDictionary(static group => group.Key, static group => group.Count(), StringComparer.Ordinal);
            this.MaterialSlots = inventory.Slots.Select((slot, index) => new MaterialSlotChoice(
                new(geometryUri, slot.SlotId, inventory.LayoutRevision),
                string.IsNullOrWhiteSpace(slot.DisplayName) ? $"Slot {index + 1}"
                    : labels[slot.DisplayName] > 1 ? $"{slot.DisplayName} ({index + 1})" : slot.DisplayName)).ToArray();
            this.SelectedMaterialSlot = this.MaterialSlots.FirstOrDefault(slot => selected is not null && slot.Target.GeometryUri == selected.GeometryUri && slot.Target.SlotId == selected.SlotId)
                ?? this.MaterialSlots.FirstOrDefault();
            var slotIds = inventory.Slots.Select(static slot => slot.SlotId).ToHashSet();
            var unresolved = nodes.SelectMany(static node => node.Components.OfType<GeometryComponent>().SelectMany(static geometry => geometry.OverrideSlots.OfType<MaterialsSlot>()))
                .Count(slot => slot.Target.GeometryUri != geometryUri || !slotIds.Contains(slot.Target.SlotId));
            this.MaterialSlotNotice = unresolved == 1 ? "One material assignment needs repair."
                : unresolved > 1 ? $"{unresolved} material assignments need repair."
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
                this.SetMaterialSlotsUnavailable("Material slots unavailable. Refresh after the project is ready.");
            }
        }
        catch (Exception error) when (error is IOException or InvalidDataException or FormatException or InvalidOperationException)
        {
            if (!this.disposed && ReferenceEquals(this.slotRefresh, request))
            {
                this.SetMaterialSlotsUnavailable("Material slots unavailable. " + error.Message);
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

    private void SetMaterialSlotsUnavailable(string message)
    {
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
            ?.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault(slot => slot.Target.GeometryUri == selected.Target.GeometryUri && slot.Target.SlotId == selected.Target.SlotId)?.Material.Uri)
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
        this.OnPropertyChanged(nameof(this.HasMaterialSlotNotice));
    }

    partial void OnIsMaterialSlotLoadingChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.CanEditMaterialSlot));
    }

    private sealed record MaterialPickerEdit(SceneDocumentCommandContext Context, List<SceneNode> Nodes, MaterialSlotTarget Target);
}
