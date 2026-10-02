// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.ContentPipeline.Mounting;

/// <summary>Admits one captured publication without recomputing root paths or precedence.</summary>
public sealed class CookedContentMountService
{
    /// <summary>Validates native metadata and retains the exact selection through runtime ownership.</summary>
    /// <param name="project">The owning project.</param>
    /// <param name="publication">The borrowed accepted or candidate snapshot.</param>
    /// <param name="cancellationToken">Cancels before native ownership transfer.</param>
    /// <returns>The complete ordered roots and their protected readers.</returns>
    public async Task<CookedContentMountSet> PrepareAsync(ProjectContext project, CookPublicationReadLease publication, CancellationToken cancellationToken)
    {
        if (project.ProjectId != publication.ProjectId
            || !string.Equals(Path.GetFullPath(project.ProjectRoot), publication.ProjectRoot, StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("Cooked content belongs to another project.");
        }

        var readers = new List<IDisposable>(publication.Roots.Length + 1);
        var captured = publication.Retain();
        readers.Add(captured);
        try
        {
            captured.RequireAvailableGenerations();
            for (var index = 0; index < captured.Roots.Length; index++)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var binding = captured.Roots[index];
                var path = captured.RootPaths[index];
                CookOutputLease.RejectReparsePoint(path);
                var files = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
                readers.Add(files);
                if (!files.HasIndex)
                {
                    throw new InvalidDataException($"Cooked content has no index: '{binding.Name}'.");
                }

                var inventory = await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false);
                if (inventory.Index.SourceGuid != binding.SourceKey || !string.Equals(inventory.Fingerprint, binding.IndexSha256, StringComparison.OrdinalIgnoreCase))
                {
                    throw new InvalidDataException($"Cooked library '{binding.Name}' changed after this publication was captured.");
                }

                inventory.ValidateMetadata(files.GetFiles());
            }

            return new(captured, captured.RootPaths, readers);
        }
        catch
        {
            foreach (var reader in readers.AsEnumerable().Reverse())
            {
                reader.Dispose();
            }

            throw;
        }
    }
}
