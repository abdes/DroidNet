// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Catalog;

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
    private readonly IContentBrowserAssetProvider assetProvider;
    private readonly IMaterialPickerService materialPickerService;
    private readonly IBuiltinCatalogDiscovery builtins;
    private readonly ISceneContentDemandService contentDemand;
    private readonly IGeometryMaterialSlotProvider materialSlots;
    private readonly IProjectContextService projectContexts;
    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly PropertyBinding<Uri?> geometryUriBinding = new(SceneDocumentCommandService.Geometry.GeometryUriDescriptor);
    private readonly ObservableCollection<AssetPickerRow> contentItems = [];
    private readonly ObservableCollection<AssetPickerRow> engineItems = [];
    private readonly ObservableCollection<MaterialPickerRow> engineMaterials = [];
    private readonly Dictionary<string, AssetPickerRow> contentItemsByKey = [with(StringComparer.OrdinalIgnoreCase)];
    private readonly ObservableCollection<MaterialPickerRow> contentMaterialItems = [];
    private readonly Dictionary<string, MaterialPickerItem> contentMaterialItemsByKey = [with(StringComparer.OrdinalIgnoreCase)];
    private readonly DispatcherQueue dispatcherQueue;

    private IDisposable? assetChangesSubscription;
    private IDisposable? materialResultsSubscription;

    private ICollection<SceneNode>? selectedItems;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryViewModel"/> class.
    /// </summary>
    /// <param name="hosting">The WinUI hosting context used for UI-thread dispatch.</param>
    /// <param name="assetProvider">The shared asset identity, cook status and runtime availability feed.</param>
    /// <param name="materialPickerService">Material picker service used to populate material choices.</param>
    /// <param name="builtins">The shared engine-provided catalog and last-known availability.</param>
    /// <param name="contentDemand">The lifetime owner for saved-asset preview requests.</param>
    /// <param name="materialSlots">The native geometry slot inventory reader.</param>
    /// <param name="projectContexts">The active project lifetime.</param>
    /// <param name="commandService">Optional command service used to apply geometry edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public GeometryViewModel(
        HostingContext hosting,
        IContentBrowserAssetProvider assetProvider,
        IMaterialPickerService materialPickerService,
        IBuiltinCatalogDiscovery builtins,
        ISceneContentDemandService contentDemand,
        IGeometryMaterialSlotProvider materialSlots,
        IProjectContextService projectContexts,
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        this.assetProvider = assetProvider;
        this.materialPickerService = materialPickerService;
        this.builtins = builtins;
        this.contentDemand = contentDemand;
        this.materialSlots = materialSlots;
        this.projectContexts = projectContexts;
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
        this.dispatcherQueue = hosting.Dispatcher;

        this.Groups = [new AssetGroup("Engine", this.engineItems), new AssetGroup("Content", this.contentItems)];
        this.MaterialGroups = [new MaterialGroup("Assignment", [new(CreateNoMaterialItem())]), new MaterialGroup("Engine", this.engineMaterials), new MaterialGroup("Content", this.contentMaterialItems)];
        this.builtins.Changed += this.OnBuiltinCatalogChanged;
        this.ApplyBuiltinCatalog();
        _ = this.InitializeBuiltinsAsync();
        this.StartMaterialPickerSubscription();
        this.StartAssetCatalogSubscription();
    }

    /// <summary>
    /// Gets the collection of geometry asset groups shown to the user. Each group contains
    /// a list of stable <see cref="AssetPickerRow"/> instances.
    /// </summary>
    [ObservableProperty]
    public partial IReadOnlyList<AssetGroup> Groups { get; set; } = [];

    /// <summary>
    /// Gets the collection of material assignment groups shown to the user.
    /// </summary>
    [ObservableProperty]
    public partial IReadOnlyList<MaterialGroup> MaterialGroups { get; set; } = [];

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
    /// Gets or sets the URI string of the currently selected geometry asset. When multiple
    /// nodes are selected and they reference different assets this value will be <c>null</c>.
    /// </summary>
    [ObservableProperty]
    public partial string? SelectedAssetUriString { get; set; }

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
    /// Gets or sets the URI string of the currently selected material asset.
    /// </summary>
    [ObservableProperty]
    public partial string? SelectedMaterialUriString { get; set; }

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

            if (string.IsNullOrWhiteSpace(this.SelectedAssetUriString))
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

            if (string.IsNullOrWhiteSpace(this.SelectedMaterialUriString))
            {
                return string.Empty;
            }

            return "\uE790"; // Material
        }
    }

    /// <summary>
    /// Apply the specified geometry asset to all currently selected scene nodes.
    /// </summary>
    /// <param name="item">The asset picker item to apply. If <see langword="null"/> or not enabled the method returns immediately.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyAssetAsync(AssetPickerItem item)
    {
        if (!this.IsInputEnabled)
        {
            return;
        }

        if (item is null)
        {
            return;
        }

        if (!item.IsEnabled)
        {
            return;
        }

        if (this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        var newUri = item.Uri;
        if (this.commandService is null || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var edit = PropertyEdit.SingleEdit(SceneDocumentCommandService.Geometry.GeometryUri, newUri);
        var result = await this.commandService.EditPropertiesAsync(
            context,
            nodes.ConvertAll(static node => node.Id),
            edit,
            "Edit Geometry",
            EditSessionToken.OneShot).ConfigureAwait(true);
        if (!result.Succeeded || !this.IsCurrentSelection(nodes))
        {
            return;
        }

        // Update viewmodel display
        this.SelectedAssetUriString = newUri.ToString();
        this.SelectedAssetName = item.Name;
        this.IsMixed = false;
        this.contentDemand.RequestAssignment(context.Scene, nodes.ConvertAll(static node => node.Id), newUri, AssetKind.Geometry);
        await this.RefreshMaterialSlotsAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Applies the specified material assignment to all currently selected scene nodes.
    /// </summary>
    /// <param name="item">The material picker item to apply.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyMaterialAsync(MaterialPickerItem item)
    {
        var picker = this.materialEdit;
        this.materialEdit = null;
        if (!this.IsInputEnabled || item is not { IsEnabled: true } || picker is null
            || this.commandService is null || !this.IsCurrentSelection(picker.Nodes)
            || !ReferenceEquals(this.commandContextProvider?.Invoke()?.Scene, picker.Context.Scene))
        {
            return;
        }

        var result = await this.commandService.EditMaterialSlotAsync(picker.Context,
            picker.Nodes.ConvertAll(static node => node.Id), picker.Target, item.Uri, EditSessionToken.OneShot).ConfigureAwait(true);
        if (!this.IsCurrentSelection(picker.Nodes))
        {
            return;
        }

        if (result.Succeeded)
        {
            this.UpdateMaterialDisplay(item.Uri);
            this.contentDemand.RequestAssignment(picker.Context.Scene, picker.Nodes.ConvertAll(static node => node.Id), item.Uri, AssetKind.Material);
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
            && this.selectedItems is { Count: > 0 } nodes && this.commandContextProvider?.Invoke() is { } context
                ? new(context, nodes.ToList(), slot.Target) : null;
        await this.materialPickerService
            .RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false })
            .ConfigureAwait(true);

        if (!this.IsMaterialMixed
            && Uri.TryCreate(this.SelectedMaterialUriString, UriKind.Absolute, out var currentMaterialUri))
        {
            _ = await this.materialPickerService.ResolveAsync(currentMaterialUri).ConfigureAwait(true);
        }
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
            this.SelectedAssetUriString = null;
            this.SelectedAssetName = "--";
        }
        else
        {
            var selectedUri = this.geometryUriBinding.HasValue ? this.geometryUriBinding.Value : null;
            this.SelectedAssetUriString = selectedUri?.ToString();
            this.SelectedAssetName = selectedUri is null ? "None" : this.ResolveGeometryDisplayName(selectedUri);
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
        this.builtins.Changed -= this.OnBuiltinCatalogChanged;
        this.assetChangesSubscription?.Dispose();
        this.materialResultsSubscription?.Dispose();
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Extracts the user-facing material name from a material descriptor or cooked material URI.
    /// </summary>
    /// <param name="uriString">The material URI string.</param>
    /// <returns>The user-facing material name.</returns>
    internal static string ExtractMaterialNameFromUriString(string uriString)
    {
        var name = ExtractNameFromUriString(uriString);
        foreach (var suffix in new[] { ".omat.json", ".omat" })
        {
            if (name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return name[..^suffix.Length];
            }
        }

        return name;
    }

    private static string ExtractNameFromUriString(string uriString)
    {
        if (!Uri.TryCreate(uriString, UriKind.Absolute, out var uri))
        {
            return uriString;
        }

        var lastSegment = uri.AbsolutePath.Trim('/').Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
        return string.IsNullOrWhiteSpace(lastSegment) ? uriString : lastSegment;
    }

    private static AssetPickerItem CreateEngineItem(string name, Uri uri, string displayPath)
        => new(
            Name: name,
            Uri: uri,
            DisplayType: "Static Mesh",
            DisplayPath: displayPath,
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: "\uF158");

    private static MaterialPickerItem CreateNoMaterialItem()
        => new(
            Name: "Clear override",
            Uri: null,
            DisplayType: "Use the geometry default",
            DisplayPath: "Geometry default",
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: string.Empty);

    private static MaterialPickerItem CreateEngineMaterialItem(string name, Uri uri, string displayPath)
        => new(
            Name: name,
            Uri: uri,
            DisplayType: "Material",
            DisplayPath: displayPath,
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: "\uE790");

    private static bool UriValuesEqual(Uri? left, Uri? right)
        => string.Equals(left?.ToString(), right?.ToString(), StringComparison.OrdinalIgnoreCase);

    private static MaterialPickerItem CreateContentMaterialItem(MaterialPickerResult asset)
        => new(
            Name: asset.DisplayName,
            Uri: asset.MaterialUri,
            DisplayType: $"Material · {asset.StatusText}",
            DisplayPath: asset.DescriptorPath ?? asset.CookedPath ?? AssetUriHelper.GetVirtualPath(asset.MaterialUri),
            Group: AssetPickerGroup.Content,
            IsEnabled: asset.DisplayState is not AssetState.Missing and not AssetState.Broken,
            ThumbnailModel: "\uE8B9");

    private bool IsCurrentSelection(List<SceneNode> nodes)
        => !this.disposed && this.selectedItems?.Count == nodes.Count
            && nodes.TrueForAll(node => this.selectedItems.Any(selected => ReferenceEquals(selected, node)));

    private void StartMaterialPickerSubscription()
    {
        this.materialResultsSubscription = this.materialPickerService.Results.Subscribe(
            rows => this.DispatchOnUi(() => this.ReplaceContentMaterialItems(rows)),
            ex => Debug.WriteLine($"[GeometryViewModel] Error in material picker stream: {ex}"));
        _ = this.materialPickerService.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });
    }

    private void UpdateMaterialDisplay(Uri? uri)
    {
        this.SelectedMaterialUriString = uri?.ToString();
        this.SelectedMaterialName = uri is null ? "Geometry default" : this.ResolveMaterialDisplayName(uri);
        this.IsMaterialMixed = false;
    }

    private string ResolveMaterialDisplayName(Uri uri)
        => this.contentMaterialItemsByKey.TryGetValue(uri.ToString(), out var item)
            ? item.Name
            : ExtractMaterialNameFromUriString(uri.ToString());

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

    private void ReplaceContentMaterialItems(IReadOnlyList<MaterialPickerResult> materials)
    {
        var existing = this.contentMaterialItems.ToDictionary(static row => row.Item.Uri!.ToString(), StringComparer.OrdinalIgnoreCase);
        this.contentMaterialItemsByKey.Clear();
        var index = 0;
        foreach (var material in materials.OrderBy(static m => m.DisplayName, StringComparer.OrdinalIgnoreCase))
        {
            var item = CreateContentMaterialItem(material);
            var key = material.MaterialUri.ToString();
            if (existing.TryGetValue(key, out var row))
            {
                row.Update(item);
                if (!ReferenceEquals(this.contentMaterialItems[index], row))
                {
                    this.contentMaterialItems.Move(this.contentMaterialItems.IndexOf(row), index);
                }
            }
            else
            {
                this.contentMaterialItems.Insert(index, new(item));
            }

            this.contentMaterialItemsByKey[key] = item;
            index++;
        }

        while (this.contentMaterialItems.Count > index)
        {
            this.contentMaterialItems.RemoveAt(this.contentMaterialItems.Count - 1);
        }
    }

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

    partial void OnSelectedAssetUriStringChanged(string? value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.AssetThumbnailGlyph));
    }

    partial void OnIsMaterialMixedChanged(bool value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialButtonLabel));
        this.OnPropertyChanged(nameof(this.MaterialThumbnailGlyph));
    }

    partial void OnSelectedMaterialNameChanged(string value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialButtonLabel));
    }

    partial void OnSelectedMaterialUriStringChanged(string? value)
    {
        _ = value;
        this.OnPropertyChanged(nameof(this.MaterialThumbnailGlyph));
    }
}
