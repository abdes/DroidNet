// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Import.Materials;
using Oxygen.Assets.Model;
using Oxygen.Core;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentBrowser.AssetIdentity;

/// <summary>
/// Default ED-M06 asset identity reducer.
/// </summary>
public sealed class AssetIdentityReducer : IAssetIdentityReducer
{
    private readonly Lock descriptorValidationSync = new();
    private readonly Dictionary<string, DescriptorValidationEntry> descriptorValidationCache = new(StringComparer.OrdinalIgnoreCase);

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
            .GroupBy(static record => GetLogicalKey(record.Uri), StringComparer.OrdinalIgnoreCase)
            .Select(group => this.CreateItem(group.ToList(), project, cookScope))
            .OfType<ContentBrowserAssetItem>()
            .Where(item => IsIncluded(item, filter))
            .Where(item => MatchesSearch(item, filter.SearchText))
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

    private ContentBrowserAssetItem? CreateItem(
        IReadOnlyList<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope)
    {
        if (records.Count == 0)
        {
            return null;
        }

        var descriptor = records.FirstOrDefault(static record => IsDescriptorUri(record.Uri));
        var cooked = records.FirstOrDefault(static record => IsCookedUri(record.Uri));
        var source = records.FirstOrDefault(static record => !IsDescriptorUri(record.Uri) && !IsCookedUri(record.Uri));
        var selected = descriptor ?? source ?? cooked;
        if (selected is null)
        {
            return null;
        }

        var sourcePath = source is null ? null : TryResolveSourcePath(project, source.Uri);
        var descriptorPath = descriptor is null ? null : TryResolveSourcePath(project, descriptor.Uri);
        var cookedUri = cooked?.Uri ?? (descriptor is null ? null : ToCookedUri(descriptor.Uri));
        var cookedPath = cookedUri is null ? null : TryResolveCookedPath(cookScope, cookedUri);
        var diagnostics = new List<string>();

        var primaryState = descriptor is not null
            ? AssetState.Descriptor
            : source is not null
                ? AssetState.Source
                : AssetState.Cooked;
        AssetState? derivedState = null;

        if (descriptor is not null)
        {
            if (descriptorPath is null || !File.Exists(descriptorPath))
            {
                primaryState = AssetState.Broken;
                diagnostics.Add(AssetIdentityDiagnosticCodes.DescriptorBroken);
            }
            else if (!this.CanReadMaterialDescriptor(descriptorPath))
            {
                primaryState = AssetState.Broken;
                diagnostics.Add(AssetIdentityDiagnosticCodes.DescriptorBroken);
            }
            else if (cookedUri is not null && cookedPath is not null)
            {
                if (!File.Exists(cookedPath))
                {
                    diagnostics.Add(AssetIdentityDiagnosticCodes.CookedMissing);
                }
                else
                {
                    derivedState = File.GetLastWriteTimeUtc(descriptorPath) > File.GetLastWriteTimeUtc(cookedPath)
                        ? AssetState.Stale
                        : AssetState.Cooked;
                }
            }
        }
        else if (cooked is not null && cookedPath is not null && !File.Exists(cookedPath))
        {
            primaryState = AssetState.Broken;
            diagnostics.Add(AssetIdentityDiagnosticCodes.CookedMissing);
        }

        return new ContentBrowserAssetItem(
            IdentityUri: descriptor?.Uri ?? selected.Uri,
            DisplayName: GetDisplayName(descriptor?.Uri ?? selected.Uri),
            Kind: GetKind(descriptor?.Uri ?? selected.Uri),
            PrimaryState: primaryState,
            DerivedState: derivedState,
            RuntimeAvailability: cookedUri is null ? AssetRuntimeAvailability.NotApplicable : AssetRuntimeAvailability.NotMounted,
            DisplayPath: AssetUriHelper.GetVirtualPath(descriptor?.Uri ?? selected.Uri),
            SourcePath: sourcePath,
            DescriptorPath: descriptorPath,
            CookedUri: cookedUri,
            CookedPath: cookedPath,
            AssetGuid: null,
            DiagnosticCodes: diagnostics,
            IsSelectable: primaryState is not AssetState.Broken and not AssetState.Missing);
    }

    private static bool IsIncluded(ContentBrowserAssetItem item, AssetBrowserFilter filter)
    {
        if (filter.Kinds.Count > 0 && !filter.Kinds.Contains(item.Kind))
        {
            return false;
        }

        return StateIncluded(item.PrimaryState, filter)
               || (item.DerivedState is { } derivedState && StateIncluded(derivedState, filter));
    }

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
    {
        if (string.IsNullOrWhiteSpace(searchText))
        {
            return true;
        }

        return item.DisplayName.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || item.IdentityUri.ToString().Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || item.DisplayPath.Contains(searchText, StringComparison.OrdinalIgnoreCase)
               || (item.AssetGuid?.Contains(searchText, StringComparison.OrdinalIgnoreCase) == true);
    }

    private static bool IsBrowsableRecord(Uri uri, ProjectContext project)
    {
        var path = AssetUriHelper.GetRelativePath(uri).Replace('\\', '/').TrimStart('/');
        if (IsImportSidecarPath(path))
        {
            return false;
        }

        var mountPoint = AssetUriHelper.GetMountPoint(uri);
        if (!string.Equals(mountPoint, "project", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        if (path.StartsWith(".cooked/", StringComparison.OrdinalIgnoreCase)
            || path.StartsWith(".imported/", StringComparison.OrdinalIgnoreCase)
            || path.StartsWith(".build/", StringComparison.OrdinalIgnoreCase))
        {
            return false;
        }

        return !project.AuthoringMounts.Any(mount =>
        {
            var mountRelativePath = mount.RelativePath.Replace('\\', '/').Trim('/');
            return path.Equals(mountRelativePath, StringComparison.OrdinalIgnoreCase)
                   || path.StartsWith(mountRelativePath + "/", StringComparison.OrdinalIgnoreCase);
        });
    }

    private static bool IsImportSidecarPath(string path)
        => path.EndsWith(".import.json", StringComparison.OrdinalIgnoreCase);

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

    private bool CanReadMaterialDescriptor(string descriptorPath)
    {
        if (!descriptorPath.EndsWith(".omat.json", StringComparison.OrdinalIgnoreCase))
        {
            return true;
        }

        var lastWriteUtc = File.GetLastWriteTimeUtc(descriptorPath);
        lock (this.descriptorValidationSync)
        {
            if (this.descriptorValidationCache.TryGetValue(descriptorPath, out var cached)
                && cached.LastWriteUtc == lastWriteUtc)
            {
                return cached.CanRead;
            }
        }

        var canRead = false;
        try
        {
            _ = MaterialSourceReader.Read(File.ReadAllBytes(descriptorPath));
            canRead = true;
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or InvalidDataException or JsonException)
        {
            canRead = false;
        }

        lock (this.descriptorValidationSync)
        {
            this.descriptorValidationCache[descriptorPath] = new DescriptorValidationEntry(lastWriteUtc, canRead);
        }

        return canRead;
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
        if (mount is null)
        {
            return null;
        }

        return Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath, mountRelativePath));
    }

    private static string? TryResolveCookedPath(ProjectCookScope cookScope, Uri uri)
    {
        var relative = Uri.UnescapeDataString(uri.AbsolutePath).TrimStart('/').Replace('/', Path.DirectorySeparatorChar);
        return string.IsNullOrWhiteSpace(cookScope.CookedOutputRoot)
            ? null
            : Path.Combine(cookScope.CookedOutputRoot, relative);
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

    private static string GetDisplayName(Uri uri)
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

    private sealed record DescriptorValidationEntry(DateTime LastWriteUtc, bool CanRead);
}
