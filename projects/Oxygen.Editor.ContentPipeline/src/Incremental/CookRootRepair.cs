// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Identifies source-owned repair work before capturing inputs or inheriting cooked bytes.</summary>
internal sealed record CookRootRepair(ImmutableHashSet<string> EmptyRoots, ImmutableHashSet<Uri> Sources)
{
    public static CookRootRepair Create(CookPublicationReadLease publication, CookProvenance previous,
        IReadOnlyCollection<string> mounts, ImmutableDictionary<string, CookedInventoryReport> inventories)
    {
        var empty = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var sources = ImmutableHashSet.CreateBuilder<Uri>();
        var diagnostics = new List<DiagnosticRecord>();
        foreach (var mount in mounts)
        {
            var path = publication.FindProjectRoot(mount);
            var prior = previous.Roots.FirstOrDefault(root => string.Equals(root.Mount, mount, StringComparison.Ordinal));
            var owners = previous.Products.Where(product => product.Outputs.Any(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal))).ToArray();
            var retiredScenes = owners.Where(product => product.SourceInput is { Kind: ContentCookAssetKind.Scene } source
                && !CookSavedSourceReader.Exists(Path.Combine(publication.ProjectRoot, source.SourceRelativePath))).ToArray();
            if (retiredScenes.Length != 0)
            {
                // Native indices are append/update inventories. Reconstruct an owned root rather
                // than deleting descriptors behind an index or inheriting renamed products.
                empty.Add(mount);
                sources.UnionWith(owners.Except(retiredScenes).Select(static product => product.SourceUri));
                if (inventories.TryGetValue(mount, out var selected)
                    && path is not null)
                {
                    if (selected.Assets.Any(asset => !owners.SelectMany(static product => product.Outputs)
                        .Any(output => string.Equals(output.Asset.VirtualPath, asset.VirtualPath, StringComparison.Ordinal))))
                    {
                        diagnostics.Add(Failure(path, "Cannot retire renamed scenes: the root contains assets without source ownership."));
                    }

                    VerifyRebuildOwnership(path, mount, owners, selected, diagnostics);
                }

                continue;
            }

            if (!inventories.TryGetValue(mount, out var inventory))
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

                continue;
            }

            if (path is null)
            {
                throw new InvalidDataException("An inventory has no selected generation.");
            }

            if (inventory.IsValid)
            {
                continue;
            }

            if (prior is null || !prior.Matches(inventory))
            {
                diagnostics.Add(Failure(path, "Damaged cooked content has no matching source ownership record."));
                continue;
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
                else
                {
                    sources.Add(owner[0].SourceUri);
                }
            }

            if (sharedDamage)
            {
                VerifyRebuildOwnership(path, mount, owners, inventory, diagnostics);
                empty.Add(mount);
                sources.UnionWith(owners.Select(static product => product.SourceUri));
                if (owners.Length == 0)
                {
                    diagnostics.Add(Failure(path, "The root has no source ownership record to rebuild from."));
                }
            }
        }

        if (diagnostics.Count != 0)
        {
            throw new CookInputDiscoveryException(diagnostics);
        }

        return new(empty.ToImmutable(), sources.ToImmutable());
    }

    private static void VerifyRebuildOwnership(string path, string mount, IReadOnlyList<CookProvenance.Product> owners, CookedInventoryReport inventory, List<DiagnosticRecord> diagnostics)
    {
        var associatedResources = owners.SelectMany(static product => product.AuxiliaryFiles)
            .Concat(owners.SelectMany(static product => product.Outputs)
                .Where(output => string.Equals(output.RootMount, mount, StringComparison.Ordinal) && output.Asset.DescriptorRelativePath is not null)
                .Select(static output => output.Asset.DescriptorRelativePath!)).ToHashSet(StringComparer.Ordinal);
        foreach (var file in inventory.Files.Where(file => file.Value.Kind == Oxygen.Managed.Assets.Persistence.LooseCooked.V3.FileKind.Auxiliary
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
