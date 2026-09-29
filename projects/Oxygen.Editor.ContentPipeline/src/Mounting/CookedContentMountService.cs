// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentPipeline.Mounting;

/// <summary>Prepares the same saved source order for startup, recooking and explicit mount changes.</summary>
public sealed class CookedContentMountService
{
    /// <summary>Finds project-owned roots in deterministic order without including external libraries.</summary>
    /// <param name="project">The owning project.</param>
    /// <returns>Existing indexed project roots.</returns>
    public static IReadOnlyList<string> FindProjectRoots(ProjectContext project)
    {
        var root = Path.Combine(project.ProjectRoot, ".cooked");
        return File.Exists(Path.Combine(root, "container.index.bin")) ? [root]
            : project.AuthoringMounts.Where(static mount => mount.RelativePath.Trim().Replace('\\', '/').Trim('/') is not (".cooked" or ".imported" or ".build"))
            .Select(mount => Path.Combine(root, mount.Name))
            .Where(path => File.Exists(Path.Combine(path, "container.index.bin"))).Distinct(StringComparer.OrdinalIgnoreCase).Order(StringComparer.Ordinal).ToArray();
    }

    /// <summary>Validates and protects the complete ordered source set; takes ownership of the project reader on entry.</summary>
    /// <param name="project">The saved or candidate project configuration.</param>
    /// <param name="projectRoots">All installed roots owned by project publication.</param>
    /// <param name="projectReader">Read ownership of the project's published generation.</param>
    /// <param name="cancellationToken">Cancels preparation before native ownership transfer.</param>
    /// <returns>The root order and readers that must survive until native refresh or teardown drains.</returns>
    public async Task<CookedContentMountSet> PrepareAsync(ProjectContext project, IReadOnlyList<string> projectRoots, IDisposable projectReader, CancellationToken cancellationToken)
    {
        var readers = new List<IDisposable> { projectReader };
        try
        {
            var roots = ResolveRoots(project, projectRoots);
            foreach (var path in roots)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var files = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
                readers.Add(files);
                if (!files.HasIndex)
                {
                    throw new InvalidDataException($"Cooked content has no index: {path}.");
                }

                var index = await CookedIndexSnapshot.ReadAsync(path, cancellationToken).ConfigureAwait(false);
                index.ValidateMetadata(files.GetFiles());

            }

            return new(roots, readers);
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

    private static string[] ResolveRoots(ProjectContext project, IReadOnlyList<string> projectRoots)
    {
        var roots = new List<string>();
        var order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder);
        foreach (var source in order)
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput)
            {
                roots.AddRange(projectRoots.Order(StringComparer.Ordinal).Select(Path.GetFullPath));
            }
            else
            {
                var folder = project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.OrdinalIgnoreCase));
                if (File.Exists(Path.Combine(folder.AbsolutePath, "container.index.bin")))
                {
                    roots.Add(Path.GetFullPath(folder.AbsolutePath));
                }
            }
        }

        return roots.AsEnumerable().Reverse().Distinct(StringComparer.OrdinalIgnoreCase).Reverse().ToArray();
    }
}
