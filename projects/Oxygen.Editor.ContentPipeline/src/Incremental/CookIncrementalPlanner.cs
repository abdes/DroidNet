// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Compares saved product inputs and complete output hashes without starting native work.</summary>
internal static class CookIncrementalPlanner
{
    /// <summary>Verifies published roots before input capture or reuse planning.</summary>
    /// <param name="publication">The read lease over the published roots.</param>
    /// <param name="mounts">The mounts whose roots are verified.</param>
    /// <param name="native">The existing native content API.</param>
    /// <param name="cancellationToken">Cancels native work.</param>
    /// <param name="artifacts">Optional artifacts already retained by the operation.</param>
    /// <returns>The verified inventories keyed by mount; unreadable or unindexed roots are omitted.</returns>
    public static async Task<ImmutableDictionary<string, CookedInventoryReport>> ReadInventoriesAsync(
        CookPublicationReadLease publication,
        IEnumerable<string> mounts,
        IEngineContentPipelineApi native,
        CancellationToken cancellationToken,
        NativeArtifactLease? artifacts = null)
    {
        var inventories = ImmutableDictionary.CreateBuilder<string, CookedInventoryReport>(StringComparer.Ordinal);
        foreach (var mount in mounts.Distinct(StringComparer.Ordinal))
        {
            if (publication.UnavailableRoots.Any(root => string.Equals(root.Name, mount, StringComparison.Ordinal))
                || publication.FindProjectRoot(mount)?.FullPath is not { } path)
            {
                continue;
            }

            var inventory = await ReadRootAsync(path, native, artifacts, cancellationToken).ConfigureAwait(false);
            if (inventory is not null)
            {
                inventories.Add(mount, inventory);
            }
        }

        return inventories.ToImmutable();
    }

    /// <summary>Compares captured inputs with verified output facts without performing more I/O.</summary>
    /// <param name="inputs">The captured source and generated-product inputs.</param>
    /// <param name="graph">The dependency graph of the requested assets.</param>
    /// <param name="previous">The provenance saved by the previous cook.</param>
    /// <param name="inventories">The verified inventories of the published roots, by mount.</param>
    /// <param name="rebuiltRoots">Mounts whose roots are rebuilt, so none of their outputs are reused.</param>
    /// <param name="removedSources">Sources whose prior products leave the cooked content.</param>
    /// <param name="rebuiltSources">Sources the repair cooks again even though their inputs did not change.</param>
    /// <returns>The fingerprints to build and the verified products to reuse.</returns>
    public static CookIncrementalPlan CreatePlan(
        IReadOnlyList<CookSnapshotInput> inputs,
        CookDependencyGraph graph,
        CookProvenance previous,
        ImmutableDictionary<string, CookedInventoryReport> inventories,
        IReadOnlySet<string>? rebuiltRoots = null,
        IReadOnlySet<AssetPath>? removedSources = null,
        IReadOnlySet<AssetPath>? rebuiltSources = null)
    {
        var fingerprints = graph.Assets.ToImmutableDictionary(static input => input.AssetUri, input => Fingerprint(input, inputs, graph));

        var sharedRoots = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var validOutputs = new HashSet<(string Root, string Path)>();
        foreach (var root in previous.Roots)
        {
            if (rebuiltRoots?.Contains(root.Mount) == true
                || !inventories.TryGetValue(root.Mount, out var inventory) || !root.Matches(inventory))
            {
                continue;
            }

            var (shared, descriptors) = root.Compare(inventory, previous.Products.SelectMany(static product => product.Outputs));
            if (!shared)
            {
                continue;
            }

            _ = sharedRoots.Add(root.Mount);
            foreach (var output in previous.Products.SelectMany(static product => product.Outputs)
                .Where(output => string.Equals(output.RootMount, root.Mount, StringComparison.Ordinal) && output.Asset.DescriptorRelativePath is { } path && descriptors.Contains(path)))
            {
                _ = validOutputs.Add((root.Mount, output.Asset.VirtualPath));
            }
        }

        // A removed source has nothing to reuse, and a source the repair cooks again, such as a user of a removed
        // product, must cook to report what it lost even though its own inputs did not change.
        var reused = previous.Products.Where(product => removedSources?.Contains(product.SourceUri) != true
            && rebuiltSources?.Contains(product.SourceUri) != true
            && fingerprints.TryGetValue(product.SourceUri, out var fingerprint)
            && string.Equals(fingerprint, product.ReuseFingerprint, StringComparison.Ordinal)
            && product.Outputs.Length != 0
            && product.Outputs.All(output => validOutputs.Contains((output.RootMount, output.Asset.VirtualPath))))
            .ToImmutableDictionary(static product => product.SourceUri).ToBuilder();

        bool changed;
        do
        {
            changed = false;
            foreach (var (consumer, owners) in graph.ResourceDependencies)
            {
                if (reused.ContainsKey(consumer) && owners.Any(owner => !reused.ContainsKey(owner)))
                {
                    _ = reused.Remove(consumer);
                    changed = true;
                }
            }
        }
        while (changed);

        return new(fingerprints, reused.ToImmutable(), sharedRoots.ToImmutable())
        {
            VerifiedOutputs = validOutputs.ToImmutableHashSet(),
            PriorInventories = inventories,
            RemovedSources = removedSources?.ToImmutableHashSet() ?? [],
        };
    }

    internal static string Fingerprint(ContentCookInput input, IReadOnlyList<CookSnapshotInput> inputs, CookDependencyGraph graph)
    {
        var files = inputs.ToDictionary(static file => file.RelativePath, StringComparer.Ordinal);
        return Fingerprint(
            input,
            graph.FileDependencies[input.AssetUri].Select(path => files[path]),
            graph.Dependencies[input.AssetUri],
            graph.CookedDependencies.GetValueOrDefault(input.AssetUri, []));
    }

    // A product depends on its inputs alone: the engine's verification of the cooked root refuses outputs in a format
    // it no longer reads, so the tools that cooked them are no part of the fingerprint.
    internal static string Fingerprint(
        ContentCookInput input,
        IEnumerable<CookSnapshotInput> files,
        IEnumerable<AssetPath> dependencies,
        IEnumerable<CookedDependencySnapshot> cookedDependencies)
    {
        var authored = Hash(new
        {
            Version = 2,
            Source = input.AssetUri.ToUriString(),
            input.Kind,
            Files = files.Select(file => new
            {
                Path = file.RelativePath,
                file.DiscoveryHash,
                file.Kind,
                Metadata = file.ProbeShape,
            }),

            // Native descriptors refer to stable virtual asset identities; scalar material bytes do not alter their consumers.
            Dependencies = dependencies.Select(static uri => uri.ToUriString()),
        });
        var libraries = cookedDependencies
            .OrderBy(static dependency => dependency.AssetUri).ThenBy(static dependency => dependency.AssetKey, StringComparer.Ordinal).ToArray();
        return libraries.Length == 0 ? authored : Hash(new { Authored = authored, Libraries = libraries });
    }

    private static string Hash<T>(T value) => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(value)));

    private static async Task<CookedInventoryReport?> ReadRootAsync(string path, IEngineContentPipelineApi native, NativeArtifactLease? artifacts, CancellationToken cancellationToken)
    {
        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            return lease.HasIndex ? await lease.ReadInventoryAsync(native, cancellationToken, artifacts).ConfigureAwait(false) : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return null;
        }
    }
}
