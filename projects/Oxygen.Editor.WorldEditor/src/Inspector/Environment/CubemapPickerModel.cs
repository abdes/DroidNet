// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.World.Inspector.Geometry;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Lists the project's cube textures for a cubemap field, with search.</summary>
/// <remarks>
/// A texture is listed when its descriptor's cube settings produce a cube
/// texture. The environment inspector builds one <see cref="CubeTextures"/>
/// feed for all its cubemap fields, so each catalog update reads the
/// descriptors once, off the notification scheduler. It also requests one
/// catalog refresh for all its texture fields, through the exposure section.
/// </remarks>
public sealed partial class CubemapPickerModel : ObservableObject, IDisposable
{
    private readonly IObservable<IReadOnlyList<ContentBrowserAssetItem>>? cubeTextures;
    private readonly IScheduler observerScheduler;
    private IDisposable? assetSubscription;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="CubemapPickerModel"/> class.</summary>
    /// <param name="cubeTextures">The shared <see cref="CubeTextures"/> feed, or null when no catalog is available.</param>
    /// <param name="observerScheduler">The scheduler that delivers catalog updates to the view.</param>
    internal CubemapPickerModel(IObservable<IReadOnlyList<ContentBrowserAssetItem>>? cubeTextures, IScheduler observerScheduler)
    {
        this.cubeTextures = cubeTextures;
        this.observerScheduler = observerScheduler;
    }

    /// <summary>Gets or sets the search text that filters the listed cubemaps.</summary>
    [ObservableProperty]
    public partial string SearchText { get; set; } = string.Empty;

    /// <summary>Gets the listed cube textures in display order.</summary>
    public ObservableCollection<AssetPickerRow> Rows { get; } = [];

    /// <summary>Gets the cube textures matching the search text.</summary>
    public IReadOnlyList<AssetPickerRow> FilteredRows => string.IsNullOrWhiteSpace(this.SearchText)
        ? this.Rows.ToArray()
        : this.Rows.Where(row => row.Item.Name.Contains(this.SearchText, StringComparison.OrdinalIgnoreCase)
            || row.Item.DisplayPath.Contains(this.SearchText, StringComparison.OrdinalIgnoreCase)).ToArray();

    /// <summary>Gets the display name of a cubemap reference.</summary>
    /// <param name="cubemap">The referenced cube texture, or null for none.</param>
    /// <returns>The texture's name, its file name when unlisted, or "None".</returns>
    public string DisplayName(Uri? cubemap)
        => cubemap is null ? "None"
            : this.Rows.FirstOrDefault(row => row.Item.Uri == cubemap)?.Item.Name
                ?? Path.GetFileNameWithoutExtension(cubemap.AbsolutePath);

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
    }

    /// <summary>
    /// Creates the catalog's cube textures in display order, shared by every cubemap field of one
    /// inspector: the descriptors are read once per catalog update and the last list is replayed.
    /// </summary>
    /// <param name="assetProvider">The shared content catalog, or null when none is available.</param>
    /// <returns>The shared feed, or null without a catalog.</returns>
    internal static IObservable<IReadOnlyList<ContentBrowserAssetItem>>? CubeTextures(IContentBrowserAssetProvider? assetProvider)
        => assetProvider?.Items
            .Select(static assets => (IReadOnlyList<ContentBrowserAssetItem>)assets
                .Where(static asset => asset.Kind == AssetKind.Texture && !asset.IsBuiltin
                    && asset.DescriptorPath is { } descriptor && TextureSourceAssetImporter.IsCubeDescriptor(descriptor))
                .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase)
                .ToArray())
            .Replay(1)
            .RefCount();

    /// <summary>Starts at most one catalog feed.</summary>
    internal void Start()
    {
        if (this.disposed || this.cubeTextures is null || this.assetSubscription is not null)
        {
            return;
        }

        this.assetSubscription = this.cubeTextures.ObserveOn(this.observerScheduler).Subscribe(this.UpdateRows);
    }

    partial void OnSearchTextChanged(string value) => this.OnPropertyChanged(nameof(this.FilteredRows));

    private void UpdateRows(IReadOnlyList<ContentBrowserAssetItem> cubemaps)
    {
        if (this.disposed)
        {
            return;
        }

        var wanted = cubemaps.Select(static asset => asset.IdentityUri.AbsoluteUri).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var removed in this.Rows.Where(row => !wanted.Contains(row.Item.Uri.AbsoluteUri)).ToArray())
        {
            _ = this.Rows.Remove(removed);
        }

        for (var index = 0; index < cubemaps.Count; index++)
        {
            var asset = cubemaps[index];
            var item = new AssetPickerItem(asset.DisplayName, asset.IdentityUri, "Cube texture · " + asset.PrimaryBadge, asset.DisplayPath, AssetPickerGroup.Content, asset.IsSelectable, "");
            var row = this.Rows.FirstOrDefault(candidate => candidate.Item.Uri == asset.IdentityUri);
            if (row is null)
            {
                this.Rows.Insert(Math.Min(index, this.Rows.Count), new(item));
                continue;
            }

            row.Update(item);
            var currentIndex = this.Rows.IndexOf(row);
            if (currentIndex != index)
            {
                this.Rows.Move(currentIndex, Math.Min(index, this.Rows.Count - 1));
            }
        }

        this.OnPropertyChanged(nameof(this.FilteredRows));
    }
}
