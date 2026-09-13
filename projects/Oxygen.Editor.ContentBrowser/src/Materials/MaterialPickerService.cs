// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using System.Security.Cryptography;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Material picker projection over the shared ED-M06 Content Browser asset provider.
/// </summary>
public sealed partial class MaterialPickerService : IMaterialPickerService, IDisposable
{
    private readonly IContentBrowserAssetProvider assetProvider;
    private readonly IMessenger? messenger;
    private readonly BehaviorSubject<IReadOnlyList<MaterialPickerResult>> results = new([]);
    private readonly Lock pinnedMaterialsSync = new();
    private readonly Dictionary<string, MaterialPickerResult> pinnedMaterials = [with(StringComparer.OrdinalIgnoreCase)];
    private readonly IDisposable itemsSubscription;
    private readonly Lock previewSync = new();
    private readonly Dictionary<string, PreviewCacheEntry> previews = [with(StringComparer.OrdinalIgnoreCase)];
    private MaterialPickerFilter currentFilter = MaterialPickerFilter.Default;
    private IReadOnlyList<ContentBrowserAssetItem> latestItems = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="MaterialPickerService"/> class.</summary>
    /// <param name="assetProvider">The workspace asset status provider.</param>
    /// <param name="messenger">Asset change notifications.</param>
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
        => string.IsNullOrWhiteSpace(searchText) || row.DisplayName.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || row.MaterialUri.ToString().Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || (row.DescriptorPath?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true)
               || (row.CookedPath?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true);

    private static bool IsDefaultMaterial(Uri uri)
        => UriValuesEqual(uri, AssetUris.BuildGeneratedUri("Materials/Default"));

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

    private static MaterialPreviewColor? TryReadBaseColorPreview(string? descriptorPath, string? expectedHash = null)
    {
        if (descriptorPath is null || !File.Exists(descriptorPath))
        {
            return null;
        }

        try
        {
            var bytes = File.ReadAllBytes(descriptorPath);
            if (expectedHash is not null && !string.Equals(Convert.ToHexString(SHA256.HashData(bytes)), expectedHash, StringComparison.Ordinal))
            {
                return null;
            }

            var source = MaterialSourceReader.Read(bytes);
            var pbr = source.PbrMetallicRoughness;
            return new MaterialPreviewColor(pbr.BaseColorR, pbr.BaseColorG, pbr.BaseColorB, pbr.BaseColorA);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or FormatException or System.Text.Json.JsonException)
        {
            return null;
        }
    }

    private MaterialPickerResult? CreateResult(ContentBrowserAssetItem item)
        => item.Kind != AssetKind.Material ? null : new MaterialPickerResult(
            item.IdentityUri,
            item.DisplayName,
            item.PrimaryState,
            item.DerivedState,
            item.RuntimeAvailability,
            item.DescriptorPath,
            item.CookedPath,
            this.GetBaseColorPreview(item)) { CookStatus = item.CookStatus, CookActivity = item.CookActivity };

    private MaterialPreviewColor? GetBaseColorPreview(ContentBrowserAssetItem item)
    {
        if (item.PrimaryState is AssetState.Missing or AssetState.Broken)
        {
            return null;
        }

        if (item.DescriptorPath is not { } path || item.CookStatus?.SavedSourceHash is not { } hash)
        {
            return TryReadBaseColorPreview(item.DescriptorPath);
        }

        lock (this.previewSync)
        {
            if (this.previews.TryGetValue(path, out var cached) && string.Equals(cached.Hash, hash, StringComparison.Ordinal))
            {
                return cached.Color;
            }

            var preview = TryReadBaseColorPreview(path, hash);
            this.previews[path] = new(hash, preview);
            return preview;
        }
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
            .Where(row => IsIncluded(row, this.currentFilter) && MatchesSearch(row, this.currentFilter.SearchText))
            .ToList();

        var pinnedRows = this.ResolvePinnedMissingRows(rows);
        rows.AddRange(pinnedRows);

        if (this.currentFilter.IncludeGenerated && rows.TrueForAll(static row => !IsDefaultMaterial(row.MaterialUri)))
        {
            rows.Insert(
                0,
                new MaterialPickerResult(
                    AssetUris.BuildGeneratedUri("Materials/Default"),
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

    private sealed record PreviewCacheEntry(string Hash, MaterialPreviewColor? Color);
}
