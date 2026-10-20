// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>
/// Finds and reads the input sources of a project.
/// </summary>
/// <remarks>
/// A product is named under its source (<c>/Content/Input/Player.input/Jump.oiact</c> is declared by
/// <c>/Content/Input/Player.input.json</c>), so its source follows from its identity; nothing scans a folder.
/// </remarks>
public static class InputSourceFiles
{
    /// <summary>Gets a value indicating whether a virtual path is in one of the project's authoring mounts, where the
    /// project's own input sources are, rather than in a library or the pipeline's output.</summary>
    /// <param name="project">The project.</param>
    /// <param name="path">The virtual path.</param>
    /// <returns><see langword="true"/> for a path the project authors.</returns>
    public static bool IsAuthored(ProjectContext project, AssetPath path)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(path);
        return project.AuthoringMounts.Any(mount => string.Equals(mount.Name, path.Mount, StringComparison.Ordinal) && !ProjectOutputFolders.Contains(mount));
    }

    /// <summary>Gets the file of an input source of the project, whether or not it exists.</summary>
    /// <param name="project">The project.</param>
    /// <param name="container">The source's identity, such as <c>/Content/Input/Player.input</c>.</param>
    /// <returns>The file, or <see langword="null"/> when the identity is not in an authoring mount of the project.</returns>
    public static FileSystemPath? FileOf(ProjectContext project, AssetPath container)
    {
        ArgumentNullException.ThrowIfNull(container);
        return IsAuthored(project, container) ? project.Find(container.WithAuthoringSuffix())?.File : null;
    }

    /// <summary>Reads an input source of the project the way the cooker will.</summary>
    /// <param name="project">The project.</param>
    /// <param name="container">The source's identity.</param>
    /// <returns>The source, or <see langword="null"/> when it does not exist, cannot be read or does not match the
    /// engine's input schema.</returns>
    public static InputContainer? Read(ProjectContext project, AssetPath container)
    {
        if (FileOf(project, container) is not { } file)
        {
            return null;
        }

        try
        {
            return InputSourceReader.Check(project.FileSystem.File.ReadAllBytes(file.FullPath)).Source is { } source
                ? new InputContainer(container, source)
                : null;
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException)
        {
            return null;
        }
    }
}
