// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Persistence.LooseCooked;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns fresh candidate roots at their final paths under the coordinator's operation lifetime.</summary>
internal sealed class CookStagingArea : IAsyncDisposable
{
    private readonly List<CookStagingRoot> roots;
    private bool retained;
    private bool disposed;

    private CookStagingArea(CookPublicationReadLease baseline, CookPublicationTransaction transaction, int rootCount)
    {
        this.roots = new(rootCount);
        this.Baseline = baseline;
        this.Transaction = transaction;
    }

    internal CookPublicationReadLease Baseline { get; }

    internal CookPublicationTransaction Transaction { get; }

    internal IReadOnlyList<CookStagingRoot> Roots => this.roots;

    public void VerifyAcceptedInventories(IReadOnlyDictionary<string, CookedInventoryReport> inventories, IReadOnlySet<string> emptyRoots)
    {
        foreach (var root in this.Roots)
        {
            if (emptyRoots.Contains(root.Mount))
            {
                continue;
            }

            if (root.Before.Files.Count == 0)
            {
                if (inventories.ContainsKey(root.Mount))
                {
                    throw new IOException($"Cooked root disappeared after verification: '{root.Mount}'. Retry the cook.");
                }

                continue;
            }

            if (!inventories.TryGetValue(root.Mount, out var inventory)
                || !root.Before.Files.TryGetValue(LooseCookedIndex.FileName, out var index)
                || index.Size != inventory.IndexSize
                || !string.Equals(index.Sha256, inventory.IndexSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new IOException($"Cooked index changed before staging '{root.Mount}'. Retry the cook.");
            }

            var damaged = inventory.Issues.Select(static issue => issue.RelativePath).ToHashSet(StringComparer.Ordinal);
            var missing = inventory.Issues.Where(static issue => string.Equals(issue.Reason, "missing", StringComparison.Ordinal)).Select(static issue => issue.RelativePath).ToHashSet(StringComparer.Ordinal);
            foreach (var (path, expected) in inventory.Files)
            {
                if (!root.Before.Files.TryGetValue(path, out var actual))
                {
                    if (missing.Contains(path))
                    {
                        continue;
                    }

                    throw new IOException($"Cooked file disappeared before staging '{root.Mount}/{path}'. Retry the cook.");
                }

                // A resource data file records no digest of its own: its resources do, which the inventory has checked.
                var isData = expected.Kind?.IsResourceData() == true;
                if (!emptyRoots.Contains(root.Mount) && !damaged.Contains(path)
                    && (actual.Size != expected.Size || (!isData && !string.Equals(actual.Sha256, expected.Sha256, StringComparison.OrdinalIgnoreCase))))
                {
                    throw new IOException($"Cooked file changed after verification: '{root.Mount}/{path}'. Retry the cook.");
                }
            }

            // A retired file is listed by an earlier commit, and goes when no reader holds that commit.
            var retired = ReadRetired(root.Path);
            if (root.Before.Files.Keys.Any(path => !LooseCookedIndex.IsReservedPath(path) && !inventory.Files.ContainsKey(path) && !retired.Contains(path)))
            {
                throw new IOException($"Cooked membership changed before staging '{root.Mount}'. Retry the cook.");
            }
        }
    }

    // Called with a root the inventory has read, so its index is there and well formed.
    private static HashSet<string> ReadRetired(string root)
    {
        using var index = File.OpenRead(Path.Combine(root, LooseCookedIndex.FileName));
        return LooseCookedIndex.Read(index).Retired.Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
    }

    public async ValueTask DisposeAsync()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        try
        {
            // Every root is released even when an earlier one fails; a failure propagates once all were tried.
            await DisposeRootsAsync(this.Roots, 0).ConfigureAwait(false);

            if (!this.retained)
            {
                await this.Transaction.AbandonBuildAsync().ConfigureAwait(false);
                foreach (var root in this.Roots)
                {
                    CookOutputLease.RejectReparsePoint(root.Path);
                    if (Directory.Exists(root.Path))
                    {
                        // No head has referenced these candidates; native work
                        // has drained while the coordinator retains operation ownership.
                        Directory.Delete(root.Path, recursive: true);
                    }
                }
            }
        }
        finally
        {
            this.Baseline.Dispose();
        }
    }

    internal static string[] ValidateMounts(IEnumerable<string> mounts)
    {
        ArgumentNullException.ThrowIfNull(mounts);
        var names = mounts.ToArray();
        return names.Length == 0 || names.ToHashSet(StringComparer.Ordinal).Count != names.Length
            || names.Any(static name => string.IsNullOrWhiteSpace(name) || name is "." or ".."
                || !string.Equals(name, name.TrimEnd(' ', '.'), StringComparison.Ordinal)
                || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains('/', StringComparison.Ordinal) || name.Contains('\\', StringComparison.Ordinal))
            ? throw new ArgumentException("Cooking requires distinct mount names that are single directory names.", nameof(mounts))
            : [.. names.Order(StringComparer.Ordinal)];
    }

    internal static async Task<CookStagingArea> CreateAsync(
        ContentCookOperation operation,
        CookPublicationReadLease baseline,
        IEnumerable<string> mounts,
        IAtomicFileStore files,
        IProjectManagerService manager,
        CancellationToken cancellationToken,
        IReadOnlySet<string>? emptyRoots = null,
        Func<string, Task>? checkpoint = null)
    {
        var names = ValidateMounts(mounts);
        var generations = names.Select(static _ => Guid.CreateVersion7()).ToImmutableArray();
        var transaction = await CookPublicationTransaction.ReserveAsync(operation, baseline, generations, files, manager, cancellationToken, checkpoint).ConfigureAwait(false);
        CookStagingArea? area = null;
        var retainedBaseline = baseline.Retain();
        try
        {
            area = new(retainedBaseline, transaction, names.Length);
            retainedBaseline = null;
            for (var index = 0; index < names.Length; index++)
            {
                var name = names[index];
                var path = CookPublicationPaths.Generation(operation.Project.ProjectRoot.FullPath, generations[index]);
                if (Directory.Exists(path) || File.Exists(path))
                {
                    throw new IOException("A fresh generation identity already has an output path.");
                }

                // Publish ownership to the area before a fallible seed copy.
                var root = new CookStagingRoot(name, generations[index], path);
                area.roots.Add(root);
                _ = Directory.CreateDirectory(path);
                if (emptyRoots?.Contains(name) != true && baseline.FindProjectRoot(name)?.FullPath is { } previous)
                {
                    await root.SeedAsync(previous, cancellationToken).ConfigureAwait(false);
                }
            }

            return area;
        }
        catch
        {
            if (area is not null)
            {
                await area.DisposeAsync().ConfigureAwait(false);
            }

            retainedBaseline?.Dispose();
            throw;
        }
    }

    internal void RetainForPublication() => this.retained = true;

    internal ImmutableArray<CookPublicationRoot> SealRoots() => [.. this.Roots.Select(static root => root.Seal())];

    // Releases the roots from the index on, each one after the one before it, whether or not that one failed.
    private static async ValueTask DisposeRootsAsync(IReadOnlyList<CookStagingRoot> roots, int index)
    {
        if (index >= roots.Count)
        {
            return;
        }

        try
        {
            await roots[index].DisposeAsync().ConfigureAwait(false);
        }
        finally
        {
            await DisposeRootsAsync(roots, index + 1).ConfigureAwait(false);
        }
    }
}
