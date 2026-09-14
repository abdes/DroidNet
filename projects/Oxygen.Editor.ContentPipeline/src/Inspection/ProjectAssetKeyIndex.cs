// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using DroidNet.Storage.Native;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Caches engine identities for current project candidates without decoding native formats.</summary>
internal sealed class ProjectAssetKeyIndex(string cachePath, string[] paths)
{
    private static readonly NativeAtomicFileStore Files = new(new RealFileSystem());
    private CookedAssetKeyMap? map;

    /// <summary>Gets a value indicating whether candidate identities still need a native lookup.</summary>
    public bool IsPending => paths.Length != 0 && this.map is null;

    /// <summary>Reads a cache for the current descriptor and known imported-output names.</summary>
    /// <param name="project">The source mounts to inspect.</param>
    /// <param name="knownOutputs">Previously identified imported outputs.</param>
    /// <param name="cancellationToken">Cancels enumeration and cache reading.</param>
    /// <returns>A current candidate index, possibly awaiting native identity lookup.</returns>
    public static async Task<ProjectAssetKeyIndex> ReadAsync(ProjectContext project, IReadOnlyCollection<Uri> knownOutputs, CancellationToken cancellationToken)
    {
        var candidates = EnumeratePaths(project, cancellationToken).Concat(knownOutputs.Select(static uri => Uri.UnescapeDataString(uri.AbsolutePath)))
            .Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        var fingerprint = Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(candidates)));
        var index = new ProjectAssetKeyIndex(Path.Combine(project.ProjectRoot, ".build", "cache", "project-asset-keys-v1", fingerprint + ".json"), candidates);
        await index.ReadCacheAsync(cancellationToken).ConfigureAwait(false);
        return index;
    }

    /// <summary>Finds a native dependency's project path after its identity map is available.</summary>
    /// <param name="key">The native key in managed index representation.</param>
    /// <returns>The candidate identity, or null when absent or not yet inspected.</returns>
    public Uri? Resolve(string key)
        => this.map?.PathsByKey.GetValueOrDefault(key) is { } path
            ? new Uri("asset:///" + string.Join('/', path.TrimStart('/').Split('/').Select(Uri.EscapeDataString))) : null;

    /// <summary>Fills a cache miss using one batched native metadata operation.</summary>
    /// <param name="provider">The engine identity authority.</param>
    /// <param name="operationRoot">Private operation scratch space.</param>
    /// <param name="cancellationToken">Cancels lookup and persistence.</param>
    /// <param name="artifacts">Optional borrowed native artifact ownership.</param>
    /// <returns>Whether new identities were cached.</returns>
    public async Task<bool> EnsureAsync(ICookedAssetKeyProvider provider, string operationRoot, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        if (!this.IsPending)
        {
            return false;
        }

        var report = await provider.ResolveAssetKeysAsync(operationRoot, paths, cancellationToken, artifacts).ConfigureAwait(false);
        report.ValidatePaths(paths);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(cachePath)!);
        var baseline = await Files.ReadAsync(cachePath, cancellationToken).ConfigureAwait(false);
        var cached = new CachedMap(Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(report.Json))), report.Json);
        _ = await Files.WriteAsync(cachePath, JsonSerializer.SerializeToUtf8Bytes(cached), baseline.Version, cancellationToken).ConfigureAwait(false);
        this.map = report;
        return true;
    }

    private static IEnumerable<string> EnumeratePaths(ProjectContext project, CancellationToken cancellationToken)
    {
        foreach (var mount in project.AuthoringMounts)
        {
            var root = Path.GetFullPath(Path.Combine(project.ProjectRoot, mount.RelativePath));
            var relative = Path.GetRelativePath(project.ProjectRoot, root).Replace('\\', '/');
            if (Path.IsPathRooted(relative) || relative.StartsWith("..", StringComparison.Ordinal)
                || relative.Split('/')[0].ToUpperInvariant() is ".COOKED" or ".IMPORTED" or ".BUILD" or ".PIPELINE" || !Directory.Exists(root))
            {
                continue;
            }

            Publication.CookOutputLease.RejectReparsePoint(root);
            var options = new EnumerationOptions { RecurseSubdirectories = true, AttributesToSkip = FileAttributes.ReparsePoint, IgnoreInaccessible = false, MatchCasing = MatchCasing.CaseInsensitive };
            foreach (var file in Directory.EnumerateFiles(root, "*.json", options))
            {
                cancellationToken.ThrowIfCancellationRequested();
                if (Path.GetExtension(file[..^5]).ToUpperInvariant() is ".OMAT" or ".OGEO" or ".OSCENE")
                {
                    yield return "/" + mount.Name + "/" + Path.GetRelativePath(root, file[..^5]).Replace('\\', '/');
                }
            }
        }
    }

    private async Task ReadCacheAsync(CancellationToken cancellationToken)
    {
        var saved = await Files.ReadAsync(cachePath, cancellationToken).ConfigureAwait(false);
        if (!saved.Version.Exists)
        {
            return;
        }

        try
        {
            var cached = JsonSerializer.Deserialize<CachedMap>(saved.Content.AsSpan());
            if (cached?.Report is null || !string.Equals(cached.Digest, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(cached.Report))), StringComparison.Ordinal))
            {
                return;
            }

            var report = CookedAssetKeyMap.Parse(cached.Report);
            report.ValidatePaths(paths);
            this.map = report;
        }
        catch (Exception error) when (error is JsonException or InvalidDataException)
        {
            // Derived metadata can be regenerated; it never proves identities after corruption.
        }
    }

    private sealed record CachedMap(string Digest, string Report);
}
