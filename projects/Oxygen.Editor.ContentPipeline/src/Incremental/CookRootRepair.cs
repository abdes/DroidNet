// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Identifies source-owned repair work before capturing inputs or inheriting cooked bytes.</summary>
/// <param name="EmptyRoots">The roots reconstructed from nothing, because their cooked content is missing or damaged.</param>
/// <param name="Sources">The sources cooked again: products of reconstructed roots and the users of removed products.</param>
internal sealed record CookRootRepair(ImmutableHashSet<string> EmptyRoots, ImmutableHashSet<AssetPath> Sources)
{
    /// <summary>Gets the sources whose prior products leave the cooked content: moved, renamed or deleted sources.</summary>
    public ImmutableHashSet<AssetPath> RemovedSources { get; init; } = [];

    /// <summary>Gets the outputs removed from each inherited root, by mount.</summary>
    public ImmutableDictionary<string, CookedOutputRemoval> Removals { get; init; } = ImmutableDictionary<string, CookedOutputRemoval>.Empty.WithComparers(StringComparer.Ordinal);

    /// <summary>Gets the mounts whose every product leaves. With nothing cooked into them, they leave the publication.</summary>
    public ImmutableHashSet<string> VacatedRoots { get; init; } = ImmutableHashSet<string>.Empty.WithComparer(StringComparer.Ordinal);

    /// <summary>Selects the roots to reconstruct, the outputs to remove and the sources to cook again.</summary>
    /// <param name="publication">The publication lease that resolves project roots.</param>
    /// <param name="previous">The provenance of the previous cook.</param>
    /// <param name="mounts">The mounts being cooked.</param>
    /// <param name="inventories">The cooked inventory of each mount.</param>
    /// <returns>The repair work.</returns>
    public static CookRootRepair Create(
        CookPublicationReadLease publication,
        CookProvenance previous,
        IReadOnlyCollection<string> mounts,
        ImmutableDictionary<string, CookedInventoryReport> inventories)
    {
        var empty = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var sources = ImmutableHashSet.CreateBuilder<AssetPath>();
        var removals = ImmutableDictionary.CreateBuilder<string, CookedOutputRemoval>(StringComparer.Ordinal);
        var vacated = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var diagnostics = new List<DiagnosticRecord>();

        // A moved, renamed or deleted source retires its product, and its prior outputs leave their roots.
        var retired = previous.Products.Where(product => IsRetired(publication.ProjectRoot.FullPath, product)).ToHashSet();
        sources.UnionWith(FindUsers(previous, retired));
        foreach (var mount in mounts)
        {
            var path = publication.FindProjectRoot(mount)?.FullPath;
            var prior = previous.Roots.FirstOrDefault(root => string.Equals(root.Mount, mount, StringComparison.Ordinal));
            var owners = previous.Products.Where(product => product.Outputs.Any(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal))).ToArray();
            var kept = owners.Where(product => !retired.Contains(product)).ToArray();
            if (!inventories.TryGetValue(mount, out var inventory))
            {
                RepairMissingInventory(mount, path, prior, kept, empty, sources, diagnostics);
                continue;
            }

            if (path is null)
            {
                throw new InvalidDataException("An inventory has no selected generation.");
            }

            if (!inventory.IsValid && RepairDamagedRoot(mount, path, prior, owners, retired, inventory, empty, sources, diagnostics))
            {
                continue;
            }

            var leaving = owners.Where(retired.Contains).ToArray();
            if (leaving.Length != 0)
            {
                RemoveOutputs(mount, path, owners, leaving, inventory, removals, vacated, diagnostics);
            }
        }

        if (diagnostics.Count != 0)
        {
            throw new CookInputDiscoveryException(diagnostics);
        }

        return new(empty.ToImmutable(), sources.ToImmutable())
        {
            RemovedSources = [.. retired.Select(static product => product.SourceUri)],
            Removals = removals.ToImmutable(),
            VacatedRoots = vacated.ToImmutable(),
        };
    }

    /// <summary>Tests whether a prior product's authored source was moved or deleted.</summary>
    /// <remarks>
    /// A model is retired only when no import record names it any more; a model file missing while its
    /// record remains is damage that cooking reports instead.
    /// </remarks>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="product">The prior product.</param>
    /// <returns>Whether the product must leave the cooked index.</returns>
    public static bool IsRetired(string projectRoot, CookProvenance.Product product)
    {
        ArgumentNullException.ThrowIfNull(product);
        if (product.SourceInput is not { } source)
        {
            return false;
        }

        var fileSystem = new RealFileSystem();
        return !ExactSourcePath.Exists(fileSystem, projectRoot, source.SourceRelativePath)
            && (product.ImportedSource is null
                || !new Models.ModelImportRecords(fileSystem, projectRoot).IsImported(Path.Combine(projectRoot, source.SourceRelativePath)));
    }

    /// <summary>Finds the remaining products that use a removed product, so that cooking them again reports what they lost.</summary>
    private static IEnumerable<AssetPath> FindUsers(CookProvenance previous, HashSet<CookProvenance.Product> removed)
    {
        var identities = removed.Select(static product => product.SourceUri)
            .Concat(removed.SelectMany(static product => product.Outputs).SelectMany(static output => new[] { output.Asset.SourceAssetUri, output.Asset.CookedAssetUri }))
            .ToHashSet();
        return previous.Products.Where(product => !removed.Contains(product) && product.Dependencies.Any(identities.Contains))
            .Select(static product => product.SourceUri);
    }

    private static void RemoveOutputs(
        string mount,
        string path,
        CookProvenance.Product[] owners,
        CookProvenance.Product[] leaving,
        CookedInventoryReport inventory,
        ImmutableDictionary<string, CookedOutputRemoval>.Builder removals,
        ImmutableHashSet<string>.Builder vacated,
        List<DiagnosticRecord> diagnostics)
    {
        var remaining = owners.Except(leaving).ToArray();
        if (remaining.Length == 0)
        {
            _ = vacated.Add(mount);
            var owned = owners.SelectMany(static product => product.Outputs).Select(static output => output.Asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
            if (inventory.Assets.Any(asset => !owned.Contains(asset.VirtualPath)))
            {
                diagnostics.Add(Failure(path, "Cannot retire moved or deleted assets: the root contains assets without source ownership."));
            }
        }

        var listed = inventory.Assets.Select(static asset => asset.VirtualPath).ToHashSet(StringComparer.Ordinal);
        var kept = remaining.SelectMany(static product => product.AuxiliaryFiles).ToHashSet(StringComparer.Ordinal);
        var removal = new CookedOutputRemoval(
            [.. leaving.SelectMany(static product => product.Outputs)
                .Where(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal) && listed.Contains(output.Asset.VirtualPath))
                .Select(static output => output.Asset.VirtualPath).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal)],
            [.. leaving.SelectMany(static product => product.AuxiliaryFiles)
                .Where(file => !kept.Contains(file) && inventory.Files.TryGetValue(file, out var member) && member.Kind?.IsPathKeyed() == true)
                .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal)]);
        if (!removal.IsEmpty)
        {
            removals[mount] = removal;
        }
    }

    private static void RepairMissingInventory(
        string mount,
        string? path,
        CookProvenance.Root? prior,
        CookProvenance.Product[] owners,
        ImmutableHashSet<string>.Builder empty,
        ImmutableHashSet<AssetPath>.Builder sources,
        List<DiagnosticRecord> diagnostics)
    {
        if (prior is null && path is not null && Directory.Exists(path) && Directory.EnumerateFileSystemEntries(path).Any())
        {
            diagnostics.Add(Failure(path, "The cooked index cannot establish ownership. Restore it before rebuilding this root."));
        }
        else if (prior is not null)
        {
            empty.Add(mount);
            sources.UnionWith(owners.Select(static product => product.SourceUri));
        }
    }

    /// <summary>Selects the damaged descriptors to cook again, or the whole root when shared files are damaged.</summary>
    /// <returns>Whether the root is reconstructed from nothing.</returns>
    private static bool RepairDamagedRoot(
        string mount,
        string path,
        CookProvenance.Root? prior,
        CookProvenance.Product[] owners,
        HashSet<CookProvenance.Product> retired,
        CookedInventoryReport inventory,
        ImmutableHashSet<string>.Builder empty,
        ImmutableHashSet<AssetPath>.Builder sources,
        List<DiagnosticRecord> diagnostics)
    {
        if (prior is null || !prior.Matches(inventory))
        {
            diagnostics.Add(Failure(path, "Damaged cooked content has no matching source ownership record."));
            return true;
        }

        var descriptors = inventory.Assets.Select(static asset => asset.DescriptorPath).ToHashSet(StringComparer.Ordinal);
        descriptors.UnionWith(owners.SelectMany(static product => product.Outputs)
            .Where(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal) && output.Asset.DescriptorRelativePath is not null)
            .Select(static output => output.Asset.DescriptorRelativePath!));
        var sharedDamage = inventory.Issues.Any(issue => !descriptors.Contains(issue.RelativePath)
            || !string.Equals(issue.Reason, "digest_mismatch", StringComparison.Ordinal));
        var affected = sharedDamage ? descriptors
            : descriptors.Where(descriptor => inventory.Issues.Any(issue => string.Equals(issue.RelativePath, descriptor, StringComparison.Ordinal)));
        foreach (var descriptor in affected)
        {
            var owner = owners.Where(product => product.Outputs.Any(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal)
                && string.Equals(output.Asset.DescriptorRelativePath, descriptor, StringComparison.Ordinal))).ToArray();
            if (owner.Length != 1)
            {
                diagnostics.Add(Failure(Path.Combine(path, descriptor), $"Cannot rebuild '{descriptor}': its source owner is unknown."));
            }
            else if (!retired.Contains(owner[0]))
            {
                sources.Add(owner[0].SourceUri);
            }
        }

        if (!sharedDamage)
        {
            return false;
        }

        var kept = owners.Where(product => !retired.Contains(product)).ToArray();
        VerifyRebuildOwnership(path, mount, owners, inventory, diagnostics);
        empty.Add(mount);
        sources.UnionWith(kept.Select(static product => product.SourceUri));
        if (owners.Length == 0)
        {
            diagnostics.Add(Failure(path, "The root has no source ownership record to rebuild from."));
        }

        return true;
    }

    private static void VerifyRebuildOwnership(string path, string mount, IReadOnlyList<CookProvenance.Product> owners, CookedInventoryReport inventory, List<DiagnosticRecord> diagnostics)
    {
        var associatedResources = owners.SelectMany(static product => product.AuxiliaryFiles)
            .Concat(owners.SelectMany(static product => product.Outputs)
                .Where(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal) && output.Asset.DescriptorRelativePath is not null)
                .Select(static output => output.Asset.DescriptorRelativePath!)).ToHashSet(StringComparer.Ordinal);
        foreach (var file in inventory.Files.Where(file => file.Value.Kind?.IsPathKeyed() == true
            && !associatedResources.Contains(file.Key)))
        {
            diagnostics.Add(Failure(Path.Combine(path, file.Key), "Cannot rebuild this auxiliary file: its source owner is unknown."));
        }

        foreach (var issue in inventory.Issues.Where(static issue => issue.Reason is "unexpected" or "linked_path" or "not_regular"))
        {
            diagnostics.Add(Failure(Path.Combine(path, issue.RelativePath), "This file has no safe source reconstruction. Resolve it before rebuilding the root."));
        }
    }

    private static DiagnosticRecord Failure(string path, string message) => new()
    {
        OperationId = Guid.Empty,
        Domain = FailureDomain.AssetCook,
        Severity = DiagnosticSeverity.Error,
        Code = "asset_cook.repair_source_unknown",
        Message = message,
        AffectedPath = path,
    };
}
