// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>
/// Names a project's output folders and classifies folders by the output folder rule: any folder below the project
/// root whose name starts with a dot is output, written by the editor and never authored or mounted. The editor
/// writes <see cref="Cooked"/>, <see cref="Build"/> and <see cref="Pipeline"/>; every path under them is built here.
/// </summary>
public static class ProjectOutputFolders
{
    /// <summary>The folder of the editor's publication model: its head, publications and mount generations.</summary>
    public const string Cooked = ".cooked";

    /// <summary>The folder of the editor's working files: cook operations, caches, journals and Play run files.</summary>
    public const string Build = ".build";

    /// <summary>The folder of descriptors the pipeline generates, below a cook operation's inputs.</summary>
    public const string Pipeline = ".pipeline";

    /// <summary>The name of the Content Explorer node that shows <see cref="Cooked"/>; no mount may take it.</summary>
    public const string CookedViewName = "Cooked";

    /// <summary>The name of the Content Explorer node that shows <see cref="Build"/>; no mount may take it.</summary>
    public const string BuildViewName = "Build";

    /// <summary>Gets the name of the Content Explorer node that shows an output folder.</summary>
    /// <param name="folder">The output folder: <see cref="Cooked"/> or <see cref="Build"/>.</param>
    /// <returns>The node's name.</returns>
    /// <exception cref="ArgumentException">The folder is not one the Content Explorer shows.</exception>
    public static string ViewName(string folder) => folder switch
    {
        Cooked => CookedViewName,
        Build => BuildViewName,
        _ => throw new ArgumentException($"The Content Explorer shows only {Cooked} and {Build}, not '{folder}'.", nameof(folder)),
    };

    /// <summary>
    /// Checks whether a project mount lies in an output folder rather than authored content. <c>SourceMedia</c>,
    /// which the virtual path rules also keep out of authored paths, holds sources and is not output.
    /// </summary>
    /// <param name="mount">The project mount.</param>
    /// <returns><see langword="true"/> for a mount in an output folder.</returns>
    public static bool Contains(ProjectMountPoint mount)
    {
        ArgumentNullException.ThrowIfNull(mount);
        return Contains(mount.RelativePath);
    }

    /// <summary>
    /// Checks whether a path relative to the project root lies in an output folder. The path is made canonical
    /// first: <c>.</c> and <c>..</c> are not folder names, so <c>Content/../.cooked/x</c> is output and
    /// <c>../Media</c>, which leaves the project, is not.
    /// </summary>
    /// <param name="projectRelativePath">A folder or file path relative to the project root.</param>
    /// <returns><see langword="true"/> when a folder of the canonical path below the project root starts with a dot.</returns>
    public static bool Contains(string projectRelativePath)
    {
        ArgumentNullException.ThrowIfNull(projectRelativePath);
        var path = projectRelativePath.Trim().Replace('\\', '/');
        if (Path.IsPathRooted(path))
        {
            return false;
        }

        var canonical = new List<string>();
        foreach (var segment in path.Split('/', StringSplitOptions.RemoveEmptyEntries))
        {
            if (string.Equals(segment, ".", StringComparison.Ordinal))
            {
                continue;
            }

            if (!string.Equals(segment, "..", StringComparison.Ordinal))
            {
                canonical.Add(segment);
                continue;
            }

            if (canonical.Count == 0)
            {
                return false;
            }

            canonical.RemoveAt(canonical.Count - 1);
        }

        return canonical.Any(static segment => segment.StartsWith('.'));
    }

    /// <summary>Builds a path under a project's <see cref="Cooked"/> folder.</summary>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="segments">The segments below the folder.</param>
    /// <returns>The combined path.</returns>
    public static string CookedPath(string projectRoot, params string[] segments)
        => Under(projectRoot, Cooked, segments);

    /// <summary>Builds a path under a project's <see cref="Build"/> folder.</summary>
    /// <param name="projectRoot">The project root.</param>
    /// <param name="segments">The segments below the folder.</param>
    /// <returns>The combined path.</returns>
    public static string BuildPath(string projectRoot, params string[] segments)
        => Under(projectRoot, Build, segments);

    /// <summary>Builds a path under the <see cref="Pipeline"/> folder of a cook operation's input root.</summary>
    /// <param name="inputRoot">The operation's input root.</param>
    /// <param name="segments">The segments below the folder.</param>
    /// <returns>The combined path.</returns>
    public static string PipelinePath(string inputRoot, params string[] segments)
        => Under(inputRoot, Pipeline, segments);

    private static string Under(string root, string folder, string[] segments)
    {
        ArgumentException.ThrowIfNullOrEmpty(root);
        ArgumentNullException.ThrowIfNull(segments);
        return Path.Combine([root, folder, .. segments]);
    }
}
