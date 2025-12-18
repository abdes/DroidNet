// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.WorldEditor.Messages;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Inspector;

/// <summary>
/// ViewModel for editing the Geometry component of selected <see cref="SceneNode"/> instances.
/// </summary>
public partial class GeometryViewModel : ComponentPropertyEditor
{
    private ICollection<SceneNode>? selectedItems;
    private readonly ContentBrowser.AssetsIndexingService? assetsIndexingService;
    private readonly IMessenger? messenger;
    private readonly List<GeometryAssetPickerItem> contentItems = [];

    [ObservableProperty]
    private IReadOnlyList<GeometryAssetGroup> groups = [];

    /// <inheritdoc />
    public override string Header => "Geometry";

    /// <inheritdoc />
    public override string Description => "Defines renderable geometry by referencing a geometry (mesh) asset.";

    [ObservableProperty]
    public partial bool IsMixed { get; set; }

    [ObservableProperty]
    public partial string? SelectedAssetUriString { get; set; }

    [ObservableProperty]
    public partial string SelectedAssetName { get; set; } = "None";

    public string AssetButtonLabel => this.IsMixed ? "Mixed" : this.SelectedAssetName;

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

    public GeometryViewModel(ContentBrowser.IAssetIndexingService? assetsIndexingService = null, IMessenger? messenger = null)
    {
        this.assetsIndexingService = assetsIndexingService as ContentBrowser.AssetsIndexingService;
        this.messenger = messenger;

        var engineItems = new List<GeometryAssetPickerItem>
        {
            CreateEngineItem("Cube", "asset://Generated/BasicShapes/Cube", "/Engine/BasicShapes/Cube"),
            CreateEngineItem("Sphere", "asset://Generated/BasicShapes/Sphere", "/Engine/BasicShapes/Sphere"),
            CreateEngineItem("Plane", "asset://Generated/BasicShapes/Plane", "/Engine/BasicShapes/Plane"),
            CreateEngineItem("Cylinder", "asset://Generated/BasicShapes/Cylinder", "/Engine/BasicShapes/Cylinder"),
        };

        // Start with Engine group only
        this.Groups =
        [
            new GeometryAssetGroup("Engine", engineItems),
            new GeometryAssetGroup("Content", this.contentItems),
        ];

        // Subscribe to asset changes for mesh assets
        if (assetsIndexingService is not null)
        {
            Debug.WriteLine("[GeometryViewModel] Subscribing to asset changes for mesh assets");

            // Get initial snapshot and track seen assets to deduplicate replay
            _ = Task.Run(async () =>
            {
                var meshAssets = await assetsIndexingService.QueryAssetsAsync(
                    a => a.AssetType == ContentBrowser.AssetType.Mesh).ConfigureAwait(false);

                var seenLocations = new HashSet<string>(meshAssets.Select(a => a.Location));

                // Populate initial items
                foreach (var asset in meshAssets)
                {
                    Debug.WriteLine($"[GeometryViewModel] Initial mesh asset: {asset.Name}");
                    this.contentItems.Add(CreateContentItem(asset));
                }

                // Subscribe to future changes, deduplicating replayed items
                _ = assetsIndexingService.AssetChanges
                    .Where(n => n.ChangeType == ContentBrowser.AssetChangeType.Added)
                    .Where(n => n.Asset.AssetType == ContentBrowser.AssetType.Mesh)
                    .Where(n => !seenLocations.Contains(n.Asset.Location)) // Deduplicate replay
                    .ObserveOn(System.Reactive.Concurrency.Scheduler.Default)
                    .Subscribe(
                        notification =>
                        {
                            Debug.WriteLine($"[GeometryViewModel] New mesh asset added: {notification.Asset.Name}");
                            this.contentItems.Add(CreateContentItem(notification.Asset));
                            seenLocations.Add(notification.Asset.Location);
                        },
                        ex => Debug.WriteLine($"[GeometryViewModel] Error in asset stream: {ex.Message}"));
            });
        }
        else
        {
            Debug.WriteLine("[GeometryViewModel] No assetsIndexingService available!");
        }
    }

    /// <summary>
    /// Apply the specified asset to the currently selected scene nodes.
    /// </summary>
    public async Task ApplyAssetAsync(GeometryAssetPickerItem item)
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
            .Select(node => new GeometrySnapshot(node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry.Uri?.ToString()))
            .ToList();

        // Apply new URI to each node's GeometryComponent
        var newUri = item.Uri;
        foreach (var node in nodes)
        {
            var geo = node.Components.OfType<GeometryComponent>().FirstOrDefault();
            if (geo is not null)
            {
                geo.Geometry.Uri = newUri;
            }
        }

        // Capture new snapshots
        var newSnapshots = nodes
            .Select(node => new GeometrySnapshot(node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry.Uri?.ToString()))
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

        await Task.CompletedTask;
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;

        var uriStrings = items
            .Select(static node => node.Components.OfType<GeometryComponent>().FirstOrDefault()?.Geometry.Uri?.ToString())
            .ToList();

        var first = uriStrings.FirstOrDefault();
        var isMixed = uriStrings.Any(u => !string.Equals(u, first, StringComparison.Ordinal));

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

    private static string ExtractNameFromUriString(string uriString)
    {
        if (!Uri.TryCreate(uriString, UriKind.Absolute, out var uri))
        {
            return uriString;
        }

        var lastSegment = uri.AbsolutePath.Trim('/').Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
        return string.IsNullOrWhiteSpace(lastSegment) ? uriString : lastSegment;
    }

    private static GeometryAssetPickerItem CreateEngineItem(string name, string uriString, string displayPath)
    {
        var uri = new Uri(uriString, UriKind.Absolute);
        return new GeometryAssetPickerItem(
            Name: name,
            Uri: uri,
            DisplayType: "Static Mesh",
            DisplayPath: displayPath,
            Group: GeometryAssetPickerGroup.Engine,
            IsEnabled: true,
            ThumbnailModel: "\uE7C3");
    }

    private static GeometryAssetPickerItem CreateContentItem(ContentBrowser.GameAsset asset)
    {
        // Create URI: asset://Content/{location}
        var uriString = $"asset://Content/{asset.Location}";
        var uri = new Uri(uriString, UriKind.Absolute);
        var displayPath = $"/Content/{asset.Location}";

        return new GeometryAssetPickerItem(
            Name: Path.GetFileNameWithoutExtension(asset.Name),
            Uri: uri,
            DisplayType: "Static Mesh",
            DisplayPath: displayPath,
            Group: GeometryAssetPickerGroup.Content,
            IsEnabled: false,
            ThumbnailModel: "\uE7C3");
    }
}
