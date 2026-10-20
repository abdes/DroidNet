// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
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
        if (candidate.Id != expected.ProjectId || candidate.Location is null
            || FileSystemPath.From(candidate.Location) != expected.ProjectRoot)
        {
            throw new InvalidOperationException("A content mount change must stay within the active project.");
        }

        Validate(candidate);
        return ProjectContext.FromProjectInfo(candidate, expected.FileSystem) with
        {
            OpenInitialScene = expected.OpenInitialScene,
            InitialScene = expected.InitialScene,
        };
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

    private static void Validate(ProjectInfo candidate)
    {
        var names = candidate.AuthoringMounts.Select(static mount => mount.Name)
            .Concat(candidate.LocalFolderMounts.Select(static mount => mount.Name))
            .Concat(candidate.PakFileMounts.Select(static mount => mount.Name)).ToArray();
        if (candidate.AuthoringMounts.Any(static mount => !ProjectOutputFolders.Contains(mount) && VirtualPath.ValidateMountName(mount.Name, VirtualPathRuleSet.Authored) is not null)
            || candidate.LocalFolderMounts.Select(static mount => mount.Name).Concat(candidate.PakFileMounts.Select(static mount => mount.Name))
                .Any(static name => string.IsNullOrWhiteSpace(name) || name.IndexOfAny(['/', '\\', ':']) >= 0)
            || names.Distinct(VirtualPathComparer.CaseFolded).Count() != names.Length)
        {
            throw new InvalidDataException("Content mount names must be unique folder names.");
        }

        if (!candidate.AuthoringMounts.Any(static mount => mount.RelativePath.Trim().Replace('\\', '/').Trim('/') is not (".cooked" or ".imported" or ".build")))
        {
            throw new InvalidDataException("The project must keep at least one authoring content folder.");
        }

        foreach (var folder in candidate.LocalFolderMounts)
        {
            if (!Directory.Exists(folder.Root.FullPath))
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

            if (!File.Exists(pak.Path.FullPath))
            {
                throw new FileNotFoundException($"Pak file '{pak.Name}' is unavailable: {pak.Path}.", pak.Path.FullPath);
            }
        }

        _ = CookedContentOrdering.Resolve(candidate);
    }
}
