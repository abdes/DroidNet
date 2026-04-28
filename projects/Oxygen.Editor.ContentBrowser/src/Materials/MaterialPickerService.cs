// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Core;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.Materials;

/// <summary>
/// Default material picker catalog adapter for ED-M05 material assignment.
/// </summary>
public sealed class MaterialPickerService : IMaterialPickerService, IDisposable
{
    private readonly IAssetCatalog assetCatalog;
    private readonly IProjectContextService projectContextService;
    private readonly IMessenger? messenger;
    private readonly BehaviorSubject<IReadOnlyList<MaterialPickerResult>> results = new([]);
    private readonly Lock knownMaterialsSync = new();
    private readonly Dictionary<string, Uri> knownMaterialUris = new(StringComparer.OrdinalIgnoreCase);
    private readonly IDisposable changesSubscription;
    private MaterialPickerFilter currentFilter = MaterialPickerFilter.Default;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialPickerService"/> class.
    /// </summary>
    /// <param name="assetCatalog">The asset catalog.</param>
    public MaterialPickerService(
        IAssetCatalog assetCatalog,
        IProjectContextService projectContextService,
        IMessenger? messenger = null)
    {
        this.assetCatalog = assetCatalog ?? throw new ArgumentNullException(nameof(assetCatalog));
        this.projectContextService = projectContextService ?? throw new ArgumentNullException(nameof(projectContextService));
        this.messenger = messenger;
        this.changesSubscription = this.assetCatalog.Changes
            .Where(static change => IsMaterialUri(change.Uri) || (change.PreviousUri is not null && IsMaterialUri(change.PreviousUri)))
            .Subscribe(_change => { _ = this.RefreshAsync(this.currentFilter); });

        this.messenger?.Register<AssetsChangedMessage>(this, (_, message) => this.OnAssetsChanged(message));
    }

    /// <inheritdoc />
    public IObservable<IReadOnlyList<MaterialPickerResult>> Results => this.results.AsObservable();

    /// <inheritdoc />
    public async Task RefreshAsync(MaterialPickerFilter filter, CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        this.currentFilter = filter;

        var records = (await this.assetCatalog
            .QueryAsync(new AssetQuery(filter.Scope, filter.SearchText), cancellationToken)
            .ConfigureAwait(false)).ToList();
        this.AddKnownMaterialRecords(records);

        var rows = records
            .Where(static record => IsMaterialUri(record.Uri))
            .GroupBy(static record => GetMaterialLogicalKey(record.Uri), StringComparer.OrdinalIgnoreCase)
            .Select(this.CreateMergedResult)
            .OfType<MaterialPickerResult>()
            .Where(row => IsIncluded(row, filter))
            .OrderBy(static row => row.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToList();

        if (filter.IncludeGenerated)
        {
            rows.Insert(
                0,
                new MaterialPickerResult(
                    new Uri(AssetUris.BuildGeneratedUri("Materials/Default"), UriKind.Absolute),
                    "Default",
                    AssetState.Generated,
                    DescriptorPath: null,
                    CookedPath: null,
                    BaseColorPreview: new MaterialPreviewColor(1.0f, 1.0f, 1.0f, 1.0f)));
        }

        this.results.OnNext(rows);
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

        var rows = await this.assetCatalog
            .QueryAsync(new AssetQuery(AssetQueryScope.All), cancellationToken)
            .ConfigureAwait(false);
        var found = rows.FirstOrDefault(record => UriValuesEqual(record.Uri, materialUri));
        if (found is not null)
        {
            return this.CreateMergedResult([found]);
        }

        if (IsSourceMaterialUri(materialUri) && this.TryGetSourcePath(materialUri) is { } descriptorPath && File.Exists(descriptorPath))
        {
            return this.CreateMergedResult([new AssetRecord(materialUri)]);
        }

        return new MaterialPickerResult(
            materialUri,
            GetDisplayName(materialUri),
            AssetState.Missing,
            DescriptorPath: null,
            CookedPath: null,
            BaseColorPreview: null);
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
        this.changesSubscription.Dispose();
        this.results.Dispose();
    }

    private void OnAssetsChanged(AssetsChangedMessage message)
    {
        if (message.AssetUri is { } assetUri && IsMaterialUri(assetUri))
        {
            _ = this.PublishKnownMaterialAsync(assetUri);
            return;
        }

        _ = this.RefreshAsync(this.currentFilter);
    }

    private async Task PublishKnownMaterialAsync(Uri materialUri)
    {
        MaterialPickerResult? row;
        try
        {
            row = await this.ResolveAsync(materialUri).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or FormatException)
        {
            row = new MaterialPickerResult(
                materialUri,
                GetDisplayName(materialUri),
                AssetState.Broken,
                DescriptorPath: null,
                CookedPath: null,
                BaseColorPreview: null);
        }

        if (row is null || !IsIncluded(row, this.currentFilter))
        {
            await this.RefreshAsync(this.currentFilter).ConfigureAwait(false);
            return;
        }

        var logicalKey = GetMaterialLogicalKey(row.MaterialUri);
        lock (this.knownMaterialsSync)
        {
            this.knownMaterialUris[logicalKey] = row.MaterialUri;
        }

        var current = this.results.Value
            .Where(existing => !string.Equals(GetMaterialLogicalKey(existing.MaterialUri), logicalKey, StringComparison.OrdinalIgnoreCase))
            .Append(row)
            .Where(existing => IsIncluded(existing, this.currentFilter))
            .OrderBy(static existing => existing.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToList();

        this.results.OnNext(current);
    }

    private void AddKnownMaterialRecords(List<AssetRecord> records)
    {
        Uri[] knownUris;
        lock (this.knownMaterialsSync)
        {
            knownUris = this.knownMaterialUris.Values.ToArray();
        }

        foreach (var uri in knownUris)
        {
            if (!records.Any(record => UriValuesEqual(record.Uri, uri))
                && IsSourceMaterialUri(uri)
                && this.TryGetSourcePath(uri) is { } descriptorPath
                && File.Exists(descriptorPath))
            {
                records.Add(new AssetRecord(uri));
            }
        }
    }

    private MaterialPickerResult? CreateMergedResult(IEnumerable<AssetRecord> records)
    {
        var snapshot = records.ToList();
        if (snapshot.Count == 0)
        {
            return null;
        }

        var source = snapshot.FirstOrDefault(static record => IsSourceMaterialUri(record.Uri));
        var cooked = snapshot.FirstOrDefault(static record => IsCookedMaterialUri(record.Uri));
        var selectedUri = source?.Uri ?? cooked?.Uri;
        if (selectedUri is null)
        {
            return null;
        }

        var descriptorPath = source is null ? null : this.TryGetSourcePath(source.Uri);
        var cookedPath = cooked is null ? this.TryGetCookedPath(GetCookedUri(selectedUri)) : this.TryGetCookedPath(cooked.Uri);
        var state = DetermineState(source?.Uri, descriptorPath, cooked?.Uri, cookedPath);
        return new MaterialPickerResult(
            selectedUri,
            GetDisplayName(selectedUri),
            state,
            DescriptorPath: descriptorPath,
            CookedPath: cookedPath,
            BaseColorPreview: TryReadBaseColorPreview(descriptorPath));
    }

    private static AssetState DetermineState(Uri? sourceUri, string? descriptorPath, Uri? cookedUri, string? cookedPath)
    {
        _ = sourceUri;
        _ = cookedUri;
        if (descriptorPath is not null && !File.Exists(descriptorPath))
        {
            return AssetState.Broken;
        }

        if (cookedPath is not null && !File.Exists(cookedPath))
        {
            return descriptorPath is null ? AssetState.Broken : AssetState.Source;
        }

        if (descriptorPath is not null && cookedPath is not null)
        {
            var descriptorTime = File.GetLastWriteTimeUtc(descriptorPath);
            var cookedTime = File.GetLastWriteTimeUtc(cookedPath);
            return descriptorTime > cookedTime ? AssetState.Stale : AssetState.Cooked;
        }

        return descriptorPath is not null ? AssetState.Source : AssetState.Cooked;
    }

    private static bool IsIncluded(MaterialPickerResult row, MaterialPickerFilter filter)
        => row.State switch
        {
            AssetState.Generated => filter.IncludeGenerated,
            AssetState.Source => filter.IncludeSource,
            AssetState.Cooked or AssetState.Stale => filter.IncludeCooked,
            AssetState.Missing or AssetState.Broken => filter.IncludeMissing,
            _ => false,
        };

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

        return IsCookedMaterialPath(path) || IsSourceMaterialPath(path);
    }

    private static bool IsSourceMaterialUri(Uri uri)
        => IsSourceMaterialPath(AssetUriHelper.GetRelativePath(uri));

    private static bool IsCookedMaterialUri(Uri uri)
        => IsCookedMaterialPath(AssetUriHelper.GetRelativePath(uri));

    private static bool IsSourceMaterialPath(string path)
        => path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase);

    private static bool IsCookedMaterialPath(string path)
        => path.EndsWith(".omat", StringComparison.OrdinalIgnoreCase);

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

    private string? TryGetSourcePath(Uri materialSourceUri)
    {
        if (this.projectContextService.ActiveProject is not { } project)
        {
            return null;
        }

        if (!TryResolveSourcePath(project, materialSourceUri, out var sourcePath))
        {
            return null;
        }

        return sourcePath;
    }

    private string? TryGetCookedPath(Uri cookedUri)
    {
        if (this.projectContextService.ActiveProject is not { } project)
        {
            return null;
        }

        var relative = cookedUri.AbsolutePath.TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
        return Path.Combine(project.ProjectRoot, ".cooked", relative);
    }

    private static Uri GetCookedUri(Uri materialUri)
    {
        var path = materialUri.AbsolutePath;
        if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            path = path[..^".json".Length];
        }

        return new Uri($"{AssetUris.Scheme}://{path}");
    }

    private static bool TryResolveSourcePath(ProjectContext project, Uri materialSourceUri, out string sourcePath)
    {
        sourcePath = string.Empty;
        var path = Uri.UnescapeDataString(materialSourceUri.AbsolutePath).TrimStart('/');
        var slash = path.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return false;
        }

        var mountName = path[..slash];
        var mountRelativePath = path[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        if (mount is null)
        {
            return false;
        }

        sourcePath = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativePath));
        return true;
    }

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
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or FormatException)
        {
            return null;
        }
    }
}
