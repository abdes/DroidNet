// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Retains unchanged lookup roots while native jobs consume private roots for affected mounts.</summary>
internal sealed partial class CookReferenceRoots : IDisposable
{
    private readonly List<CookOutputReadLease> readers = [];

    /// <summary>Gets native lookup roots in their stable project order.</summary>
    public IReadOnlyList<string> Paths { get; private set; } = [];

    /// <summary>Acquires unchanged referenced roots and checks their recorded byte identities before native work.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="staging">Private roots that remain writable by this operation.</param>
    /// <param name="previous">Verified prior publication metadata.</param>
    /// <param name="plan">The reused products needed by the closure.</param>
    /// <param name="cancellationToken">Cancels reader acquisition.</param>
    /// <returns>The lookup paths and retained readers.</returns>
    public static async Task<CookReferenceRoots> AcquireAsync(ProjectContext project, CookStagingArea staging, CookProvenance previous, CookIncrementalPlan plan, CancellationToken cancellationToken)
    {
        var result = new CookReferenceRoots();
        var paths = staging.Roots.ToDictionary(static root => root.Mount, static root => root.StagingPath, StringComparer.OrdinalIgnoreCase);
        try
        {
            foreach (var mount in plan.ReusedAssets.Select(static asset => asset.MountName).Distinct(StringComparer.OrdinalIgnoreCase))
            {
                if (paths.ContainsKey(mount))
                {
                    continue;
                }

                var path = Path.GetDirectoryName(CookIncrementalPlanner.ResolveOutputPath(project.ProjectRoot, mount, "container.index.bin"))!;
                CookOutputLease.RejectReparsePoint(path);
                var lease = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
                result.readers.Add(lease);
                if (!lease.HasIndex)
                {
                    throw new IOException($"Cooked dependency mount '{mount}' is missing its index. Cook that content again.");
                }

                var expected = previous.Roots.Single(root => string.Equals(root.Mount, mount, StringComparison.OrdinalIgnoreCase));
                var actual = await lease.ReadHashesAsync(cancellationToken).ConfigureAwait(false);
                if (expected.SharedFiles.Concat(expected.Assets.Select(static asset => asset.File)).Any(file =>
                    !actual.TryGetValue(file.RelativePath, out var observed) || observed.Size != file.Size || !string.Equals(observed.Sha256, file.Sha256, StringComparison.Ordinal)))
                {
                    throw new IOException($"Cooked dependency mount '{mount}' changed before native lookup. Cook that content again.");
                }

                paths.Add(mount, path);
            }

            result.Paths = paths.OrderBy(static pair => pair.Key, StringComparer.Ordinal).Select(static pair => pair.Value).ToArray();
            return result;
        }
        catch
        {
            result.Dispose();
            throw;
        }
    }

    /// <summary>Rechecks directory membership while the referenced file bytes remain locked.</summary>
    public void Verify()
    {
        foreach (var lease in this.readers)
        {
            _ = lease.GetFiles();
        }
    }

    /// <inheritdoc />
    public void Dispose()
    {
        foreach (var lease in this.readers)
        {
            lease.Dispose();
        }
    }
}
