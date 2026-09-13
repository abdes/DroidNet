// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Import.Materials;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Default ED-M06 asset identity reducer.
/// </summary>
public sealed class AssetIdentityReducer : IAssetIdentityReducer
{
    private readonly Lock descriptorValidationSync = new();
    private readonly Dictionary<string, DescriptorValidationEntry> descriptorValidationCache = [with(StringComparer.OrdinalIgnoreCase)];

    /// <inheritdoc />
    public IReadOnlyList<ContentBrowserAssetItem> Reduce(
        IReadOnlyList<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope,
        AssetBrowserFilter filter)
    {
        ArgumentNullException.ThrowIfNull(records);
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(cookScope);
        ArgumentNullException.ThrowIfNull(filter);

        return records
            .Where(record => IsBrowsableRecord(record.Uri, project))
            .GroupBy(static record => record.Cooked is null ? GetLogicalKey(record.Uri) : AssetUriHelper.GetVirtualPath(record.Uri), StringComparer.OrdinalIgnoreCase)
            .Select(group => this.CreateItem(group.ToList(), project, cookScope))
            .OfType<ContentBrowserAssetItem>()
            .Where(item => IsIncluded(item, filter) && MatchesSearch(item, filter.SearchText))
            .OrderBy(static item => item.DisplayPath, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    /// <inheritdoc />
    public ContentBrowserAssetItem CreateMissing(Uri uri)
        => new(
            IdentityUri: uri,
            DisplayName: GetDisplayName(uri),
            Kind: GetKind(uri),
            PrimaryState: AssetState.Missing,
            DerivedState: null,
            RuntimeAvailability: AssetRuntimeAvailability.Unknown,
            DisplayPath: AssetUriHelper.GetVirtualPath(uri),
            SourcePath: null,
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [AssetIdentityDiagnosticCodes.ResolveMissing],
            IsSelectable: false);

    /// <summary>Resolves a descriptor path only when it stays inside its indexed root.</summary>
    /// <param name="metadata">The indexed physical source.</param>
    /// <returns>The contained path, or null for an invalid descriptor path.</returns>
    internal static string? TryResolveIndexedDescriptorPath(CookedAssetMetadata metadata)
    {
        try
        {
            var relative = metadata.DescriptorRelativePath.Replace('/', Path.DirectorySeparatorChar);
            var root = Path.GetFullPath(metadata.RootFolderPath).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            var path = Path.GetFullPath(Path.Combine(root, relative));
            return !Path.IsPathRooted(relative) && path.StartsWith(root, StringComparison.OrdinalIgnoreCase) ? path : null;
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }
    }

    /// <summary>Maps native asset types to browser types.</summary>
    /// <param name="assetType">The native type identifier.</param>
    /// <returns>The browser type, or Unknown for unsupported types.</returns>
    internal static AssetKind GetCookedKind(byte assetType) => assetType switch
    {
        1 => AssetKind.Material,
        2 => AssetKind.Geometry,
        3 => AssetKind.Scene,
        _ => AssetKind.Unknown,
    };

    /// <summary>Formats an asset name without descriptor suffixes.</summary>
    /// <param name="uri">The source or cooked identity.</param>
    /// <returns>The readable asset name.</returns>
    internal static string GetDisplayName(Uri uri)
    {
        var path = AssetUriHelper.GetVirtualPath(uri);
        var name = Path.GetFileName(path);
        foreach (var suffix in new[] { ".omat.json", ".omat", ".ogeo.json", ".ogeo", ".oscene.json", ".oscene", ".otex.json", ".otex" })
        {
            if (name.EndsWith(suffix, StringComparison.OrdinalIgnoreCase))
            {
                return name[..^suffix.Length];
            }
        }

        return string.IsNullOrWhiteSpace(name) ? uri.ToString() : name;
    }

    private static ContentBrowserAssetItem CreateGeneratedItem(AssetRecord selected, GeneratedAssetMetadata recipe)
        => new(
            IdentityUri: selected.Uri,
            DisplayName: selected.Name,
            Kind: GetKind(new Uri(AssetUris.Scheme + "://" + recipe.CookedVirtualPath)),
            PrimaryState: AssetState.Generated,
            DerivedState: null,
            RuntimeAvailability: AssetRuntimeAvailability.Unknown,
            DisplayPath: AssetUriHelper.GetVirtualPath(selected.Uri),
            SourcePath: null,
            DescriptorPath: null,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: true)
        {
            Generated = recipe,
        };

    private static bool IsIncluded(ContentBrowserAssetItem item, AssetBrowserFilter filter)
        => (filter.Kinds.Count == 0 || filter.Kinds.Contains(item.Kind))
            && (StateIncluded(item.PrimaryState, filter) || (item.DerivedState is { } derivedState && StateIncluded(derivedState, filter)));

    private static bool StateIncluded(AssetState state, AssetBrowserFilter filter)
        => state switch
        {
            AssetState.Generated => filter.IncludeGenerated,
            AssetState.Source => filter.IncludeSource,
            AssetState.Descriptor => filter.IncludeDescriptor,
            AssetState.Cooked => filter.IncludeCooked,
            AssetState.Stale => filter.IncludeStale,
            AssetState.Missing => filter.IncludeMissing,
            AssetState.Broken => filter.IncludeBroken,
            _ => false,
        };

    private static bool MatchesSearch(ContentBrowserAssetItem item, string? searchText)
        => string.IsNullOrWhiteSpace(searchText)
               || item.DisplayName.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || item.Generated?.CanonicalName.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true
               || item.IdentityUri.ToString().Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || item.DisplayPath.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || (item.AssetGuid?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true);

    private static bool IsBrowsableRecord(Uri uri, ProjectContext project)
    {
        var path = AssetUriHelper.GetRelativePath(uri).Replace('\\', '/').TrimStart('/');
        if (IsImportSidecarPath(path))
        {
            return false;
        }

        var mountPoint = AssetUriHelper.GetMountPoint(uri);
        return !IsDerivedRootMount(mountPoint)
            && (!string.Equals(mountPoint, "project", StringComparison.OrdinalIgnoreCase)
            || (!path.StartsWith(".cooked/", StringComparison.OrdinalIgnoreCase)
            && !path.StartsWith(".imported/", StringComparison.OrdinalIgnoreCase)
            && !path.StartsWith(".build/", StringComparison.OrdinalIgnoreCase)
            && !project.AuthoringMounts.Any(mount =>
        {
            var mountRelativePath = mount.RelativePath.Replace('\\', '/').Trim('/');
            return path.Equals(mountRelativePath, StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith(mountRelativePath + "/", StringComparison.OrdinalIgnoreCase);
        })
            && !project.LocalFolderMounts.Any(mount =>
        {
            var relative = Path.GetRelativePath(project.ProjectRoot, mount.AbsolutePath).Replace('\\', '/');
            return relative is not "." and not ".." && !relative.StartsWith("../", StringComparison.Ordinal)
                && (path.Equals(relative, StringComparison.OrdinalIgnoreCase) || path.StartsWith(relative + "/", StringComparison.OrdinalIgnoreCase));
        })));
    }

    private static bool IsImportSidecarPath(string path)
        => path.EndsWith(".import.json", StringComparison.OrdinalIgnoreCase);

    private static bool IsDerivedRootMount(string mountPoint)
        => string.Equals(mountPoint, "Cooked", StringComparison.OrdinalIgnoreCase)
           || string.Equals(mountPoint, "Imported", StringComparison.OrdinalIgnoreCase)
           || string.Equals(mountPoint, "Build", StringComparison.OrdinalIgnoreCase);

    private static string GetLogicalKey(Uri uri)
    {
        var path = AssetUriHelper.GetVirtualPath(uri);
        if (string.IsNullOrWhiteSpace(path))
        {
            path = uri.AbsolutePath;
        }

        return path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? path[..^".json".Length]
            : path;
    }

    private static string? TryResolveSourcePath(ProjectContext project, Uri uri)
    {
        var relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/');
        var slash = relative.IndexOf('/', StringComparison.Ordinal);
        if (slash <= 0)
        {
            return null;
        }

        var mountName = relative[..slash];
        var mountRelativePath = relative[(slash + 1)..];
        var mount = project.AuthoringMounts.FirstOrDefault(m => string.Equals(m.Name, mountName, StringComparison.OrdinalIgnoreCase));
        return mount is null ? null : Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativePath));
    }

    private static string? TryResolveCookedPath(ProjectCookScope cookScope, Uri uri)
    {
        var relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
        return string.IsNullOrWhiteSpace(cookScope.CookedOutputRoot)
            ? null
            : Path.Combine(cookScope.CookedOutputRoot, relative);
    }

    private static string GetDisplayPath(AssetRecord selected, ProjectContext project)
    {
        var local = selected.Cooked is { } cooked ? project.LocalFolderMounts.FirstOrDefault(mount =>
            string.Equals(Path.GetFullPath(mount.AbsolutePath), Path.GetFullPath(cooked.RootFolderPath), StringComparison.OrdinalIgnoreCase)) : null;
        return local is not null ? "/" + local.Name + "/" + selected.Cooked!.DescriptorRelativePath : AssetUriHelper.GetVirtualPath(selected.Uri);
    }

    private static Uri ToCookedUri(Uri uri)
    {
        var path = uri.AbsolutePath;
        if (path.EndsWith(".json", StringComparison.OrdinalIgnoreCase))
        {
            path = path[..^".json".Length];
        }

        return new Uri($"{AssetUris.Scheme}://{path}");
    }

    private static bool IsDescriptorUri(Uri uri)
    {
        var path = AssetUriHelper.GetRelativePath(uri);
        return path.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".ogeo.json", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".otex.json", StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsCookedUri(Uri uri)
    {
        var path = AssetUriHelper.GetRelativePath(uri);
        return path.EndsWith(".omat", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".ogeo", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".oscene", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".otex", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".data", StringComparison.OrdinalIgnoreCase)
               || path.EndsWith(".table", StringComparison.OrdinalIgnoreCase);
    }

    private static AssetKind GetKind(Uri uri)
    {
        var path = AssetUriHelper.GetRelativePath(uri);
        var upper = path.ToUpperInvariant();
        if (upper.EndsWith(".OMAT.JSON", StringComparison.Ordinal) || upper.EndsWith(".OMAT", StringComparison.Ordinal))
        {
            return AssetKind.Material;
        }

        if (upper.EndsWith(".OGEO.JSON", StringComparison.Ordinal) || upper.EndsWith(".OGEO", StringComparison.Ordinal))
        {
            return AssetKind.Geometry;
        }

        if (upper.EndsWith(".OSCENE.JSON", StringComparison.Ordinal) || upper.EndsWith(".OSCENE", StringComparison.Ordinal))
        {
            return AssetKind.Scene;
        }

        if (upper.EndsWith(".OTEX.JSON", StringComparison.Ordinal) || upper.EndsWith(".OTEX", StringComparison.Ordinal))
        {
            return AssetKind.Texture;
        }

        var ext = Path.GetExtension(path).ToUpperInvariant();
        return ext switch
        {
            ".PNG" or ".JPG" or ".JPEG" or ".TGA" => AssetKind.Image,
            ".IMPORT" => AssetKind.ImportSettings,
            ".DATA" => AssetKind.CookedData,
            ".TABLE" => AssetKind.CookedTable,
            ".GLB" or ".GLTF" or ".FBX" => AssetKind.ForeignSource,
            _ => AssetKind.Unknown,
        };
    }

    private static AssetKind GetKind(AssetRecord record) => record.Cooked is { } cooked ? GetCookedKind(cooked.AssetType) : GetKind(record.Uri);

    private ContentBrowserAssetItem? CreateItem(
        List<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope)
    {
        var descriptor = records.FirstOrDefault(static record => record.Cooked is null && IsDescriptorUri(record.Uri));
        var cooked = records.FirstOrDefault(static record => record.Cooked is not null) ?? records.FirstOrDefault(static record => IsCookedUri(record.Uri));
        var source = records.FirstOrDefault(static record => record.Cooked is null && !IsDescriptorUri(record.Uri) && !IsCookedUri(record.Uri));
        var selected = descriptor ?? source ?? cooked;
        if (selected is null)
        {
            return null;
        }

        if (selected.Generated is { } recipe)
        {
            return CreateGeneratedItem(selected, recipe);
        }

        var sourcePath = source is null ? null : TryResolveSourcePath(project, source.Uri);
        var descriptorPath = descriptor is null ? null : TryResolveSourcePath(project, descriptor.Uri);
        var cookedUri = cooked?.Uri ?? (descriptor is null ? null : ToCookedUri(descriptor.Uri));
        var cookedPath = cooked?.Cooked is { } metadata ? TryResolveIndexedDescriptorPath(metadata)
            : cookedUri is null ? null : TryResolveCookedPath(cookScope, cookedUri);
        var diagnostics = new List<string>();

        var primaryState = descriptor is not null
            ? AssetState.Descriptor
            : source is not null
                ? AssetState.Source
                : AssetState.Cooked;
        AssetState? derivedState = null;

        if (descriptor is not null)
        {
            (primaryState, derivedState) = this.ReadDescriptorState(descriptorPath, cookedPath, diagnostics);
        }
        else if (cooked is not null && (cookedPath is null || !File.Exists(cookedPath)))
        {
            primaryState = AssetState.Broken;
            diagnostics.Add(cookedPath is null ? AssetIdentityDiagnosticCodes.DescriptorBroken : AssetIdentityDiagnosticCodes.CookedMissing);
        }

        return new ContentBrowserAssetItem(
            IdentityUri: descriptor?.Uri ?? selected.Uri,
            DisplayName: GetDisplayName(descriptor?.Uri ?? selected.Uri),
            Kind: GetKind(descriptor ?? cooked ?? selected),
            PrimaryState: primaryState,
            DerivedState: derivedState,
            RuntimeAvailability: cookedUri is null ? AssetRuntimeAvailability.NotApplicable : AssetRuntimeAvailability.NotMounted,
            DisplayPath: GetDisplayPath(descriptor ?? cooked ?? selected, project),
            SourcePath: sourcePath,
            DescriptorPath: descriptorPath,
            CookedUri: cookedUri,
            CookedPath: cookedPath,
            AssetGuid: null,
            DiagnosticCodes: diagnostics,
            IsSelectable: primaryState is not AssetState.Broken and not AssetState.Missing)
        {
            CookedMetadata = cooked?.Cooked,
            OverriddenCookedSources = cooked?.OverriddenCookedSources ?? [],
        };
    }

    private bool CanReadMaterialDescriptor(string descriptorPath)
    {
        if (!descriptorPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        byte[] bytes;
        try
        {
            bytes = File.ReadAllBytes(descriptorPath);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return false;
        }

        var contentHash = Convert.ToHexString(SHA256.HashData(bytes));
        lock (this.descriptorValidationSync)
        {
            if (this.descriptorValidationCache.TryGetValue(descriptorPath, out var cached)
                && string.Equals(cached.ContentHash, contentHash, StringComparison.Ordinal))
            {
                return cached.CanRead;
            }
        }

        bool canRead;
        try
        {
            _ = MaterialSourceReader.Read(bytes);
            canRead = true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
        {
            canRead = false;
        }

        lock (this.descriptorValidationSync)
        {
            this.descriptorValidationCache[descriptorPath] = new DescriptorValidationEntry(contentHash, canRead);
        }

        return canRead;
    }

    private (AssetState primary, AssetState? derived) ReadDescriptorState(string? descriptorPath, string? cookedPath, List<string> diagnostics)
    {
        if (descriptorPath is null || !File.Exists(descriptorPath) || !this.CanReadMaterialDescriptor(descriptorPath))
        {
            diagnostics.Add(AssetIdentityDiagnosticCodes.DescriptorBroken);
            return (AssetState.Broken, null);
        }

        if (cookedPath is null)
        {
            return (AssetState.Descriptor, null);
        }

        if (!File.Exists(cookedPath))
        {
            diagnostics.Add(AssetIdentityDiagnosticCodes.CookedMissing);
            return (AssetState.Descriptor, null);
        }

        // Presence alone does not establish freshness; the shared cook-status reader supplies that proof.
        return (AssetState.Descriptor, AssetState.Stale);
    }

    private sealed record DescriptorValidationEntry(string ContentHash, bool CanRead);
}
