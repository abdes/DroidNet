// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Text.Json;
using System.Text.Json.Serialization;
using DroidNet.Storage;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Incremental;

/// <summary>Persists incremental cook evidence independently of publication transaction metadata.</summary>
/// <param name="files">The ordinary atomic storage boundary.</param>
internal sealed class CookProvenanceStore(IAtomicFileStore files)
{
    private static readonly JsonSerializerOptions Options = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    /// <summary>Reads the last cache, retaining a compare-and-swap baseline even if its contents are obsolete.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="cancellationToken">Cancels reading.</param>
    /// <returns>The usable cache and storage version.</returns>
    public async Task<(CookProvenance provenance, FileVersion version)> ReadAsync(ProjectContext project, CancellationToken cancellationToken)
    {
        var snapshot = await files.ReadAsync(PathFor(project), cancellationToken).ConfigureAwait(false);
        var empty = new CookProvenance(1, project.ProjectId, [], []);
        if (!snapshot.Version.Exists)
        {
            return (empty, snapshot.Version);
        }

        try
        {
            var cache = JsonSerializer.Deserialize<CookProvenance>(snapshot.Content.AsSpan(), Options);
            return (cache is not null && IsValid(cache, project) ? cache : empty, snapshot.Version);
        }
        catch (JsonException)
        {
            return (empty, snapshot.Version);
        }
    }

    /// <summary>Commits verified cook evidence without silently replacing another writer's cache.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="provenance">The complete updated cache.</param>
    /// <param name="version">The version read before cooking.</param>
    /// <param name="cancellationToken">Cancels before the atomic replacement.</param>
    /// <returns>The asynchronous write.</returns>
    public async Task WriteAsync(ProjectContext project, CookProvenance provenance, FileVersion version, CancellationToken cancellationToken)
        => _ = await files.WriteAsync(PathFor(project), Serialize(project, provenance), version, cancellationToken).ConfigureAwait(false);

    /// <summary>Validates and serializes product evidence for a journaled metadata commit.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="provenance">The complete updated product evidence.</param>
    /// <returns>The bytes committed with the publication receipt.</returns>
    internal static byte[] Serialize(ProjectContext project, CookProvenance provenance)
        => IsValid(provenance, project) ? JsonSerializer.SerializeToUtf8Bytes(provenance, Options)
            : throw new InvalidDataException("Cooked output did not establish complete product provenance.");

    private static string PathFor(ProjectContext project) => Path.Combine(project.ProjectRoot, ".build", "cook", "provenance.json");

    private static bool IsValid(CookProvenance cache, ProjectContext project)
    {
        if (cache.Version != 1 || cache.ProjectId != project.ProjectId || cache.Roots.IsDefault || cache.Products.IsDefault
            || cache.Roots.Any(static root => root?.SharedFiles.IsDefaultOrEmpty != false || root.SharedFiles.Any(static file => file is null) || root.Assets.IsDefault)
            || cache.Roots.Any(static root => root.Assets.Any(static asset => asset is null || asset.Entry is null || string.IsNullOrWhiteSpace(asset.Entry.VirtualPath) || asset.File is null))
            || cache.Products.Any(static product => product?.SourceUri is null || !product.SourceUri.IsAbsoluteUri || product.Fingerprint is not { Length: 64 } || !product.Fingerprint.All(Uri.IsHexDigit) || product.Dependencies.IsDefault || product.Outputs.IsDefaultOrEmpty || product.Diagnostics.IsDefault || product.Diagnostics.Any(static diagnostic => diagnostic is null)
                || product.CookedDependencies.IsDefault || product.CookedDependencies.Any(static dependency => dependency?.AssetUri?.IsAbsoluteUri != true
                    || string.IsNullOrWhiteSpace(dependency.SourceName) || !Path.IsPathFullyQualified(dependency.RootPath)
                    || string.IsNullOrWhiteSpace(dependency.AssetKey) || dependency.ContentFingerprint is not { Length: 64 } || !dependency.ContentFingerprint.All(Uri.IsHexDigit)))
            || cache.Roots.Select(static root => root.Mount).ToHashSet(StringComparer.Ordinal).Count != cache.Roots.Length
            || cache.Products.Select(static product => product.SourceUri).ToHashSet().Count != cache.Products.Length)
        {
            return false;
        }

        try
        {
            foreach (var root in cache.Roots)
            {
                if (!root.SharedFiles.Any(static file => string.Equals(file.RelativePath, "container.index.bin", StringComparison.Ordinal)))
                {
                    return false;
                }

                foreach (var proof in root.SharedFiles.Concat(root.Assets.Select(static asset => asset.File)))
                {
                    if (proof is null || proof.Size < 0 || proof.Sha256 is not { Length: 64 } || !proof.Sha256.All(Uri.IsHexDigit))
                    {
                        return false;
                    }

                    _ = CookIncrementalPlanner.ResolveOutputPath(project.ProjectRoot, root.Mount, proof.RelativePath);
                }
            }

            return cache.Products.SelectMany(static product => product.Outputs).All(output => output?.Asset is not null
                && cache.Roots.Any(root => string.Equals(root.Mount, output.RootMount, StringComparison.Ordinal)
                    && root.Assets.Any(asset => string.Equals(asset.Entry.VirtualPath, output.Asset.VirtualPath, StringComparison.Ordinal))));
        }
        catch (Exception exception) when (exception is InvalidDataException or ArgumentException)
        {
            return false;
        }
    }
}
