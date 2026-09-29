// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Compares saved product inputs and complete output hashes without starting native work.</summary>
internal static class CookIncrementalPlanner
{
    /// <summary>Verifies published roots before input capture or reuse planning.</summary>
    public static async Task<ImmutableDictionary<string, CookedInventoryReport>> ReadInventoriesAsync(
        ProjectContext project, IEnumerable<string> mounts, IEngineContentPipelineApi native, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        var inventories = ImmutableDictionary.CreateBuilder<string, CookedInventoryReport>(StringComparer.Ordinal);
        foreach (var mount in mounts.Distinct(StringComparer.Ordinal))
        {
            var inventory = await ReadRootAsync(project.ProjectRoot, mount, native, cancellationToken, artifacts).ConfigureAwait(false);
            if (inventory is not null)
            {
                inventories.Add(mount, inventory);
            }
        }

        return inventories.ToImmutable();
    }

    /// <summary>Compares captured inputs with verified output facts without performing more I/O.</summary>
    public static CookIncrementalPlan CreatePlan(string producer, IReadOnlyList<CookSnapshotInput> inputs,
        CookDependencyGraph graph, CookProvenance previous, ImmutableDictionary<string, CookedInventoryReport> inventories,
        IReadOnlySet<string>? rebuiltRoots = null)
    {
        var fingerprints = graph.Assets.ToImmutableDictionary(static input => input.AssetUri, input => Fingerprint(input, producer, inputs, graph));
        foreach (var builtin in CookIncrementalPlan.Builtins(graph))
        {
            fingerprints = fingerprints.SetItem(builtin, GeneratedFingerprint(builtin, producer));
        }

        var sharedRoots = ImmutableHashSet.CreateBuilder<string>(StringComparer.Ordinal);
        var validOutputs = new HashSet<(string root, string path)>();
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
                .Where(output => output.RootMount == root.Mount && output.Asset.DescriptorRelativePath is { } path && descriptors.Contains(path)))
            {
                _ = validOutputs.Add((root.Mount, output.Asset.VirtualPath));
            }
        }

        var reused = previous.Products.Where(product => fingerprints.TryGetValue(product.SourceUri, out var fingerprint)
            && string.Equals(fingerprint, product.ReuseFingerprint, StringComparison.Ordinal)
            && product.Outputs.Length != 0
            && product.Outputs.All(output => validOutputs.Contains((output.RootMount, output.Asset.VirtualPath))))
            .ToImmutableDictionary(static product => product.SourceUri);

        return new(fingerprints, reused, sharedRoots.ToImmutable()) { VerifiedOutputs = validOutputs.ToImmutableHashSet(), PriorInventories = inventories };
    }

    /// <summary>Builds an engine-generated product identity from its recipe owner and producer.</summary>
    /// <param name="source">The stable engine identity.</param>
    /// <param name="producer">The engine/tool/generator content identity.</param>
    /// <returns>The deterministic fingerprint.</returns>
    public static string GeneratedFingerprint(Uri source, string producer) => Hash(new { Version = 1, Source = source.AbsoluteUri, Producer = producer });

    /// <summary>Resolves a cache path only within the declared cooked root.</summary>
    /// <param name="projectRoot">The project directory.</param>
    /// <param name="mount">The physical output mount.</param>
    /// <param name="relative">The root-relative file path.</param>
    /// <returns>The contained absolute path.</returns>
    public static string ResolveOutputPath(string projectRoot, string mount, string relative)
    {
        if (string.IsNullOrWhiteSpace(mount) || mount is "." or ".." || mount.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new InvalidDataException("Cook provenance contains an invalid mount name.");
        }

        var root = Path.GetFullPath(ContentPipelinePaths.GetCookedMountRoot(projectRoot, mount));
        var path = Path.GetFullPath(Path.Combine(root, relative));
        return path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)
            ? path : throw new InvalidDataException("Cook provenance contains a file outside its output root.");
    }

    internal static string Fingerprint(ContentCookInput input, string producer, IReadOnlyList<CookSnapshotInput> inputs, CookDependencyGraph graph)
    {
        var files = inputs.ToDictionary(static file => file.RelativePath, StringComparer.Ordinal);
        var authored = Hash(new
        {
            Version = 1,
            Source = input.AssetUri.AbsoluteUri,
            input.Kind,
            input.OutputVirtualPath,
            Producer = producer,
            Files = graph.FileDependencies[input.AssetUri].Select(path => new { Path = path, files[path].DiscoveryHash, files[path].IsAbsent }),

            // Native descriptors refer to stable virtual asset identities; scalar material bytes do not alter their consumers.
            Dependencies = graph.Dependencies[input.AssetUri].Select(static uri => uri.AbsoluteUri),
        });
        var libraries = graph.CookedDependencies.GetValueOrDefault(input.AssetUri, [])
            .OrderBy(static dependency => dependency.AssetUri.AbsoluteUri, StringComparer.Ordinal).ThenBy(static dependency => dependency.AssetKey, StringComparer.Ordinal).ToArray();
        return libraries.Length == 0 ? authored : Hash(new { Authored = authored, Libraries = libraries });
    }

    private static string Hash<T>(T value) => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(value)));

    private static async Task<CookedInventoryReport?> ReadRootAsync(string projectRoot, string mount, IEngineContentPipelineApi native, CancellationToken cancellationToken, NativeArtifactLease? artifacts)
    {
        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(ContentPipelinePaths.GetCookedMountRoot(projectRoot, mount), cancellationToken).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            return lease.HasIndex ? await lease.ReadInventoryAsync(native, cancellationToken, artifacts).ConfigureAwait(false) : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return null;
        }
    }
}
