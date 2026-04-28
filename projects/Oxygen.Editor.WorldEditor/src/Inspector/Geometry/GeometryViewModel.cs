// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Hosting.WinUI;
using Microsoft.UI.Dispatching;
using Oxygen.Assets.Catalog;
using Oxygen.Core;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;

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
public partial class GeometryViewModel : ComponentPropertyEditor, IDisposable
{
    private readonly IAssetCatalog assetCatalog;
    private readonly IMaterialPickerService materialPickerService;
    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly ObservableCollection<AssetPickerItem> contentItems = [];
    private readonly Dictionary<string, AssetPickerItem> contentItemsByKey = new(StringComparer.OrdinalIgnoreCase);
    private readonly ObservableCollection<MaterialPickerItem> contentMaterialItems = [];
    private readonly Dictionary<string, MaterialPickerItem> contentMaterialItemsByKey = new(StringComparer.OrdinalIgnoreCase);
    private readonly DispatcherQueue dispatcherQueue;

    private IDisposable? assetChangesSubscription;
    private IDisposable? materialResultsSubscription;

    private ICollection<SceneNode>? selectedItems;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryViewModel"/> class.
    /// </summary>
    /// <param name="assetCatalog">Asset catalog used to populate and subscribe to mesh assets.</param>
    /// <param name="commandService">Optional command service used to apply geometry edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public GeometryViewModel(
        HostingContext hosting,
        IAssetCatalog assetCatalog,
        IMaterialPickerService materialPickerService,
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        this.assetCatalog = assetCatalog;
        this.materialPickerService = materialPickerService;
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
        this.dispatcherQueue = hosting.Dispatcher;

        var engineItems = new List<AssetPickerItem>
        {
            CreateEngineItem("Cube", AssetUris.BuildGeneratedUri("BasicShapes/Cube"), "/Engine/Generated/BasicShapes/Cube"),
            CreateEngineItem("Sphere", AssetUris.BuildGeneratedUri("BasicShapes/Sphere"), "/Engine/Generated/BasicShapes/Sphere"),
            CreateEngineItem("Plane", AssetUris.BuildGeneratedUri("BasicShapes/Plane"), "/Engine/Generated/BasicShapes/Plane"),
            CreateEngineItem("Cylinder", AssetUris.BuildGeneratedUri("BasicShapes/Cylinder"), "/Engine/Generated/BasicShapes/Cylinder"),
            CreateEngineItem("Cone", AssetUris.BuildGeneratedUri("BasicShapes/Cone"), "/Engine/Generated/BasicShapes/Cone"),
            CreateEngineItem("Quad", AssetUris.BuildGeneratedUri("BasicShapes/Quad"), "/Engine/Generated/BasicShapes/Quad"),
            CreateEngineItem("Torus", AssetUris.BuildGeneratedUri("BasicShapes/Torus"), "/Engine/Generated/BasicShapes/Torus"),
            CreateEngineItem("ArrowGizmo", AssetUris.BuildGeneratedUri("BasicShapes/ArrowGizmo"), "/Engine/Generated/BasicShapes/ArrowGizmo"),
        };

        // Start with Engine group only
        this.Groups =
        [
            new AssetGroup("Engine", engineItems),
            new AssetGroup("Content", this.contentItems),
        ];

        this.MaterialGroups =
        [
            new MaterialGroup("Assignment", [CreateNoMaterialItem()]),
            new MaterialGroup(
                "Engine",
                [
                    CreateEngineMaterialItem(
                        "Default",
                        AssetUris.BuildGeneratedUri("Materials/Default"),
                        "/Engine/Generated/Materials/Default"),
                ]),
            new MaterialGroup("Content", this.contentMaterialItems),
        ];

        Debug.WriteLine("[GeometryViewModel] Subscribing to asset changes for mesh assets");

        this.materialResultsSubscription = this.materialPickerService.Results.Subscribe(
            rows => this.DispatchOnUi(() => this.ReplaceContentMaterialItems(rows)),
            ex => Debug.WriteLine($"[GeometryViewModel] Error in material picker stream: {ex}"));
        _ = this.materialPickerService.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false });

        // Get initial snapshot and track seen assets to deduplicate replay
        _ = Task.Run(async () =>
        {
            try
            {
                var allAssets = await this.assetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);
                var meshAssets = allAssets.Where(a => IsSelectableCookedMesh(a.Uri)).ToList();

                Debug.WriteLine($"[GeometryViewModel] Asset catalog returned {allAssets.Count} assets; {meshAssets.Count} mesh assets");

                var selectedByKey = new Dictionary<string, AssetRecord>(StringComparer.OrdinalIgnoreCase);
                foreach (var asset in meshAssets)
                {
                    var key = GetMeshLogicalKey(asset.Uri);
                    if (!selectedByKey.TryGetValue(key, out var existing) || IsPreferredMeshUri(asset.Uri, existing.Uri))
                    {
                        selectedByKey[key] = asset;
                    }
                }

                // Populate initial items on the UI thread.
                this.DispatchOnUi(() =>
                    {
                        foreach (var asset in selectedByKey.Values.OrderBy(a => AssetUriHelper.GetVirtualPath(a.Uri), StringComparer.OrdinalIgnoreCase))
                        {
                            Debug.WriteLine($"[GeometryViewModel] Initial mesh asset: {asset.Name} ({asset.Uri})");
                            var item = CreateContentItem(asset);
                            var key = GetMeshLogicalKey(asset.Uri);
                            this.contentItemsByKey[key] = item;
                            this.contentItems.Add(item);
                        }
                    });

                // Subscribe to future changes, deduplicating replayed items.
                this.assetChangesSubscription = this.assetCatalog.Changes
                    .Where(n => IsSelectableCookedMesh(n.Uri))
                    .Subscribe(
                        notification =>
                            this.DispatchOnUi(() =>
                                {
                                    this.ApplyAssetChange(notification);
                                }),
                        ex => Debug.WriteLine($"[GeometryViewModel] Error in asset stream: {ex}"));
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[GeometryViewModel] Failed to initialize content assets: {ex}");
            }
        });
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.assetChangesSubscription?.Dispose();
        this.materialResultsSubscription?.Dispose();
    }

    /// <summary>
    /// Gets the collection of geometry asset groups shown to the user. Each group contains
    /// a list of <see cref="AssetPickerItem"/> instances.
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

            return "\uE7C3"; // Geometry
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

            return "\uE8B9"; // Material
        }
    }

    /// <summary>
    /// Apply the specified geometry asset to all currently selected scene nodes.
    /// </summary>
    /// <param name="item">The asset picker item to apply. If <see langword="null"/> or not enabled the method returns immediately.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyAssetAsync(AssetPickerItem item)
    {
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

        var result = await this.commandService.EditGeometryAsync(
            context,
            nodes.Select(static node => node.Id).ToList(),
            new GeometryEdit(Optional<Uri?>.Supplied(newUri)),
            EditSessionToken.OneShot).ConfigureAwait(true);
        if (!result.Succeeded)
        {
            return;
        }

        // Update viewmodel display
        this.SelectedAssetUriString = newUri.ToString();
        this.SelectedAssetName = ExtractNameFromUriString(this.SelectedAssetUriString ?? string.Empty);
        this.IsMixed = false;
    }

    /// <summary>
    /// Applies the specified material assignment to all currently selected scene nodes.
    /// </summary>
    /// <param name="item">The material picker item to apply.</param>
    /// <returns>A <see cref="Task"/> that completes when the operation has finished.</returns>
    public async Task ApplyMaterialAsync(MaterialPickerItem item)
    {
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
        if (this.commandService is null || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var currentMaterial = !this.IsMaterialMixed && Uri.TryCreate(this.SelectedMaterialUriString, UriKind.Absolute, out var currentUri)
            ? currentUri
            : null;
        if (!this.IsMaterialMixed && UriValuesEqual(currentMaterial, item.Uri))
        {
            return;
        }

        var result = await this.commandService.EditMaterialSlotAsync(
            context,
            nodes.Select(static node => node.Id).ToList(),
            slotIndex: 0,
            item.Uri,
            EditSessionToken.OneShot).ConfigureAwait(true);
        if (!result.Succeeded)
        {
            return;
        }

        this.UpdateMaterialDisplay(item.Uri);
    }

    /// <summary>
    /// Refreshes the material picker from the live catalog before the material flyout is shown.
    /// </summary>
    /// <returns>A task that completes when the picker snapshot has been republished.</returns>
    public async Task RefreshMaterialPickerAsync()
    {
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

        var uriStrings = items
            .Select(static node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri?.ToString())
            .ToList();

        var first = uriStrings.FirstOrDefault();
        var isMixed = uriStrings.Exists(u => !string.Equals(u, first, StringComparison.Ordinal));

        this.IsMixed = isMixed;

        if (isMixed)
        {
            this.SelectedAssetUriString = null;
            this.SelectedAssetName = "--";
        }
        else
        {
            this.SelectedAssetUriString = first;
            this.SelectedAssetName = first is null ? "None" : ExtractNameFromUriString(first);
        }

        var materialUriStrings = items
            .Select(static node => NormalizeMaterialUri(node.Components.OfType<GeometryComponent>().FirstOrDefault()?.OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri)?.ToString())
            .ToList();

        var firstMaterial = materialUriStrings.FirstOrDefault();
        var isMaterialMixed = materialUriStrings.Exists(u => !string.Equals(u, firstMaterial, StringComparison.Ordinal));

        this.IsMaterialMixed = isMaterialMixed;

        if (isMaterialMixed)
        {
            this.SelectedMaterialUriString = null;
            this.SelectedMaterialName = "--";
            return;
        }

        this.SelectedMaterialUriString = firstMaterial;
        this.SelectedMaterialName = firstMaterial is null ? "None" : ExtractNameFromUriString(firstMaterial);
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

    private static AssetPickerItem CreateEngineItem(string name, string uriString, string displayPath)
    {
        var uri = new Uri(uriString, UriKind.Absolute);
        return new AssetPickerItem(
            Name: name,
            Uri: uri,
            DisplayType: "Static Mesh",
            DisplayPath: displayPath,
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: "\uE7C3");
    }

    private static bool IsSelectableCookedMesh(Uri uri)
    {
        // Geometry picker should list only runtime-consumable (cooked) assets that resolve
        // to canonical asset:/// URIs under the authoring mount point.
        // The aggregated ProjectAssetCatalog also includes:
        //  - project-root files (asset:///project/...) including .cooked/.imported
        //  - optionally mounted utility roots (Cooked/Imported/Build)
        //  - engine assets (handled separately in this view model)
        // We must exclude these to avoid duplicates and ensure applied URIs resolve correctly.

        var mountPoint = AssetUriHelper.GetMountPoint(uri);
        if (string.IsNullOrWhiteSpace(mountPoint))
        {
            return false;
        }

        if (IsExcludedMountPoint(mountPoint))
        {
            return false;
        }

        // Asset URIs can include mount points and may have a leading "//" in AbsolutePath.
        // Use the relative path when possible to make extension detection resilient.
        var path = AssetUriHelper.GetRelativePath(uri);
        if (string.IsNullOrWhiteSpace(path))
        {
            path = uri.AbsolutePath;
        }

        var ext = Path.GetExtension(path).ToUpperInvariant();
        return ext is ".MESH" or ".OGEO";

        static bool IsExcludedMountPoint(string mountPoint)
            => mountPoint.Equals("project", StringComparison.OrdinalIgnoreCase)
               || mountPoint.Equals("Engine", StringComparison.OrdinalIgnoreCase)
               || mountPoint.Equals("Cooked", StringComparison.OrdinalIgnoreCase)
               || mountPoint.Equals("Imported", StringComparison.OrdinalIgnoreCase)
               || mountPoint.Equals("Build", StringComparison.OrdinalIgnoreCase);
    }

    private static string GetMeshLogicalKey(Uri uri)
    {
        // Dedupe mesh variants that share the same virtual path but differ by extension.
        // Example: "/Content/Models/Foo.mesh" and "/Content/Models/Foo.ogeo" -> "/Content/Models/Foo"
        var virtualPath = AssetUriHelper.GetVirtualPath(uri);
        if (string.IsNullOrWhiteSpace(virtualPath))
        {
            virtualPath = uri.AbsolutePath;
        }

        var ext = Path.GetExtension(virtualPath);
        return string.IsNullOrEmpty(ext)
            ? virtualPath
            : virtualPath[..^ext.Length];
    }

    private static bool IsPreferredMeshUri(Uri candidate, Uri existing)
    {
        // Prefer canonical cooked .ogeo assets over legacy/raw .mesh when both exist.
        var candidateExt = Path.GetExtension(AssetUriHelper.GetRelativePath(candidate)).ToUpperInvariant();
        var existingExt = Path.GetExtension(AssetUriHelper.GetRelativePath(existing)).ToUpperInvariant();

        if (candidateExt == existingExt)
        {
            return false;
        }

        if (candidateExt == ".OGEO")
        {
            return true;
        }

        return false;
    }

    private static MaterialPickerItem CreateNoMaterialItem()
        => new(
            Name: "None",
            Uri: null,
            DisplayType: "No material override",
            DisplayPath: "<None>",
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: string.Empty);

    private static MaterialPickerItem CreateEngineMaterialItem(string name, string uriString, string displayPath)
        => new(
            Name: name,
            Uri: new Uri(uriString, UriKind.Absolute),
            DisplayType: "Material",
            DisplayPath: displayPath,
            Group: AssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: "\uE8B9");

    private static Uri? NormalizeMaterialUri(Uri? uri)
    {
        if (uri is null)
        {
            return null;
        }

        return string.Equals(uri.ToString(), "asset:///__uninitialized__", StringComparison.OrdinalIgnoreCase)
            ? null
            : uri;
    }

    private void ApplyAssetChange(AssetChange notification)
    {
        if (notification.Kind == AssetChangeKind.Added)
        {
            var key = GetMeshLogicalKey(notification.Uri);

            if (!this.contentItemsByKey.TryGetValue(key, out var existingItem))
            {
                Debug.WriteLine($"[GeometryViewModel] New mesh asset added: {notification.Uri}");
                var record = new AssetRecord(notification.Uri);
                var newItem = CreateContentItem(record);
                this.contentItemsByKey[key] = newItem;
                this.contentItems.Add(newItem);
                return;
            }

            if (IsPreferredMeshUri(notification.Uri, existingItem.Uri))
            {
                Debug.WriteLine($"[GeometryViewModel] Replacing mesh asset for key '{key}': {existingItem.Uri} -> {notification.Uri}");
                var record = new AssetRecord(notification.Uri);
                var newItem = CreateContentItem(record);

                var idx = this.contentItems.IndexOf(existingItem);
                if (idx >= 0)
                {
                    this.contentItems[idx] = newItem;
                }
                else
                {
                    this.contentItems.Add(newItem);
                }

                this.contentItemsByKey[key] = newItem;
            }
        }
        else if (notification.Kind == AssetChangeKind.Removed)
        {
            var key = GetMeshLogicalKey(notification.Uri);
            if (this.contentItemsByKey.TryGetValue(key, out var existingItem)
                && existingItem.Uri == notification.Uri)
            {
                Debug.WriteLine($"[GeometryViewModel] Mesh asset removed: {notification.Uri}");
                _ = this.contentItems.Remove(existingItem);
                _ = this.contentItemsByKey.Remove(key);
            }
        }
    }

    private void UpdateMaterialDisplay(Uri? uri)
    {
        this.SelectedMaterialUriString = uri?.ToString();
        this.SelectedMaterialName = uri is null ? "None" : ExtractNameFromUriString(uri.ToString());
        this.IsMaterialMixed = false;
    }

    private static bool UriValuesEqual(Uri? left, Uri? right)
        => string.Equals(left?.ToString(), right?.ToString(), StringComparison.OrdinalIgnoreCase);

    private void DispatchOnUi(Action action)
    {
        // Use the shared WinUI dispatcher helpers to handle shutdown / exceptions robustly.
        _ = this.dispatcherQueue.DispatchAsync(action)
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
    }

    private static AssetPickerItem CreateContentItem(AssetRecord asset)
    {
        string displayPath;
        var mountPoint = AssetUriHelper.GetMountPoint(asset.Uri);

        if (mountPoint.Equals("project", StringComparison.OrdinalIgnoreCase))
        {
            displayPath = "/" + AssetUriHelper.GetRelativePath(asset.Uri);
        }
        else
        {
            displayPath = AssetUriHelper.GetVirtualPath(asset.Uri);
        }

        return new AssetPickerItem(
            Name: asset.Name,
            Uri: asset.Uri,
            DisplayType: "Static Mesh",
            DisplayPath: displayPath,
            Group: AssetPickerGroup.Content,
            IsEnabled: true,
            ThumbnailModel: "\uE7C3");
    }

    private static MaterialPickerItem CreateContentMaterialItem(MaterialPickerResult asset)
        => new(
            Name: asset.DisplayName,
            Uri: asset.MaterialUri,
            DisplayType: asset.DisplayState == AssetState.Missing ? "Missing material" : "Material",
            DisplayPath: asset.DescriptorPath ?? asset.CookedPath ?? AssetUriHelper.GetVirtualPath(asset.MaterialUri),
            Group: AssetPickerGroup.Content,
            IsEnabled: asset.DisplayState is not AssetState.Missing and not AssetState.Broken,
            ThumbnailModel: "\uE8B9");

    private void ReplaceContentMaterialItems(IReadOnlyList<MaterialPickerResult> materials)
    {
        this.contentMaterialItems.Clear();
        this.contentMaterialItemsByKey.Clear();

        foreach (var material in materials.OrderBy(static m => m.DisplayName, StringComparer.OrdinalIgnoreCase))
        {
            var item = CreateContentMaterialItem(material);
            this.contentMaterialItems.Add(item);
            this.contentMaterialItemsByKey[material.MaterialUri.ToString()] = item;
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
