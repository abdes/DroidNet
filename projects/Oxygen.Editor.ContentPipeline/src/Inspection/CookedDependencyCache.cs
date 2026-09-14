// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Compatibility;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Inspection;

/// <summary>Retains native dependency reports for immutable, verified library contents.</summary>
internal static class CookedDependencyCache
{
    private static readonly NativeAtomicFileStore Files = new(new RealFileSystem());

    /// <summary>Hashes the protected container inputs with the same ordering as cook provenance.</summary>
    /// <param name="reader">The protected library files.</param>
    /// <param name="cancellationToken">Cancels hashing.</param>
    /// <returns>The container content fingerprint.</returns>
    public static async Task<string> FingerprintAsync(CookOutputReadLease reader, CancellationToken cancellationToken)
    {
        var hashes = await reader.ReadHashesAsync(cancellationToken).ConfigureAwait(false);
        return Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(hashes.Values.OrderBy(static file => file.RelativePath, StringComparer.Ordinal))));
    }

    /// <summary>Reads cached metadata without starting native work.</summary>
    /// <param name="projectRoot">The owning project's cache location.</param>
    /// <param name="fingerprint">The verified library content identity.</param>
    /// <param name="records">The protected native index records.</param>
    /// <param name="cancellationToken">Cancels cache reading.</param>
    /// <returns>A matching report, or null on a cache miss.</returns>
    public static async Task<CookedDependencyReport?> ReadAsync(string projectRoot, string fingerprint, IReadOnlyList<AssetRecord> records, CancellationToken cancellationToken)
    {
        var path = CachePath(projectRoot, fingerprint);
        var saved = await Files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        if (!saved.Version.Exists)
        {
            return null;
        }

        try
        {
            var cached = JsonSerializer.Deserialize<CachedReport>(saved.Content.AsSpan());
            if (cached?.Report is null || !string.Equals(cached.Fingerprint, fingerprint, StringComparison.Ordinal) || !string.Equals(cached.Digest, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(cached.Report))), StringComparison.Ordinal))
            {
                return null;
            }

            var report = CookedDependencyReport.Parse(cached.Report);
            ValidateRecords(report, records);
            return report;
        }
        catch (Exception error) when (error is JsonException or InvalidDataException)
        {
            return null;
        }
    }

    /// <summary>Populates a cache miss while the caller retains the library's file readers.</summary>
    /// <param name="projectRoot">The owning project's cache location.</param>
    /// <param name="root">The protected cooked library.</param>
    /// <param name="fingerprint">The verified library content identity.</param>
    /// <param name="records">The protected native index records.</param>
    /// <param name="inspector">The native metadata decoder.</param>
    /// <param name="operationRoot">Private native scratch space.</param>
    /// <param name="cancellationToken">Cancels inspection and cache writing.</param>
    /// <param name="artifacts">Optional borrowed native artifact ownership.</param>
    /// <returns>The verified cached or newly inspected report.</returns>
    public static async Task<CookedDependencyReport> EnsureAsync(string projectRoot, string root, string fingerprint, IReadOnlyList<AssetRecord> records, ICookedDependencyInspector inspector, string operationRoot, CancellationToken cancellationToken, NativeArtifactLease? artifacts = null)
    {
        var existing = await ReadAsync(projectRoot, fingerprint, records, cancellationToken).ConfigureAwait(false);
        if (existing is not null)
        {
            return existing;
        }

        var report = await inspector.InspectDependenciesAsync(operationRoot, root, cancellationToken, artifacts).ConfigureAwait(false);
        ValidateRecords(report, records);
        var path = CachePath(projectRoot, fingerprint);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var baseline = await Files.ReadAsync(path, cancellationToken).ConfigureAwait(false);
        var cached = new CachedReport(fingerprint, Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(report.Json))), report.Json);
        _ = await Files.WriteAsync(path, JsonSerializer.SerializeToUtf8Bytes(cached), baseline.Version, cancellationToken).ConfigureAwait(false);
        return report;
    }

    private static void ValidateRecords(CookedDependencyReport report, IReadOnlyList<AssetRecord> records)
    {
        if (report.Assets.Count != records.Count || records.Any(record => record.Cooked is not { } cooked
            || cooked.SourceIdentity != report.SourceIdentity
            || !report.Assets.TryGetValue(cooked.AssetKey.ToString(), out var asset)
            || asset.AssetType != cooked.AssetType
            || !string.Equals(asset.VirtualPath, Uri.UnescapeDataString(record.Uri.AbsolutePath), StringComparison.Ordinal)))
        {
            throw new InvalidDataException("The dependency report does not match the protected library index.");
        }
    }

    private static string CachePath(string projectRoot, string fingerprint)
        => Path.Combine(projectRoot, ".build", "cache", "cooked-dependencies-v1", fingerprint + ".json");

    private sealed record CachedReport(string Fingerprint, string Digest, string Report);
}
