// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
/// Reduces catalog records into one entry per logical asset, applying the identity rules once.
/// </summary>
internal sealed class AssetIdentityReducer
{
    private readonly Lock descriptorValidationSync = new();
    private readonly Dictionary<string, DescriptorValidationEntry> descriptorValidationCache = [with(StringComparer.OrdinalIgnoreCase)];

    /// <summary>Reduces catalog records into one row per logical asset.</summary>
    /// <param name="records">The catalog records.</param>
    /// <param name="project">The owning project.</param>
    /// <param name="cookScope">The project's cook output scope.</param>
    /// <param name="includeMissing">Whether rows whose source is missing are kept.</param>
    /// <returns>The rows, ordered by display path.</returns>
    public IReadOnlyList<AssetEntry> Reduce(
        IReadOnlyList<AssetRecord> records,
        ProjectContext project,
        ProjectCookScope cookScope,
        bool includeMissing = false)
    {
        ArgumentNullException.ThrowIfNull(records);
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(cookScope);

        return records
            .Where(static record => IsBrowsableRecord(record.Uri))
            .GroupBy(static record => record.Cooked is null ? GetLogicalKey(record.Uri) : AssetUriHelper.GetVirtualPath(record.Uri), StringComparer.OrdinalIgnoreCase)
            .Select(group => this.CreateItem(group.ToList(), project))
            .OfType<AssetEntry>()
            .Where(item => IsIncluded(item, includeMissing))
            .OrderBy(static item => item.DisplayPath, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

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

    /// <summary>Formats an asset name without descriptor suffixes.</summary>
    /// <param name="uri">The source or cooked identity.</param>
    /// <returns>The readable asset name.</returns>
    internal static string GetDisplayName(Uri uri)
    {
        var name = Path.GetFileName(AssetUriHelper.GetVirtualPath(uri));
        if (string.IsNullOrWhiteSpace(name))
        {
            return uri.ToString();
        }

        return AssetFileTypes.Find(name) is { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked or AssetFileCategory.Script or AssetFileCategory.Input }
            ? AssetFileTypes.RemoveSuffix(name)
            : name;
    }

    private static AssetEntry CreateGeneratedItem(AssetRecord selected, GeneratedAssetMetadata recipe)
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

    private static bool IsIncluded(AssetEntry item, bool includeMissing)
        => StateIncluded(item.PrimaryState, includeMissing) || (item.DerivedState is { } derivedState && StateIncluded(derivedState, includeMissing));

    private static bool StateIncluded(AssetState state, bool includeMissing)
        => state switch
        {
            AssetState.Generated or AssetState.Source or AssetState.Descriptor or AssetState.Cooked or AssetState.Stale or AssetState.Broken => true,
            AssetState.Missing => includeMissing,
            _ => false,
        };

    private static bool IsBrowsableRecord(Uri uri)
    {
        var path = AssetUriHelper.GetRelativePath(uri).Replace('\\', '/').TrimStart('/');
        if (IsImportSidecarPath(path))
        {
            return false;
        }

        return !IsDerivedRootMount(AssetUriHelper.GetMountPoint(uri));
    }

    private static bool IsImportSidecarPath(string path)
        => AssetFileTypes.Is(path, AssetFileCategory.ImportSettings);

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

        return AssetFileTypes.Is(path, AssetFileCategory.Script)
            ? AssetFileTypes.ToCookedPath(path)!
            : path.EndsWith(AssetFileTypes.DescriptorSuffix, StringComparison.OrdinalIgnoreCase)
                ? path[..^AssetFileTypes.DescriptorSuffix.Length]
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

    private static string GetDisplayPath(AssetRecord selected, ProjectContext project)
    {
        var local = selected.Cooked is { } cooked ? project.LocalFolderMounts.FirstOrDefault(mount =>
            string.Equals(Path.GetFullPath(mount.AbsolutePath), Path.GetFullPath(cooked.RootFolderPath), StringComparison.OrdinalIgnoreCase)) : null;
        return local is not null ? "/" + local.Name + "/" + selected.Cooked!.DescriptorRelativePath : AssetUriHelper.GetVirtualPath(selected.Uri);
    }

    private static Uri ToCookedUri(Uri uri)
    {
        var path = uri.AbsolutePath;
        if (path.EndsWith(AssetFileTypes.DescriptorSuffix, StringComparison.OrdinalIgnoreCase))
        {
            path = path[..^AssetFileTypes.DescriptorSuffix.Length];
        }

        return new Uri($"{AssetUris.Scheme}://{path}");
    }

    private static bool IsDescriptorUri(Uri uri)
        => AssetFileTypes.Is(AssetUriHelper.GetRelativePath(uri), AssetFileCategory.Descriptor);

    private static bool IsCookedUri(Uri uri)
        => AssetFileTypes.Find(AssetUriHelper.GetRelativePath(uri)) is { Category: AssetFileCategory.Cooked or AssetFileCategory.CookedResource };

    private static AssetKind GetKind(Uri uri) => AssetKinds.FromFileName(AssetUriHelper.GetRelativePath(uri));

    private static AssetKind GetKind(AssetRecord record) => record.Cooked is { } cooked ? AssetKinds.FromCookedType(cooked.AssetType) : GetKind(record.Uri);

    private AssetEntry? CreateItem(List<AssetRecord> records, ProjectContext project)
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
        var scriptUri = source is not null && AssetFileTypes.Is(source.Uri.AbsolutePath, AssetFileCategory.Script)
            ? new Uri($"{AssetUris.Scheme}://{AssetFileTypes.ToCookedPath(AssetUriHelper.GetVirtualPath(source.Uri))}")
            : null;
        var cookedUri = cooked?.Uri ?? scriptUri ?? (descriptor is null ? null : ToCookedUri(descriptor.Uri));
        var cookedPath = cooked?.Cooked is { } metadata ? TryResolveIndexedDescriptorPath(metadata) : null;
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

        return new AssetEntry(
            IdentityUri: descriptor?.Uri ?? scriptUri ?? selected.Uri,
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
        if (!descriptorPath.EndsWith(AssetFileTypes.Material + AssetFileTypes.DescriptorSuffix, StringComparison.OrdinalIgnoreCase))
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
