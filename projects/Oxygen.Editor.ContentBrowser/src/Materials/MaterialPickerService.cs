// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Core;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Material picker projection over the shared ED-M06 Content Browser asset provider.
/// </summary>
public sealed class MaterialPickerService : IMaterialPickerService, IDisposable
{
    private readonly IContentBrowserAssetProvider assetProvider;
    private readonly IMessenger? messenger;
    private readonly BehaviorSubject<IReadOnlyList<MaterialPickerResult>> results = new([]);
    private readonly Lock pinnedMaterialsSync = new();
    private readonly Dictionary<string, MaterialPickerResult> pinnedMaterials = new(StringComparer.OrdinalIgnoreCase);
    private readonly IDisposable itemsSubscription;
    private MaterialPickerFilter currentFilter = MaterialPickerFilter.Default;
    private IReadOnlyList<ContentBrowserAssetItem> latestItems = [];
    private bool disposed;

    public MaterialPickerService(
        IContentBrowserAssetProvider assetProvider,
        IMessenger? messenger = null)
    {
        this.assetProvider = assetProvider ?? throw new ArgumentNullException(nameof(assetProvider));
        this.messenger = messenger;
        this.itemsSubscription = this.assetProvider.Items
            .Subscribe(this.Publish);

        this.messenger?.Register<AssetsChangedMessage>(this, (_, message) => this.OnAssetsChanged(message));
    }

    /// <inheritdoc />
    public IObservable<IReadOnlyList<MaterialPickerResult>> Results => this.results.AsObservable();

    /// <inheritdoc />
    public async Task RefreshAsync(MaterialPickerFilter filter, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        this.currentFilter = filter;
        await this.assetProvider.RefreshAsync(AssetBrowserFilter.Default, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task<MaterialPickerResult?> ResolveAsync(Uri materialUri, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(materialUri);
        cancellationToken.ThrowIfCancellationRequested();
        if (!IsMaterialUri(materialUri))
        {
            return null;
        }

        var item = await this.assetProvider.ResolveAsync(materialUri, cancellationToken).ConfigureAwait(false);
        var result = item is null ? CreateMissingResult(materialUri) : this.CreateResult(item);
        if (result is not null)
        {
            lock (this.pinnedMaterialsSync)
            {
                this.pinnedMaterials[GetMaterialLogicalKey(materialUri)] = result;
            }
        }

        this.Publish(this.latestItems);
        return result;
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.messenger?.UnregisterAll(this);
        this.itemsSubscription.Dispose();
        this.results.Dispose();
    }

    private void OnAssetsChanged(AssetsChangedMessage message)
    {
        _ = message;
        _ = this.RefreshAsync(this.currentFilter);
    }

    private void Publish(IReadOnlyList<ContentBrowserAssetItem> items)
    {
        this.latestItems = items;
        var rows = items
            .Select(this.CreateResult)
            .OfType<MaterialPickerResult>()
            .Where(row => IsIncluded(row, this.currentFilter))
            .Where(row => MatchesSearch(row, this.currentFilter.SearchText))
            .ToList();

        var pinnedRows = this.ResolvePinnedMissingRows(rows);
        rows.AddRange(pinnedRows);

        if (this.currentFilter.IncludeGenerated && rows.All(static row => !IsDefaultMaterial(row.MaterialUri)))
        {
            rows.Insert(
                0,
                new MaterialPickerResult(
                    new Uri(AssetUris.BuildGeneratedUri("Materials/Default"), UriKind.Absolute),
                    "Default",
                    AssetState.Generated,
                    DerivedState: null,
                    AssetRuntimeAvailability.NotApplicable,
                    DescriptorPath: null,
                    CookedPath: null,
                    BaseColorPreview: new MaterialPreviewColor(1.0f, 1.0f, 1.0f, 1.0f)));
        }

        this.results.OnNext(rows
            .DistinctBy(static row => row.MaterialUri.ToString(), StringComparer.OrdinalIgnoreCase)
            .OrderBy(static row => row.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToList());
    }

    private IEnumerable<MaterialPickerResult> ResolvePinnedMissingRows(IReadOnlyList<MaterialPickerResult> rows)
    {
        MaterialPickerResult[] pinnedRows;
        lock (this.pinnedMaterialsSync)
        {
            pinnedRows = this.pinnedMaterials.Values.ToArray();
        }

        foreach (var pinned in pinnedRows)
        {
            if (rows.Any(row => UriValuesEqual(row.MaterialUri, pinned.MaterialUri)))
            {
                continue;
            }

            if (MatchesSearch(pinned, this.currentFilter.SearchText))
            {
                yield return pinned;
            }
        }
    }

    private MaterialPickerResult? CreateResult(ContentBrowserAssetItem item)
    {
        if (item.Kind != AssetKind.Material)
        {
            return null;
        }

        return new MaterialPickerResult(
            item.IdentityUri,
            item.DisplayName,
            item.PrimaryState,
            item.DerivedState,
            item.RuntimeAvailability,
            item.DescriptorPath,
            item.CookedPath,
            TryReadBaseColorPreview(item.DescriptorPath));
    }

    private static MaterialPickerResult CreateMissingResult(Uri materialUri)
        => new(
            materialUri,
            GetDisplayName(materialUri),
            AssetState.Missing,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            DescriptorPath: null,
            CookedPath: null,
            BaseColorPreview: null);

    private static bool IsIncluded(MaterialPickerResult row, MaterialPickerFilter filter)
        => StateIncluded(row.PrimaryState, filter)
           || (row.DerivedState is { } derivedState && StateIncluded(derivedState, filter));

    private static bool StateIncluded(AssetState state, MaterialPickerFilter filter)
        => state switch
        {
            AssetState.Generated => filter.IncludeGenerated,
            AssetState.Source or AssetState.Descriptor => filter.IncludeSource,
            AssetState.Cooked or AssetState.Stale => filter.IncludeCooked,
            AssetState.Missing or AssetState.Broken => filter.IncludeMissing,
            _ => false,
        };

    private static bool MatchesSearch(MaterialPickerResult row, string? searchText)
    {
        if (string.IsNullOrWhiteSpace(searchText))
        {
            return true;
        }

        return row.DisplayName.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || row.MaterialUri.ToString().Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || (row.DescriptorPath?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true)
               || (row.CookedPath?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true);
    }

    private static bool IsDefaultMaterial(Uri uri)
        => string.Equals(uri.ToString(), AssetUris.BuildGeneratedUri("Materials/Default"), StringComparison.OrdinalIgnoreCase);

    private static bool IsMaterialUri(Uri uri)
    {
        if (!string.Equals(uri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        var path = AssetUriHelper.GetRelativePath(uri);
        if (string.IsNullOrWhiteSpace(path))
        {
            path = uri.AbsolutePath;
        }

        return path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".omat", StringComparison.OrdinalIgnoreCase);
    }

    private static string GetMaterialLogicalKey(Uri uri)
    {
        var virtualPath = AssetUriHelper.GetVirtualPath(uri);
        if (string.IsNullOrWhiteSpace(virtualPath))
        {
            virtualPath = uri.AbsolutePath;
        }

        return virtualPath.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? virtualPath[..^".json".Length]
            : virtualPath;
    }

    private static string GetDisplayName(Uri uri)
    {
        var path = AssetUriHelper.GetVirtualPath(uri);
        var name = Path.GetFileName(path);
        foreach (var suffix in new[] { ".omat.json", ".omat" })
        {
            if (name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return name[..^suffix.Length];
            }
        }

        return string.IsNullOrWhiteSpace(name) ? uri.ToString() : name;
    }

    private static bool UriValuesEqual(Uri left, Uri right)
        => string.Equals(left.ToString(), right.ToString(), StringComparison.OrdinalIgnoreCase);

    private static MaterialPreviewColor? TryReadBaseColorPreview(string? descriptorPath)
    {
        if (descriptorPath is null || !File.Exists(descriptorPath))
        {
            return null;
        }

        try
        {
            var source = MaterialSourceReader.Read(File.ReadAllBytes(descriptorPath));
            var pbr = source.PbrMetallicRoughness;
            return new MaterialPreviewColor(pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or FormatException or System.Text.Json.JsonException)
        {
            return null;
        }
    }
}
