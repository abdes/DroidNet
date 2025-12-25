// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Collections.ObjectModel;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Assets.Catalog;
using Oxygen.Core;
using Oxygen.Editor.World.Messages;

namespace Oxygen.Editor.World.Inspector.Geometry;

/// <summary>
/// ViewModel that provides editing support for the <see cref="GeometryComponent"/> of one or
/// more selected <see cref="SceneNode"/> instances.
/// </summary>
/// <remarks>
/// The view model exposes a list of available geometry asset groups (engine-provided and
/// content-provided) and tracks the currently selected geometry for the current selection
/// of scene nodes. When an asset is applied, the view model updates the geometry on all
/// selected nodes and sends a <see cref="SceneNodeGeometryAppliedMessage"/> via the
/// provided <see cref="IMessenger"/> to allow undo/redo and engine synchronization.
/// </remarks>
public partial class GeometryViewModel : ComponentPropertyEditor
{
    private readonly IAssetCatalog? assetCatalog;
    private readonly IMessenger? messenger;
    private readonly ObservableCollection<AssetPickerItem> contentItems = [];

    private ICollection<SceneNode>? selectedItems;

    /// <summary>
    /// Initializes a new instance of the <see cref="GeometryViewModel"/> class.
    /// </summary>
    /// <param name="assetCatalog">Optional asset catalog used to populate and
    /// subscribe to mesh assets from the content database. May be <see langword="null"/> for scenarios where
    /// asset content is not available.</param>
    /// <param name="messenger">Optional messenger used to send notifications such as applied
    /// geometry changes. May be <see langword="null"/>.</param>
    public GeometryViewModel(IAssetCatalog? assetCatalog = null, IMessenger? messenger = null)
    {
        this.assetCatalog = assetCatalog;
        this.messenger = messenger;

        var engineItems = new List<AssetPickerItem>
        {
            CreateEngineItem("Cube", AssetUris.BuildGeneratedUri("Cube"), "/Engine/BasicShapes/Cube"),
            CreateEngineItem("Sphere", AssetUris.BuildGeneratedUri("Sphere"), "/Engine/BasicShapes/Sphere"),
            CreateEngineItem("Plane", AssetUris.BuildGeneratedUri("Plane"), "/Engine/BasicShapes/Plane"),
            CreateEngineItem("Cylinder", AssetUris.BuildGeneratedUri("Cylinder"), "/Engine/BasicShapes/Cylinder"),
            CreateEngineItem("Cone", AssetUris.BuildGeneratedUri("Cone"), "/Engine/BasicShapes/Cone"),
            CreateEngineItem("Quad", AssetUris.BuildGeneratedUri("Quad"), "/Engine/BasicShapes/Quad"),
            CreateEngineItem("Torus", AssetUris.BuildGeneratedUri("Torus"), "/Engine/BasicShapes/Torus"),
            CreateEngineItem("ArrowGizmo", AssetUris.BuildGeneratedUri("ArrowGizmo"), "/Engine/BasicShapes/ArrowGizmo"),
        };

        // Start with Engine group only
        this.Groups =
        [
            new AssetGroup("Engine", engineItems),
            new AssetGroup("Content", this.contentItems),
        ];

        // Subscribe to asset changes for mesh assets
        if (assetCatalog is not null)
        {
            Debug.WriteLine("[GeometryViewModel] Subscribing to asset changes for mesh assets");

            // Capture the current synchronization context to ensure updates happen on the UI thread
            var syncContext = SynchronizationContext.Current;

            // Get initial snapshot and track seen assets to deduplicate replay
            _ = Task.Run(async () =>
            {
                var allAssets = await assetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All)).ConfigureAwait(false);
                var meshAssets = allAssets.Where(a => IsMesh(a.Uri)).ToList();

                var seenUris = new HashSet<Uri>(meshAssets.Select(a => a.Uri));

                // Populate initial items on the UI thread
                if (syncContext != null)
                {
                    syncContext.Post(_ =>
                    {
                        foreach (var asset in meshAssets)
                        {
                            Debug.WriteLine($"[GeometryViewModel] Initial mesh asset: {asset.Name}");
                            this.contentItems.Add(CreateContentItem(asset));
                        }
                    }, null);
                }
                else
                {
                    // Fallback if no sync context (e.g. unit tests), though ObservableCollection might fail if bound
                    foreach (var asset in meshAssets)
                    {
                        this.contentItems.Add(CreateContentItem(asset));
                    }
                }

                // Subscribe to future changes, deduplicating replayed items
                _ = assetCatalog.Changes
                    .Where(n => IsMesh(n.Uri))
                    .Subscribe(
                        notification =>
                        {
                            void HandleNotification(object? state)
                            {
                                if (notification.Kind == AssetChangeKind.Added)
                                {
                                    if (!seenUris.Contains(notification.Uri))
                                    {
                                        Debug.WriteLine($"[GeometryViewModel] New mesh asset added: {notification.Uri}");
                                        var record = new AssetRecord(notification.Uri);
                                        this.contentItems.Add(CreateContentItem(record));
                                        seenUris.Add(notification.Uri);
                                    }
                                }
                                else if (notification.Kind == AssetChangeKind.Removed)
                                {
                                    if (seenUris.Contains(notification.Uri))
                                    {
                                        Debug.WriteLine($"[GeometryViewModel] Mesh asset removed: {notification.Uri}");
                                        var itemToRemove = this.contentItems.FirstOrDefault(i => i.Uri == notification.Uri);
                                        if (itemToRemove != null)
                                        {
                                            this.contentItems.Remove(itemToRemove);
                                        }
                                        seenUris.Remove(notification.Uri);
                                    }
                                }
                            }

                            if (syncContext != null)
                            {
                                syncContext.Post(HandleNotification, null);
                            }
                            else
                            {
                                HandleNotification(null);
                            }
                        },
                        ex => Debug.WriteLine($"[GeometryViewModel] Error in asset stream: {ex.Message}"));
            });
        }
        else
        {
            Debug.WriteLine("[GeometryViewModel] No AssetCatalog available!");
        }
    }

    /// <summary>
    /// Gets the collection of geometry asset groups shown to the user. Each group contains
    /// a list of <see cref="AssetPickerItem"/> instances.
    /// </summary>
    [ObservableProperty]
    public partial IReadOnlyList<AssetGroup> Groups { get; set; } = [];

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
    /// the value will be "Mixed".
    /// </summary>
    [ObservableProperty]
    public partial string SelectedAssetName { get; set; } = "None";

    /// <summary>
    /// Gets the label to show on the asset button. Returns "Mixed" when <see cref="IsMixed"/>,
    /// otherwise returns the <see cref="SelectedAssetName"/>.
    /// </summary>
    public string AssetButtonLabel => this.IsMixed ? "Mixed" : this.SelectedAssetName;

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

        // Capture old snapshots
        var nodes = this.selectedItems.ToList();
        var oldSnapshots = nodes
            .Select(node => new GeometrySnapshot(node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri?.ToString()))
            .Where(static snapshot => snapshot.UriString is not null)
            .ToList();

        // Apply new URI to each node's GeometryComponent
        var newUri = item.Uri;
        foreach (var node in nodes)
        {
            var geo = node.Components.OfType<GeometryComponent>().FirstOrDefault();
            _ = geo?.Geometry = new(newUri);
        }

        // Capture new snapshots
        var newSnapshots = nodes
            .Select(node => new GeometrySnapshot(node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry?.Uri?.ToString()))
            .Where(static snapshot => snapshot.UriString is not null)
            .ToList();

        // Update viewmodel display
        this.SelectedAssetUriString = newUri.ToString();
        this.SelectedAssetName = ExtractNameFromUriString(this.SelectedAssetUriString ?? string.Empty);
        this.IsMixed = false;

        // Send message for undo/redo handling and engine sync
        try
        {
            _ = (this.messenger as IMessenger)?.Send(new SceneNodeGeometryAppliedMessage(nodes, oldSnapshots, newSnapshots, "Asset"));
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[GeometryViewModel] Error sending SceneNodeGeometryAppliedMessage: {ex.Message}");
        }

        await Task.CompletedTask.ConfigureAwait(true);
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
            this.SelectedAssetName = "Mixed";
            return;
        }

        this.SelectedAssetUriString = first;
        this.SelectedAssetName = first is null ? "None" : ExtractNameFromUriString(first);
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

    private static bool IsMesh(Uri uri)
    {
        var ext = Path.GetExtension(uri.AbsolutePath).ToUpperInvariant();
        return ext is ".MESH" or ".OGEO";
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
}
