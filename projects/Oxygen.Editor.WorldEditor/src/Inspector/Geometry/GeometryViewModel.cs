// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>
/// ViewModel that provides editing support for the <see cref="GeometryComponent"/> of one or
/// more selected <see cref="SceneNode"/> instances.
/// </summary>
/// <remarks>
/// The view model exposes a list of available geometry asset groups and routes edits through
/// the scene document command service so undo, dirty state, diagnostics, and live sync stay
/// on the document command path.
/// </remarks>
public sealed partial class GeometryViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly IAssetFeed assetProvider;
    private readonly IGeometryMaterialSlotProvider materialSlots;
    private readonly IProjectContextService projectContexts;
    private readonly Func<SceneAuthoring?>? authoringProvider;
    private readonly PropertyBinding<AssetPath?> geometryUriBinding = new(SceneProperties.Geometry.GeometryUriDescriptor);
    private readonly DispatcherQueue dispatcherQueue;

    private IDisposable? assetChangesSubscription;
    private IDisposable? materialResultsSubscription;
    private IReadOnlyList<AssetEntry> latestMaterialAssets = [];

    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The WinUI hosting context used for UI-thread dispatch.</param>
    /// <param name="assetProvider">The shared asset identity, cook status and runtime availability feed.</param>
    /// <param name="materialSlots">The native geometry slot inventory reader.</param>
    /// <param name="projectContexts">The active project lifetime.</param>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public GeometryViewModel(
        HostingContext hosting,
        IAssetFeed assetProvider,
        IGeometryMaterialSlotProvider materialSlots,
        IProjectContextService projectContexts,
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        this.assetProvider = assetProvider;
        this.materialSlots = materialSlots;
        this.projectContexts = projectContexts;
        this.authoringProvider = authoringProvider;
        this.dispatcherQueue = hosting.Dispatcher;

        this.StartMaterialPickerSubscription();
        this.StartAssetCatalogSubscription();
    }

    /// <summary>Gets the geometries the picker offers, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel GeometryChoices { get; } = new(static asset => AssetChoice.For(asset, "Geometry", "\uF158"));

    /// <summary>Gets the materials the slot picker offers, after the choice that clears the slot's override.</summary>
    public AssetPickerModel MaterialChoices { get; } = new(static asset => AssetChoice.For(asset, "Material", "\uE8B9"), ClearMaterialChoice);

    /// <inheritdoc />
    public override string Header => "Geometry";

    /// <inheritdoc />
    public override string Description => "Defines renderable geometry by referencing a geometry (mesh) asset.";

    /// <summary>
    /// Gets or sets a value indicating whether the currently selected scene nodes have
    /// differing geometry assets. When <c>true</c>, the UI should indicate a mixed state.
    /// </summary>
    [ObservableProperty]
    public partial bool IsMixed { get; set; }

    /// <summary>
    /// Gets or sets the currently selected geometry asset. When multiple nodes are selected and
    /// they reference different assets this value will be <c>null</c>.
    /// </summary>
    [ObservableProperty]
    public partial AssetPath? SelectedGeometry { get; set; }

    /// <summary>
    /// Gets or sets the display name of the currently selected geometry asset. When no asset
    /// is selected the value will be "None" and when multiple different assets are selected
    /// the value will be "--".
    /// </summary>
    [ObservableProperty]
    public partial string SelectedAssetName { get; set; } = "None";

    /// <summary>
    /// Gets the label to show on the asset button. Returns "--" when <see cref="IsMixed"/>,
    /// otherwise returns the <see cref="SelectedAssetName"/>.
    /// </summary>
    public string AssetButtonLabel => this.IsMixed ? "--" : this.SelectedAssetName;

    /// <summary>
    /// Gets or sets a value indicating whether the currently selected scene nodes have
    /// differing material assignments.
    /// </summary>
    [ObservableProperty]
    public partial bool IsMaterialMixed { get; set; }

    /// <summary>
    /// Gets or sets the currently selected material asset.
    /// </summary>
    [ObservableProperty]
    public partial AssetPath? SelectedMaterial { get; set; }

    /// <summary>
    /// Gets or sets the display name of the currently selected material asset.
    /// </summary>
    [ObservableProperty]
    public partial string SelectedMaterialName { get; set; } = "None";

    /// <summary>
    /// Gets the label to show on the material button.
    /// </summary>
    public string MaterialButtonLabel => this.IsMaterialMixed ? "--" : this.SelectedMaterialName;

    /// <summary>
    /// Gets a glyph string to use as a thumbnail indicator for the currently selected asset.
    /// Returns an indeterminate glyph when <see cref="IsMixed"/>, an empty string when no
    /// asset is selected, or a geometry glyph for a valid selection.
    /// </summary>
    public string AssetThumbnailGlyph
    {
        get
        {
            if (this.IsMixed)
            {
                return "\uE9D9"; // Indeterminate / mixed
            }

            if (this.SelectedGeometry is null)
            {
                return string.Empty; // Empty placeholder
            }

            return "\uF158"; // Geometry
        }
    }

    /// <summary>
    /// Gets a glyph string to use as a thumbnail indicator for the selected material.
    /// </summary>
    public string MaterialThumbnailGlyph
    {
        get
        {
            if (this.IsMaterialMixed)
            {
                return "\uE9D9"; // Indeterminate / mixed
            }

            if (this.SelectedMaterial is null)
            {
                return string.Empty;
            }

            return "\uE790"; // Material
        }
    }

    /// <summary>
    /// Apply the specified geometry asset to all currently selected scene nodes.
    /// </summary>
    /// <param name="choice">The picked geometry. A disabled choice, or one without an asset, is ignored.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyAssetAsync(AssetChoice choice)
    {
        if (!this.IsInputEnabled || choice is not { IsEnabled: true, Asset: { } geometry })
        {
            return;
        }

        if (this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        if (this.authoringProvider?.Invoke() is not { } authoring)
        {
            return;
        }

        var result = await authoring.ExecuteAsync(new SetGeometry(nodes.ConvertAll(static node => node.Id), geometry)).ConfigureAwait(true);
        if (!result.Succeeded || !this.IsCurrentSelection(nodes))
        {
            return;
        }

        // Update viewmodel display
        this.SelectedGeometry = geometry;
        this.SelectedAssetName = choice.Name;
        this.IsMixed = false;
        await this.RefreshMaterialSlotsAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Applies the specified material assignment to all currently selected scene nodes.
    /// </summary>
    /// <param name="choice">The picked material, or the choice that clears the override.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyMaterialAsync(AssetChoice choice)
    {
        var picker = this.materialEdit;
        this.materialEdit = null;
        if (!this.IsInputEnabled || choice is not { IsEnabled: true } || picker is null
            || !this.IsCurrentSelection(picker.Nodes)
            || !ReferenceEquals(this.authoringProvider?.Invoke(), picker.Authoring)
            || this.projectContexts.ActiveProject is not { } project)
        {
            return;
        }

        var nodeIds = picker.Nodes.ConvertAll(static node => node.Id);
        var built = await MaterialSlotEdits.SetAsync(this.materialSlots, project, nodeIds, picker.Target, choice.Asset).ConfigureAwait(true);
        var succeeded = built.Edit is { } edit && (await picker.Authoring.ExecuteAsync(edit).ConfigureAwait(true)).Succeeded;
        if (!this.IsCurrentSelection(picker.Nodes))
        {
            return;
        }

        if (succeeded)
        {
            this.UpdateMaterialDisplay(choice.Asset);
        }

        await this.RefreshMaterialSlotsAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Refreshes the material picker from the live catalog before the material flyout is shown.
    /// </summary>
    /// <returns>A task that completes when the picker snapshot has been republished.</returns>
    public async Task RefreshMaterialPickerAsync()
    {
        this.materialEdit = this.SelectedMaterialSlot is { } slot && this.CanEditMaterialSlot
            && this.selectedItems is { Count: > 0 } nodes && this.authoringProvider?.Invoke() is { } authoring
                ? new(authoring, nodes.ToList(), slot.Target) : null;
        await this.assetProvider.RefreshAsync().ConfigureAwait(true);
        this.ApplyContentMaterials();
    }

    /// <summary>
    /// Update the view model state from the given collection of selected scene nodes.
    /// </summary>
    /// <param name="items">The currently selected scene nodes.</param>
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;

        var targets = items
            .Select(static node => new { Node = node, Geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault() })
            .Where(static target => target.Geometry is not null)
            .ToList();
        var nodeIds = targets.ConvertAll(static target => target.Node.Id);
        var targetsByNode = targets.ToDictionary(static target => target.Node.Id, static target => (object?)target.Geometry);

        this.geometryUriBinding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
        this.IsMixed = this.geometryUriBinding.IsMixed;

        if (this.geometryUriBinding.IsMixed)
        {
            this.SelectedGeometry = null;
            this.SelectedAssetName = "--";
        }
        else
        {
            var selected = this.geometryUriBinding.HasValue ? this.geometryUriBinding.Value : null;
            this.SelectedGeometry = selected;
            this.SelectedAssetName = selected is null ? "None" : this.ResolveGeometryDisplayName(selected);
        }

        // Authored assignments change synchronously, including during undo/redo.
        this.UpdateSelectedMaterialSlotDisplay();
        _ = this.RefreshMaterialSlotsAsync();
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.disposed = true;
        this.slotRefresh?.Cancel();
        this.slotRefresh?.Dispose();
        this.assetChangesSubscription?.Dispose();
        this.materialResultsSubscription?.Dispose();
        GC.SuppressFinalize(this);
    }

    /// <summary>Gets the user-facing name of an asset: its name without its file type.</summary>
    /// <param name="asset">The asset.</param>
    /// <returns>The user-facing name.</returns>
    internal static string NameOf(AssetPath asset) => AssetFileTypes.RemoveSuffix(asset.Name);

    private static AssetChoice ClearMaterialChoice { get; } = new("Clear override", Asset: null, "Use the geometry default", "Geometry default", IsEnabled: true, Glyph: string.Empty);

    private bool IsCurrentSelection(List<SceneNode> nodes)
        => !this.disposed && this.selectedItems?.Count == nodes.Count
            && nodes.TrueForAll(node => this.selectedItems.Any(selected => ReferenceEquals(selected, node)));

    private void StartMaterialPickerSubscription()
    {
        this.materialResultsSubscription = this.assetProvider.Items.Subscribe(
            assets => this.DispatchOnUi(() =>
            {
                this.latestMaterialAssets = assets;
                this.ApplyContentMaterials();
            }),
            ex => Debug.WriteLine($"[GeometryViewModel] Error in material picker stream: {ex}"));
    }

    // Lists the materials the slot accepts, keeping the one assigned now even when it is gone.
    private void ApplyContentMaterials()
        => this.MaterialChoices.Update(AssetRule.For(AssetKind.Material).Apply(this.latestMaterialAssets, this.IsMaterialMixed ? null : this.SelectedMaterial));

    private void UpdateMaterialDisplay(AssetPath? material)
    {
        this.SelectedMaterial = material;
        this.SelectedMaterialName = material is null ? "Geometry default" : this.ResolveMaterialDisplayName(material);
        this.IsMaterialMixed = false;
    }

    private string ResolveMaterialDisplayName(AssetPath material)
        => this.MaterialChoices.Find(material)?.Name ?? NameOf(material);

    // Use the shared WinUI dispatcher helpers to handle shutdown / exceptions robustly.
    private void DispatchOnUi(Action action)
        => _ = this.dispatcherQueue.DispatchAsync(action)
            .ContinueWith(
                t =>
                {
                    if (t.IsFaulted)
                    {
                        Debug.WriteLine($"[GeometryViewModel] UI dispatch failed: {t.Exception}");
                    }
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);

    partial void OnIsMixedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetButtonLabel));
        this.OnPropertyChanged(nameof(this.AssetThumbnailGlyph));
    }

    partial void OnSelectedAssetNameChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetButtonLabel));
    }

    partial void OnSelectedGeometryChanged(AssetPath? value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetThumbnailGlyph));
    }

    partial void OnIsMaterialMixedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialButtonLabel));
        this.OnPropertyChanged(nameof(this.MaterialToolTip));
        this.OnPropertyChanged(nameof(this.MaterialThumbnailGlyph));
    }

    partial void OnSelectedMaterialNameChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialButtonLabel));
        this.OnPropertyChanged(nameof(this.MaterialToolTip));
    }

    partial void OnSelectedMaterialChanged(AssetPath? value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialThumbnailGlyph));
    }
}
