// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Identifies source-owned repair work before capturing inputs or inheriting cooked bytes.</summary>
internal sealed record CookRootRepair(ImmutableHashSet<string> EmptyRoots, ImmutableHashSet<Uri> Sources)
{
    public static CookRootRepair Create(ProjectContext project, CookProvenance previous,
        IReadOnlyCollection<string> mounts, ImmutableDictionary<string, CookedInventoryReport> inventories)
    {
        var empty = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var sources = ImmutableHashSet.CreateBuilder<Uri>();
        var diagnostics = new List<DiagnosticRecord>();
        foreach (var mount in mounts)
        {
            var path = ContentPipelinePaths.GetCookedMountRoot(project.ProjectRoot, mount);
            var prior = previous.Roots.FirstOrDefault(root => root.Mount == mount);
            var owners = previous.Products.Where(product => product.Outputs.Any(output => output.RootMount == mount)).ToArray();
            if (!inventories.TryGetValue(mount, out var inventory))
            {
                if (Directory.Exists(path) && Directory.EnumerateFileSystemEntries(path).Any())
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
                .Where(output => output.RootMount == mount && output.Asset.DescriptorRelativePath is not null)
                .Select(static output => output.Asset.DescriptorRelativePath!));
            var sharedDamage = inventory.Issues.Any(issue => !descriptors.Contains(issue.RelativePath)
                || issue.Reason != "digest_mismatch");
            var affected = sharedDamage ? descriptors
                : descriptors.Where(descriptor => inventory.Issues.Any(issue => issue.RelativePath == descriptor));
            foreach (var descriptor in affected)
            {
                var owner = owners.Where(product => product.Outputs.Any(output => output.RootMount == mount
                    && output.Asset.DescriptorRelativePath == descriptor)).ToArray();
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
                var associatedResources = owners.SelectMany(static product => product.AuxiliaryFiles)
                    .Concat(owners.SelectMany(static product => product.Outputs)
                        .Where(output => output.RootMount == mount && output.Asset.DescriptorRelativePath is not null)
                        .Select(static output => output.Asset.DescriptorRelativePath!)).ToHashSet(StringComparer.Ordinal);
                foreach (var file in inventory.Files.Where(file => file.Value.Kind == Oxygen.Managed.Assets.Persistence.LooseCooked.V2.FileKind.Auxiliary
                    && !associatedResources.Contains(file.Key)))
                {
                    diagnostics.Add(Failure(Path.Combine(path, file.Key), "Cannot rebuild this auxiliary file: its source owner is unknown."));
                }

                empty.Add(mount);
                sources.UnionWith(owners.Select(static product => product.SourceUri));
                if (owners.Length == 0 || inventory.Issues.Any(static issue => issue.Reason is "unexpected" or "linked_path" or "not_regular"))
                {
                    diagnostics.Add(Failure(path, "The root contains unowned or redirected files. Resolve those files before rebuilding."));
                }
            }
        }

        if (diagnostics.Count != 0)
        {
            throw new CookInputDiscoveryException(diagnostics);
        }

        return new(empty.ToImmutable(), sources.ToImmutable());
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
