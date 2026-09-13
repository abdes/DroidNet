// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Storage;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.LooseCooked;

namespace Oxygen.Editor.ContentPipeline.Mounting;

/// <summary>Prepares the same saved source order for startup, recooking and explicit mount changes.</summary>
/// <param name="storage">The storage provider used for cooked-index discovery.</param>
/// <param name="native">The shared cooked-root inspection boundary.</param>
public sealed class CookedContentMountService(IStorageProvider storage, IEngineContentPipelineApi native)
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
            foreach (var (path, isLibrary) in roots)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var files = await CookOutputReadLease.AcquireAsync(path, cancellationToken).ConfigureAwait(false);
                readers.Add(files);
                if (!files.HasIndex)
                {
                    throw new InvalidDataException($"Cooked content has no index: {path}.");
                }

                using var catalog = new LooseCookedIndexAssetCatalog(storage, new LooseCookedIndexAssetCatalogOptions { CookedRootFolderPath = path });
                var records = await catalog.QueryAsync(new(AssetQueryScope.All), cancellationToken).ConfigureAwait(false);
                var protectedPaths = files.GetFiles().Select(static file => file.RelativePath).ToHashSet(StringComparer.Ordinal);
                if (records.Any(record => record.Cooked is null || !protectedPaths.Contains(record.Cooked.DescriptorRelativePath)))
                {
                    throw new InvalidDataException($"Cooked index references a descriptor outside its protected file set: {path}.");
                }

                if (isLibrary)
                {
                    await files.VerifyDescriptorsAsync(records, cancellationToken).ConfigureAwait(false);
                }

                var validation = await native.ValidateLooseCookedRootAsync(path, cancellationToken).ConfigureAwait(false);
                if (!validation.Succeeded)
                {
                    throw new InvalidDataException(string.Join(Environment.NewLine, validation.Diagnostics.Select(static diagnostic => diagnostic.Message)));
                }
            }

            return new(roots.Select(static root => root.path).ToArray(), readers);
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

    private static (string path, bool isLibrary)[] ResolveRoots(ProjectContext project, IReadOnlyList<string> projectRoots)
    {
        var roots = new List<(string path, bool isLibrary)>();
        var order = CookedContentOrdering.Resolve(project.LocalFolderMounts, project.CookedContentOrder);
        foreach (var source in order)
        {
            if (source.Kind == CookedContentSourceKind.ProjectOutput)
            {
                roots.AddRange(projectRoots.Order(StringComparer.Ordinal).Select(static path => (Path.GetFullPath(path), false)));
            }
            else
            {
                var folder = project.LocalFolderMounts.Single(mount => string.Equals(mount.Name, source.Name, StringComparison.OrdinalIgnoreCase));
                if (File.Exists(Path.Combine(folder.AbsolutePath, "container.index.bin")))
                {
                    roots.Add((Path.GetFullPath(folder.AbsolutePath), true));
                }
            }
        }

        return roots.AsEnumerable().Reverse().DistinctBy(static root => root.path, StringComparer.OrdinalIgnoreCase).Reverse().ToArray();
    }
}
