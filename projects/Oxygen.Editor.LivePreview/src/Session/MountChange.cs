// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.IO.Abstractions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
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
        Mounts = [.. context.DeclaredMounts],
        OutputFolderViews = [.. context.OutputFolderViews],
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

        loaded.Mounts.Clear();
        foreach (var mount in candidate.Mounts)
        {
            loaded.Mounts.Add(mount);
        }

        loaded.OutputFolderViews.Clear();
        foreach (var view in candidate.OutputFolderViews)
        {
            loaded.OutputFolderViews.Add(view);
        }

        loaded.CookedContentOrder.Clear();
        foreach (var source in candidate.CookedContentOrder)
        {
            loaded.CookedContentOrder.Add(source);
        }
    }

    private static void Validate(IFileSystem fileSystem, ProjectInfo candidate)
    {
        if (candidate.OutputFolderViews.Any(static view => view.Folder is not (ProjectOutputFolders.Cooked or ProjectOutputFolders.Build))
            || candidate.OutputFolderViews.DistinctBy(static view => view.Folder, StringComparer.Ordinal).Take(candidate.OutputFolderViews.Count + 1).Count() != candidate.OutputFolderViews.Count)
        {
            throw new InvalidDataException($"The Content Explorer shows {ProjectOutputFolders.Cooked} and {ProjectOutputFolders.Build}, each once.");
        }

        var table = ProjectMounts.Resolve(fileSystem, FileSystemPath.From(candidate.Location!), candidate.Mounts);
        var errors = table.Findings.Where(static finding => finding.IsError).Select(static error => $"{error.Code}: {error.Message}").ToArray();
        if (errors.Length > 0)
        {
            throw new InvalidDataException(string.Join(Environment.NewLine, errors));
        }

        _ = CookedContentOrdering.Resolve(candidate);
    }
}
