// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns fresh candidate roots at their final paths under the coordinator's operation lifetime.</summary>
internal sealed class CookStagingArea : IAsyncDisposable
{
    private bool retained;
    private bool disposed;
    private readonly List<CookStagingRoot> roots;

    private CookStagingArea(CookPublicationReadLease baseline, CookPublicationTransaction transaction, int rootCount)
    {
        this.roots = new(rootCount);
        this.Baseline = baseline;
        this.Transaction = transaction;
    }

    internal CookPublicationReadLease Baseline { get; }

    internal CookPublicationTransaction Transaction { get; }

    internal IReadOnlyList<CookStagingRoot> Roots => this.roots;

    internal static async Task<CookStagingArea> CreateAsync(ContentCookOperation operation, CookPublicationReadLease baseline,
        IEnumerable<string> mounts, IAtomicFileStore files, IProjectManagerService manager,
        CancellationToken cancellationToken, IReadOnlySet<string>? emptyRoots = null, Func<string, Task>? checkpoint = null)
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
                var path = CookPublicationPaths.Generation(operation.Project.ProjectRoot, generations[index]);
                if (Directory.Exists(path) || File.Exists(path))
                {
                    throw new IOException("A fresh generation identity already has an output path.");
                }

                // Publish ownership to the area before a fallible seed copy.
                var root = new CookStagingRoot(name, generations[index], path);
                area.roots.Add(root);
                _ = Directory.CreateDirectory(path);
                if (emptyRoots?.Contains(name) != true && baseline.FindProjectRoot(name) is { } previous)
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
                || !root.Before.Files.TryGetValue("container.index.bin", out var index)
                || index.Size != inventory.IndexSize
                || !string.Equals(index.Sha256, inventory.IndexSha256, StringComparison.OrdinalIgnoreCase))
            {
                throw new IOException($"Cooked index changed before staging '{root.Mount}'. Retry the cook.");
            }

            var damaged = inventory.Issues.Select(static issue => issue.RelativePath).ToHashSet(StringComparer.Ordinal);
            var missing = inventory.Issues.Where(static issue => issue.Reason == "missing").Select(static issue => issue.RelativePath).ToHashSet(StringComparer.Ordinal);
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

                if (!emptyRoots.Contains(root.Mount) && !damaged.Contains(path)
                    && (actual.Size != expected.Size || !string.Equals(actual.Sha256, expected.Sha256, StringComparison.OrdinalIgnoreCase)))
                {
                    throw new IOException($"Cooked file changed after verification: '{root.Mount}/{path}'. Retry the cook.");
                }
            }

            if (root.Before.Files.Keys.Any(path => path is not ("container.index.bin" or ".generation.lock") && !inventory.Files.ContainsKey(path)))
            {
                throw new IOException($"Cooked membership changed before staging '{root.Mount}'. Retry the cook.");
            }
        }
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
            Exception? releaseFailure = null;
            foreach (var root in this.Roots)
            {
                try
                {
                    await root.DisposeAsync().ConfigureAwait(false);
                }
                catch (Exception failure)
                {
                    releaseFailure ??= failure;
                }
            }

            if (releaseFailure is not null)
            {
                System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(releaseFailure).Throw();
            }

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
        return names.Length == 0 || names.ToHashSet(StringComparer.OrdinalIgnoreCase).Count != names.Length
            || names.Any(static name => string.IsNullOrWhiteSpace(name) || name is "." or ".."
                || !string.Equals(name, name.TrimEnd(' ', '.'), StringComparison.Ordinal)
                || name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0 || name.Contains('/', StringComparison.Ordinal) || name.Contains('\\', StringComparison.Ordinal))
            ? throw new ArgumentException("Cooking requires distinct mount names that are single directory names.", nameof(mounts))
            : names.Order(StringComparer.Ordinal).ToArray();
    }

}
