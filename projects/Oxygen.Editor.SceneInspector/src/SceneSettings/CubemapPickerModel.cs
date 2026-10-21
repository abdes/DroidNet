// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.IO.Abstractions;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Textures;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

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
    private readonly IObservable<IReadOnlyList<CubeTextureAsset>>? cubeTextures;
    private readonly IScheduler observerScheduler;
    private IReadOnlyDictionary<AssetPath, bool> storesRadiance = new Dictionary<AssetPath, bool>();
    private IDisposable? assetSubscription;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="CubemapPickerModel"/> class.</summary>
    /// <param name="cubeTextures">The shared <see cref="CubeTextures"/> feed, or null when no catalog is available.</param>
    /// <param name="observerScheduler">The scheduler that delivers catalog updates to the view.</param>
    internal CubemapPickerModel(IObservable<IReadOnlyList<CubeTextureAsset>>? cubeTextures, IScheduler observerScheduler)
    {
        this.cubeTextures = cubeTextures;
        this.observerScheduler = observerScheduler;
        this.Choices = new(this.Describe);
    }

    /// <summary>Raised after the offered cube textures change, so that a referenced texture's name can change.</summary>
    public event EventHandler? ChoicesUpdated;

    /// <summary>Gets the cube textures the picker offers, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel Choices { get; }

    /// <summary>Gets the display name of a cubemap reference.</summary>
    /// <param name="cubemap">The referenced cube texture, or null for none.</param>
    /// <returns>The texture's name, its file name when unlisted, or "None".</returns>
    public string DisplayName(AssetPath? cubemap)
        => cubemap is null ? "None"
            : this.Choices.Find(cubemap)?.Name ?? Path.GetFileNameWithoutExtension(cubemap.Name);

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
    /// <param name="assetProvider">The shared content catalog.</param>
    /// <param name="projects">The active project, whose files hold its texture descriptors.</param>
    /// <returns>The shared feed.</returns>
    internal static IObservable<IReadOnlyList<CubeTextureAsset>> CubeTextures(IAssetFeed assetProvider, IProjectContextService projects)
        => assetProvider.Items
            .Select(assets => (IReadOnlyList<CubeTextureAsset>)AssetRule.For(AssetKind.Texture).Apply(assets)
                .Select(asset => (Asset: asset, Cube: DescribeCube(projects.ActiveProject?.FileSystem, asset)))
                .Where(static entry => entry.Cube is not null)
                .Select(static entry => new CubeTextureAsset(entry.Asset, entry.Cube!.Value))
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

    // A project texture's descriptor says whether it is a cube; a packaged or library texture's cooked record does.
    // The value is whether the cube keeps float radiance, or null when the asset is not a cube.
    private static bool? DescribeCube(IFileSystem? files, AssetEntry asset)
        => asset.DescriptorPath is { } descriptorPath
            ? files is not null && Texture.TryRead(files, descriptorPath) is { IsCube: true } texture ? texture.StoresRadiance : null
            : asset.CookedMetadata?.Texture is { IsCube: true } cooked ? cooked.StoresRadiance : null;

    private void UpdateRows(IReadOnlyList<CubeTextureAsset> cubemaps)
    {
        if (this.disposed)
        {
            return;
        }

        this.storesRadiance = cubemaps.ToDictionary(static entry => entry.Asset.Identity, static entry => entry.StoresRadiance);
        this.Choices.Update([.. cubemaps.Select(static entry => entry.Asset)]);
        this.ChoicesUpdated?.Invoke(this, EventArgs.Empty);
    }

    // Lighting accepts only float radiance; an LDR cube can still be shown.
    private AssetChoice Describe(AssetEntry asset)
        => AssetChoice.For(asset, this.storesRadiance.GetValueOrDefault(asset.Identity) ? "Cube texture" : "LDR cube texture, display only", "\uE7C3");
}
