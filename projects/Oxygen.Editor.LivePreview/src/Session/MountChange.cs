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
            || !string.Equals(Path.GetFullPath(candidate.Location), Path.GetFullPath(expected.ProjectRoot), StringComparison.OrdinalIgnoreCase))
        {
            throw new InvalidOperationException("A content mount change must stay within the active project.");
        }

        Validate(candidate);
        return ProjectContext.FromProjectInfo(candidate) with
        {
            OpenInitialScene = expected.OpenInitialScene,
            InitialSceneAssetUri = expected.InitialSceneAssetUri,
        };
    }

    /// <summary>Builds the saved project information a context records.</summary>
    /// <param name="context">The context.</param>
    /// <param name="settings">The project settings.</param>
    /// <returns>The project information.</returns>
    public static ProjectInfo ProjectInfoOf(ProjectContext context, ProjectSettings settings) => new(context.ProjectId, context.Name, context.Category, context.ProjectRoot, context.Thumbnail)
    {
        Settings = settings,
        AuthoringMounts = [.. context.AuthoringMounts],
        LocalFolderMounts = [.. context.LocalFolderMounts],
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

        loaded.CookedContentOrder.Clear();
        foreach (var source in candidate.CookedContentOrder)
        {
            loaded.CookedContentOrder.Add(source);
        }
    }

    private static void Validate(ProjectInfo candidate)
    {
        var names = candidate.AuthoringMounts.Select(static mount => mount.Name).Concat(candidate.LocalFolderMounts.Select(static mount => mount.Name)).ToArray();
        if (candidate.AuthoringMounts.Any(static mount => !ProjectOutputFolders.Contains(mount) && VirtualPath.ValidateMountName(mount.Name, VirtualPathRuleSet.Authored) is not null)
            || candidate.LocalFolderMounts.Any(static mount => string.IsNullOrWhiteSpace(mount.Name) || mount.Name.IndexOfAny(['/', '\\', ':']) >= 0)
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
            if (!Path.IsPathFullyQualified(folder.AbsolutePath) || !Directory.Exists(folder.AbsolutePath))
            {
                throw new DirectoryNotFoundException($"Local folder '{folder.Name}' is unavailable: {folder.AbsolutePath}.");
            }
        }

        _ = CookedContentOrdering.Resolve(candidate.LocalFolderMounts, candidate.CookedContentOrder);
    }
}
