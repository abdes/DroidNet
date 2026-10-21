// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using System.Security.Cryptography;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Assets;

/// <summary>
/// Reduces catalog records into one entry per logical asset, applying the identity rules once.
/// </summary>
internal sealed class AssetIdentityReducer
{
    private readonly Lock descriptorValidationSync = new();
    private readonly Dictionary<FileSystemPath, DescriptorValidationEntry> descriptorValidationCache = [];

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

        return [.. records
            .Where(static record => IsBrowsableRecord(record.Identity))
            .GroupBy(static record => record.Cooked is null ? AssetFileTypes.ToIdentity(record.Identity) : record.Identity)
            .Select(group => this.CreateItem(group.Key, [.. group], project))
            .OfType<AssetEntry>()
            .Where(item => IsIncluded(item, includeMissing))
            .OrderBy(static item => item.DisplayPath, StringComparer.OrdinalIgnoreCase)];
    }

    /// <summary>Resolves a descriptor path only when it stays inside its indexed root.</summary>
    /// <param name="metadata">The indexed physical source.</param>
    /// <returns>The contained path, or null for an invalid descriptor path.</returns>
    internal static FileSystemPath? TryResolveIndexedDescriptorPath(CookedAssetMetadata metadata)
    {
        try
        {
            if (metadata.DescriptorRelativePath is not { } descriptor)
            {
                return null;
            }

            var root = metadata.ContainerPath;
            var path = root.Combine(descriptor);
            return !Path.IsPathRooted(descriptor) && path != root && path.IsSameOrInside(root) ? path : null;
        }
        catch (Exception exception) when (exception is ArgumentException or NotSupportedException or PathTooLongException)
        {
            return null;
        }
    }

    /// <summary>Formats an asset name without descriptor suffixes.</summary>
    /// <param name="path">The source or cooked path.</param>
    /// <returns>The readable asset name.</returns>
    internal static string GetDisplayName(AssetPath path)
    {
        var name = path.Name;
        return AssetFileTypes.Find(name) is { Category: AssetFileCategory.Descriptor or AssetFileCategory.Cooked or AssetFileCategory.Script or AssetFileCategory.Input }
            ? AssetFileTypes.RemoveSuffix(name)
            : name;
    }

    // A pak holds its descriptors inside the file: the asset is read-only and has nothing to cook.
    private static AssetEntry CreatePackagedItem(AssetPath identity, AssetRecord selected)
        => new(
            Identity: identity,
            DisplayName: GetDisplayName(identity),
            Kind: AssetKinds.FromCooked(selected.Cooked!, identity),
            PrimaryState: AssetState.Packaged,
            DerivedState: null,
            RuntimeAvailability: AssetRuntimeAvailability.Unknown,
            DisplayPath: identity.VirtualPath,
            SourcePath: null,
            DescriptorPath: null,
            CookedPath: null,
            AssetGuid: null,
            DiagnosticCodes: [],
            IsSelectable: true)
        {
            CookedMetadata = selected.Cooked,
            OverriddenCookedSources = selected.OverriddenCookedSources,
        };

    private static bool IsIncluded(AssetEntry item, bool includeMissing)
        => StateIncluded(item.PrimaryState, includeMissing) || (item.DerivedState is { } derivedState && StateIncluded(derivedState, includeMissing));

    private static bool StateIncluded(AssetState state, bool includeMissing)
        => state switch
        {
            AssetState.Packaged or AssetState.Source or AssetState.Descriptor or AssetState.Cooked or AssetState.Stale or AssetState.Broken => true,
            AssetState.Missing => includeMissing,
            _ => false,
        };

    private static bool IsBrowsableRecord(AssetPath path)
        => !AssetFileTypes.Is(path.Name, AssetFileCategory.ImportSettings) && !IsDerivedRootMount(path.Mount);

    private static bool IsDerivedRootMount(string mountPoint)
        => string.Equals(mountPoint, "Cooked", StringComparison.Ordinal)
           || string.Equals(mountPoint, "Imported", StringComparison.Ordinal)
           || string.Equals(mountPoint, "Build", StringComparison.Ordinal);

    // Only the files of authoring mounts are project sources.
    private static FileSystemPath? TryResolveSourcePath(ProjectContext project, AssetPath path)
        => project.Find(path) is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location ? location.File : null;

    private static string GetDisplayPath(AssetRecord selected, ProjectContext project)
    {
        var local = selected.Cooked is { DescriptorRelativePath: not null } cooked ? project.LocalFolderMounts.FirstOrDefault(mount => mount.Root == cooked.ContainerPath) : null;
        return local is not null ? "/" + local.Name + "/" + selected.Cooked!.DescriptorRelativePath : selected.Identity.VirtualPath;
    }

    private static bool IsDescriptor(AssetRecord record)
        => record.Cooked is null && AssetFileTypes.Is(record.Identity.Name, AssetFileCategory.Descriptor);

    private static bool IsCooked(AssetRecord record)
        => AssetFileTypes.Find(record.Identity.Name) is { Category: AssetFileCategory.Cooked or AssetFileCategory.CookedResource };

    private static AssetKind GetKind(AssetRecord record)
        => record.Cooked is { } cooked ? AssetKinds.FromCooked(cooked, record.Identity) : AssetKinds.FromFileName(record.Identity.Name);

    private AssetEntry? CreateItem(AssetPath identity, List<AssetRecord> records, ProjectContext project)
    {
        var descriptor = records.FirstOrDefault(IsDescriptor);
        var cooked = records.FirstOrDefault(static record => record.Cooked is not null) ?? records.FirstOrDefault(IsCooked);
        var source = records.FirstOrDefault(static record => record.Cooked is null && !IsDescriptor(record) && !IsCooked(record));
        var selected = descriptor ?? source ?? cooked;
        if (selected is null)
        {
            return null;
        }

        if (selected == cooked && cooked.Cooked is { IsPackaged: true })
        {
            return CreatePackagedItem(identity, cooked);
        }

        var sourcePath = source is null ? null : TryResolveSourcePath(project, source.Identity);
        var descriptorPath = descriptor is null ? null : TryResolveSourcePath(project, descriptor.Identity);
        var cooks = cooked is not null || descriptor is not null || (source is not null && AssetFileTypes.Is(source.Identity.Name, AssetFileCategory.Script));
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
            (primaryState, derivedState) = this.ReadDescriptorState(project.FileSystem, descriptorPath, cookedPath, diagnostics);
        }
        else if (cooked is not null && (cookedPath is null || !project.FileSystem.File.Exists(cookedPath.FullPath)))
        {
            primaryState = AssetState.Broken;
            diagnostics.Add(cookedPath is null ? AssetIdentityDiagnosticCodes.DescriptorBroken : AssetIdentityDiagnosticCodes.CookedMissing);
        }

        return new AssetEntry(
            Identity: identity,
            DisplayName: GetDisplayName((descriptor ?? selected).Identity),
            Kind: GetKind(descriptor ?? cooked ?? selected),
            PrimaryState: primaryState,
            DerivedState: derivedState,
            RuntimeAvailability: cooks ? AssetRuntimeAvailability.NotMounted : AssetRuntimeAvailability.NotApplicable,
            DisplayPath: GetDisplayPath(descriptor ?? cooked ?? selected, project),
            SourcePath: sourcePath,
            DescriptorPath: descriptorPath,
            CookedPath: cookedPath,
            AssetGuid: null,
            DiagnosticCodes: diagnostics,
            IsSelectable: primaryState is not AssetState.Broken and not AssetState.Missing)
        {
            CookedMetadata = cooked?.Cooked,
            OverriddenCookedSources = cooked?.OverriddenCookedSources ?? [],
        };
    }

    private bool CanReadMaterialDescriptor(IFileSystem fileSystem, FileSystemPath descriptorPath)
    {
        if (!descriptorPath.Name.EndsWith(AssetFileTypes.Material + AssetFileTypes.DescriptorSuffix, StringComparison.Ordinal))
        {
            return true;
        }

        byte[] bytes;
        try
        {
            bytes = fileSystem.File.ReadAllBytes(descriptorPath.FullPath);
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
        catch (AssetFormatException)
        {
            canRead = false;
        }

        lock (this.descriptorValidationSync)
        {
            this.descriptorValidationCache[descriptorPath] = new DescriptorValidationEntry(contentHash, canRead);
        }

        return canRead;
    }

    private (AssetState Primary, AssetState? Derived) ReadDescriptorState(IFileSystem fileSystem, FileSystemPath? descriptorPath, FileSystemPath? cookedPath, List<string> diagnostics)
    {
        if (descriptorPath is null || !fileSystem.File.Exists(descriptorPath.FullPath) || !this.CanReadMaterialDescriptor(fileSystem, descriptorPath))
        {
            diagnostics.Add(AssetIdentityDiagnosticCodes.DescriptorBroken);
            return (AssetState.Broken, null);
        }

        if (cookedPath is null)
        {
            return (AssetState.Descriptor, null);
        }

        if (!fileSystem.File.Exists(cookedPath.FullPath))
        {
            diagnostics.Add(AssetIdentityDiagnosticCodes.CookedMissing);
            return (AssetState.Descriptor, null);
        }

        // Presence alone does not establish freshness; the shared cook-status reader supplies that proof.
        return (AssetState.Descriptor, AssetState.Stale);
    }

    private sealed record DescriptorValidationEntry(string ContentHash, bool CanRead);
}
