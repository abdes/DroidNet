// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Compares saved product inputs and complete output hashes without starting native work.</summary>
internal static class CookIncrementalPlanner
{
    /// <summary>Resolves changed products within the requested saved dependency closure.</summary>
    /// <param name="snapshot">The coherent inputs and producer identity.</param>
    /// <param name="graph">The complete saved dependencies.</param>
    /// <param name="previous">Previously accepted output provenance.</param>
    /// <param name="cancellationToken">Cancels file verification.</param>
    /// <returns>The products to reuse or rebuild.</returns>
    public static Task<CookIncrementalPlan> PlanAsync(CookInputSnapshot snapshot, CookDependencyGraph graph, CookProvenance previous, CancellationToken cancellationToken)
        => PlanAsync(snapshot.Operation.Project, snapshot.BuildFingerprint, snapshot.Inputs, graph, previous, cancellationToken);

    /// <summary>Inspects saved inputs and published output without creating a cook operation or private staging.</summary>
    /// <param name="project">The project whose published products are inspected.</param>
    /// <param name="producer">The current native producer identity.</param>
    /// <param name="inputs">The saved file identities.</param>
    /// <param name="graph">The saved dependency graph.</param>
    /// <param name="previous">The committed product provenance.</param>
    /// <param name="cancellationToken">Cancels read-only verification.</param>
    /// <returns>Current fingerprints, reusable products, and verified prior output.</returns>
    public static async Task<CookIncrementalPlan> PlanAsync(ProjectContext project, string producer, IReadOnlyList<CookSnapshotInput> inputs, CookDependencyGraph graph, CookProvenance previous, CancellationToken cancellationToken)
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
            var (shared, assets) = await CheckRootAsync(project.ProjectRoot, root, cancellationToken).ConfigureAwait(false);
            if (!shared)
            {
                continue;
            }

            _ = sharedRoots.Add(root.Mount);
            foreach (var virtualPath in assets)
            {
                _ = validOutputs.Add((root.Mount, virtualPath));
            }
        }

        var reused = previous.Products.Where(product => fingerprints.TryGetValue(product.SourceUri, out var fingerprint)
            && string.Equals(fingerprint, product.Fingerprint, StringComparison.Ordinal)
            && product.Outputs.Length != 0
            && product.Outputs.All(output => validOutputs.Contains((output.RootMount, output.Asset.VirtualPath))))
            .ToImmutableDictionary(static product => product.SourceUri);

        return new(fingerprints, reused, sharedRoots.ToImmutable()) { VerifiedOutputs = validOutputs.ToImmutableHashSet() };
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

    private static string Fingerprint(ContentCookInput input, string producer, IReadOnlyList<CookSnapshotInput> inputs, CookDependencyGraph graph)
    {
        var files = inputs.ToDictionary(static file => file.RelativePath, StringComparer.Ordinal);
        return Hash(new
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
    }

    private static string Hash<T>(T value) => Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(value)));

    private static async Task<(bool shared, ImmutableArray<string> assets)> CheckRootAsync(string projectRoot, CookProvenance.Root root, CancellationToken cancellationToken)
    {
        try
        {
            var lease = await CookOutputReadLease.AcquireAsync(ContentPipelinePaths.GetCookedMountRoot(projectRoot, root.Mount), cancellationToken).ConfigureAwait(false);
            await using var lifetime = lease.ConfigureAwait(false);
            var actual = await lease.ReadHashesAsync(cancellationToken).ConfigureAwait(false);
            var shared = root.SharedFiles.All(Matches);
            return (shared, [.. root.Assets.Where(asset => Matches(asset.File)).Select(static asset => asset.Entry.VirtualPath)]);

            bool Matches(CookProvenance.FileProof proof) => actual.TryGetValue(proof.RelativePath, out var file) && file.Size == proof.Size && string.Equals(file.Sha256, proof.Sha256, StringComparison.Ordinal);
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException)
        {
            return (false, []);
        }
    }
}
