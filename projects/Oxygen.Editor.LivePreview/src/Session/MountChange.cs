// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Session;

/// <summary>Validates a confirmed content-mount change and builds the project context it activates.</summary>
internal static class MountChange
{
    /// <summary>Builds the context a candidate configuration activates, refusing a change that leaves the project.</summary>
    /// <param name="expected">The configuration accepted when editing began.</param>
    /// <param name="candidate">The candidate manifest.</param>
    /// <returns>The candidate context.</returns>
    /// <exception cref="InvalidOperationException">The candidate is another project.</exception>
    /// <exception cref="InvalidDataException">The candidate's mounts are not valid.</exception>
    public static ProjectContext CandidateContext(ProjectContext expected, ProjectInfo candidate)
    {
        if (!IsSameProject(expected, candidate))
        {
            throw new InvalidOperationException("A content mount change must stay within the active project.");
        }

        Validate(expected.FileSystem, candidate);
        return ProjectContext.FromProjectInfo(candidate, expected.FileSystem) with
        {
            OpenInitialScene = expected.OpenInitialScene,
            InitialScene = expected.InitialScene,
        };

        static bool IsSameProject(ProjectContext expected, ProjectInfo candidate)
            => candidate.Id == expected.ProjectId
                && candidate.Location is not null
                && FileSystemPath.From(candidate.Location) == expected.ProjectRoot;
    }

    /// <summary>Builds the saved project information a context records.</summary>
    /// <param name="context">The context.</param>
    /// <param name="settings">The project settings.</param>
    /// <returns>The project information.</returns>
    public static ProjectInfo ProjectInfoOf(ProjectContext context, ProjectSettings settings) => new(context.ProjectId, context.Name, context.Category, context.ProjectRoot.FullPath, context.Thumbnail)
    {
        Settings = settings,
        AuthoringMounts = [.. context.AuthoringMounts],
        LocalFolderMounts = [.. context.LocalFolderMounts],
        PakFileMounts = [.. context.PakFileMounts],
        CookedContentOrder = [.. context.CookedContentOrder],
        DefaultSceneId = context.DefaultSceneId,
    };

    /// <summary>Copies the candidate's mounts into the loaded project, which the editor's views read.</summary>
    /// <param name="loaded">The loaded project's information.</param>
    /// <param name="candidate">The accepted candidate.</param>
    public static void UpdateLoadedMounts(IProjectInfo loaded, ProjectInfo candidate)
    {
        ArgumentNullException.ThrowIfNull(loaded);
        ArgumentNullException.ThrowIfNull(candidate);
        if (loaded.Id != candidate.Id)
        {
            return;
        }

        loaded.AuthoringMounts.Clear();
        foreach (var mount in candidate.AuthoringMounts)
        {
            loaded.AuthoringMounts.Add(mount);
        }

        loaded.LocalFolderMounts.Clear();
        foreach (var mount in candidate.LocalFolderMounts)
        {
            loaded.LocalFolderMounts.Add(mount);
        }

        loaded.PakFileMounts.Clear();
        foreach (var mount in candidate.PakFileMounts)
        {
            loaded.PakFileMounts.Add(mount);
        }

        loaded.CookedContentOrder.Clear();
        foreach (var source in candidate.CookedContentOrder)
        {
            loaded.CookedContentOrder.Add(source);
        }
    }

    private static void Validate(IFileSystem fileSystem, ProjectInfo candidate)
    {
        if (HasInvalidAuthoringMountName(candidate) || HasInvalidLibraryMountName(candidate) || HasDuplicateMountNames(candidate))
        {
            throw new InvalidDataException("Content mount names must be unique folder names.");
        }

        if (!HasAuthoringContentFolder(candidate))
        {
            throw new InvalidDataException("The project must keep at least one authoring content folder.");
        }

        foreach (var folder in candidate.LocalFolderMounts)
        {
            if (!fileSystem.Directory.Exists(folder.Root.FullPath))
            {
                throw new DirectoryNotFoundException($"Local folder '{folder.Name}' is unavailable: {folder.Root}.");
            }
        }

        foreach (var pak in candidate.PakFileMounts)
        {
            if (!PakFileMount.IsPakFile(pak.Path.FullPath))
            {
                throw new InvalidDataException($"Pak file mount '{pak.Name}' must name a .pak file: {pak.Path}.");
            }

            if (!fileSystem.File.Exists(pak.Path.FullPath))
            {
                throw new FileNotFoundException($"Pak file '{pak.Name}' is unavailable: {pak.Path}.", pak.Path.FullPath);
            }
        }

        _ = CookedContentOrdering.Resolve(candidate);

        // An authored content folder's mount name must pass the authored virtual path rules; output folders are not
        // authored content and keep their own names.
        static bool HasInvalidAuthoringMountName(ProjectInfo candidate)
            => candidate.AuthoringMounts.Any(static mount => !ProjectOutputFolders.Contains(mount)
                && VirtualPath.ValidateMountName(mount.Name, VirtualPathRuleSet.Authored) is not null);

        // A local folder or pak file mount is named by one folder name: not blank, and without a path separator or drive.
        static bool HasInvalidLibraryMountName(ProjectInfo candidate)
            => LibraryMountNames(candidate).Any(static name => string.IsNullOrWhiteSpace(name) || name.IndexOfAny(['/', '\\', ':']) >= 0);

        // Mount names share one namespace, case-folded as the virtual file system compares them.
        static bool HasDuplicateMountNames(ProjectInfo candidate)
        {
            var names = candidate.AuthoringMounts.Select(static mount => mount.Name).Concat(LibraryMountNames(candidate)).ToArray();
            return names.Distinct(VirtualPathComparer.CaseFolded).Take(names.Length + 1).Count() != names.Length;
        }

        // The project keeps somewhere to author content: a mount outside the pipeline's output folders.
        static bool HasAuthoringContentFolder(ProjectInfo candidate)
            => candidate.AuthoringMounts.Any(static mount => !ProjectOutputFolders.Contains(mount));

        static IEnumerable<string> LibraryMountNames(ProjectInfo candidate)
            => candidate.LocalFolderMounts.Select(static mount => mount.Name).Concat(candidate.PakFileMounts.Select(static mount => mount.Name));
    }
}
