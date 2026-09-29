// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.ContentPipeline.Inspection;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Owns same-volume private output and the published baselines used to seed it.</summary>
internal sealed partial class CookStagingArea : IDisposable
{
    private readonly string outputDirectory;
    private readonly FileStream operationLease;
    private bool retained;
    private bool disposed;

    private CookStagingArea(string outputDirectory, FileStream operationLease)
    {
        this.outputDirectory = outputDirectory;
        this.operationLease = operationLease;
    }

    /// <summary>Gets the complete affected-root set and its pre-cook identities.</summary>
    public ImmutableArray<Root> Roots { get; private set; } = [];

    /// <summary>Seeds only the affected roots, preserving unrelated assets and timestamps within them.</summary>
    /// <param name="operation">The project cook that owns this output.</param>
    /// <param name="mounts">Distinct physical authoring mount names.</param>
    /// <param name="cancellationToken">Cancels private preparation.</param>
    /// <returns>The operation's private output owner.</returns>
    public static async Task<CookStagingArea> CreateAsync(ContentCookOperation operation, IEnumerable<string> mounts, CancellationToken cancellationToken, IReadOnlySet<string>? emptyRoots = null)
    {
        var names = ValidateMounts(mounts);
        using var reader = await CookOutputLease.AcquireInspectionAsync(operation.Project.ProjectRoot, cancellationToken).ConfigureAwait(false);
        var operationDirectory = Path.Combine(operation.Project.ProjectRoot, ".build", "cook", operation.OperationId.ToString("N"));
        CookOutputLease.RejectReparsePoint(operationDirectory);
        var output = Path.Combine(operationDirectory, "output");
        CookOutputLease.RejectReparsePoint(output);
        if (Directory.Exists(output))
        {
            throw new IOException("This cook operation already owns a staging directory.");
        }

        _ = Directory.CreateDirectory(output);
        var roots = ImmutableArray.CreateBuilder<Root>();
        var lease = CookOutputLease.AcquireOperation(operation.Project.ProjectRoot, operation.OperationId);
        var area = new CookStagingArea(output, lease);
        Exception? originalFailure = null;
        try
        {
            foreach (var name in names)
            {
                var published = Path.Combine(operation.Project.ProjectRoot, ".cooked", name);
                CookOutputLease.RejectReparsePoint(Path.GetDirectoryName(published)!);
                var staging = Path.Combine(output, name);
                _ = Directory.CreateDirectory(staging);
                var before = await CookRootImage.CaptureAsync(published, emptyRoots?.Contains(name) == true ? null : staging, cancellationToken).ConfigureAwait(false);
                var after = await CookRootImage.CaptureAsync(published, copyTo: null, cancellationToken).ConfigureAwait(false);
                if (!before.Matches(after))
                {
                    throw new IOException($"Published content changed while seeding '{name}'. Retry the cook.");
                }

                roots.Add(new(name, published, staging, before));
            }

            area.Roots = roots.ToImmutable();
            var result = area;
            area = null;
            return result;
        }
        catch (Exception failure)
        {
            originalFailure = failure;
            throw;
        }
        finally
        {
            try
            {
                area?.Dispose();
            }
            catch (Exception cleanup) when (originalFailure is not null && cleanup is IOException or UnauthorizedAccessException)
            {
                originalFailure.Data["RetainedStaging"] = output;
                originalFailure.Data["StagingCleanupFailure"] = cleanup.Message;
            }
        }
    }

    /// <summary>Transfers cleanup to the publication journal, which may need staging after an interruption.</summary>
    public void RetainForPublication() => this.retained = true;

    /// <summary>Checks the captured seed against accepted native facts before any importer can rewrite hashes.</summary>
    public void VerifyAcceptedInventories(IReadOnlyDictionary<string, CookedInventoryReport> inventories, IReadOnlySet<string> emptyRoots)
    {
        foreach (var root in this.Roots)
        {
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

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        try
        {
            if (!this.retained && Directory.Exists(this.outputDirectory))
            {
                CookOutputLease.RejectReparsePoint(this.outputDirectory);
                Directory.Delete(this.outputDirectory, recursive: true);
            }
        }
        finally
        {
            this.operationLease.Dispose();
        }
    }

    /// <summary>Validates physical mount directory names before publication or recovery.</summary>
    /// <param name="mounts">The names being resolved into owned paths.</param>
    /// <returns>The validated, ordered names.</returns>
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

    /// <summary>One seeded root and the prior complete published file set.</summary>
    /// <param name="Mount">The physical output mount name.</param>
    /// <param name="PublishedPath">The fixed runtime output location.</param>
    /// <param name="StagingPath">The private native writer destination.</param>
    /// <param name="Before">The coherent pre-cook output identity.</param>
    public sealed record Root(string Mount, string PublishedPath, string StagingPath, CookRootImage Before);
}
