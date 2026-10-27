// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Status;

/// <summary>Compares publication metadata without verifying cooked payload bytes.</summary>
public sealed partial class AssetCookStatusReader
{
    private static async Task<FreshnessSnapshot> ReadFreshnessAsync(
        CookPublicationReadLease publication,
        CookDependencyGraph graph,
        CookProvenance previous,
        CancellationToken cancellationToken)
    {
        var available = ImmutableHashSet.CreateBuilder<(string RootMount, string VirtualPath)>();
        var unknown = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        foreach (var root in previous.Roots)
        {
            if (publication.UnavailableRoots.Any(binding => string.Equals(binding.Name, root.Mount, StringComparison.Ordinal))
                || publication.FindProjectRoot(root.Mount)?.FullPath is not { } path)
            {
                continue;
            }

            try
            {
                // A root cooked in another index format has no readable output: it needs recooking.
                if (await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false) is not CookedIndexRead.Current { Snapshot: var current })
                {
                    continue;
                }

                if (root.SourceKey != current.Index.SourceGuid
                    || !string.Equals(root.IndexSha256, current.Fingerprint, StringComparison.OrdinalIgnoreCase))
                {
                    _ = unknown.Add(root.Mount);
                    continue;
                }

                // These are availability facts, not a second managed integrity validator.
                if (current.Index.Files.Any(file => !File.Exists(Path.Combine(path, file.RelativePath))))
                {
                    continue;
                }

                var members = current.Index.Assets.Select(static asset => asset.DescriptorRelativePath)
                    .Concat(current.Index.Files.Select(static file => file.RelativePath)).ToHashSet(StringComparer.Ordinal);
                foreach (var output in previous.Products.SelectMany(static product => product.Outputs).Where(output => string.Equals(output.RootMount, root.Mount, StringComparison.Ordinal)))
                {
                    if (output.Asset.DescriptorRelativePath is { } descriptor && members.Contains(descriptor) && File.Exists(Path.Combine(path, descriptor)))
                    {
                        _ = available.Add((root.Mount, output.Asset.VirtualPath));
                    }
                }
            }
            catch (Exception error) when (error is FileNotFoundException or DirectoryNotFoundException)
            {
                // An absent publication is affirmative missing output.
            }
            catch (Exception error) when (error is IOException or InvalidDataException or UnauthorizedAccessException or NotSupportedException)
            {
                _ = unknown.Add(root.Mount);
            }
        }

        var fingerprints = graph.Assets.ToDictionary(
            static input => input.AssetUri,
            input => CookIncrementalPlanner.Fingerprint(input, graph.Files, graph));

        var currentProducts = previous.Products.Where(product => fingerprints.TryGetValue(product.SourceUri, out var fingerprint)
            && string.Equals(fingerprint, product.ReuseFingerprint, StringComparison.Ordinal))
            .Select(static product => product.SourceUri).ToImmutableHashSet();
        return new(currentProducts, available.ToImmutable(), unknown.ToImmutable());
    }

    private static CookedOutputAvailability ReadOutputAvailability(
        AssetPath source,
        Dictionary<AssetPath, CookProvenance.Product> products,
        FreshnessSnapshot snapshot,
        HashSet<AssetPath> visited)
    {
        if (!visited.Add(source))
        {
            return CookedOutputAvailability.Present;
        }

        if (!products.TryGetValue(source, out var product))
        {
            return CookedOutputAvailability.Unknown;
        }

        if (product.Outputs.Any(output => snapshot.UnknownRoots.Contains(output.RootMount)))
        {
            return CookedOutputAvailability.Unknown;
        }

        if (product.Outputs.IsEmpty || product.Outputs.Any(output => !snapshot.AvailableOutputs.Contains((output.RootMount, output.Asset.VirtualPath))))
        {
            return CookedOutputAvailability.Missing;
        }

        var availability = CookedOutputAvailability.Present;
        foreach (var dependency in product.Dependencies.Where(uri => !product.CookedDependencies.Any(input => input.AssetUri == uri)))
        {
            var dependencyAvailability = ReadOutputAvailability(dependency, products, snapshot, visited);
            if (dependencyAvailability == CookedOutputAvailability.Missing)
            {
                return dependencyAvailability;
            }

            if (dependencyAvailability == CookedOutputAvailability.Unknown)
            {
                availability = dependencyAvailability;
            }
        }

        return availability;
    }

    private sealed record FreshnessSnapshot(
        ImmutableHashSet<AssetPath> CurrentProducts,
        ImmutableHashSet<(string RootMount, string VirtualPath)> AvailableOutputs,
        ImmutableHashSet<string> UnknownRoots);
}
