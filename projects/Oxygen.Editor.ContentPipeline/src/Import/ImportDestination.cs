// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Where an import puts what it creates: always in one of the project's own mounts.</summary>
public static class ImportDestination
{
    /// <summary>Reads a typed destination, such as <c>Content/Models</c> or <c>/Content/Models/</c>.</summary>
    /// <param name="folder">The typed folder.</param>
    /// <returns>The folder.</returns>
    /// <exception cref="ArgumentException">The text names no folder.</exception>
    public static AssetPath Parse(string folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        return AssetPath.Parse("/" + folder.Trim().Trim('/'));
    }

    /// <summary>Tests whether a source is in one of the project's source mounts, so a model references it where it is
    /// instead of copying it in.</summary>
    /// <param name="project">The project.</param>
    /// <param name="sourcePath">The source's full path.</param>
    /// <returns>Whether a source mount holds the file.</returns>
    public static bool IsMounted(ProjectContext project, string sourcePath)
    {
        ArgumentNullException.ThrowIfNull(project);
        return FindMount(project, FileSystemPath.From(Path.GetFullPath(sourcePath))) is not null;
    }

    /// <summary>Gets the identity a new model takes. A source in a mount stays there, so the model is named in the chosen
    /// folder; a source from outside is copied in with its files, so the model is its file in a folder of its name.</summary>
    /// <param name="project">The project.</param>
    /// <param name="sourcePath">The source's full path.</param>
    /// <param name="name">The model's name.</param>
    /// <param name="folder">The folder the user chose.</param>
    /// <returns>The model's identity.</returns>
    /// <exception cref="ArgumentException">The folder is not in one of the project's own mounts, or the name is invalid.</exception>
    public static AssetPath IdentityOf(ProjectContext project, string sourcePath, string name, AssetPath folder)
    {
        ArgumentNullException.ThrowIfNull(folder);
        _ = ImportSourceRetention.ResolveDestination(project, folder, name);
        var file = Path.GetFileName(sourcePath);
        return IsMounted(project, sourcePath) ? folder.Combine(name + Path.GetExtension(file)) : folder.Combine(name + "/" + file);
    }

    /// <summary>Gets the folder an import starts from: the selected one in an own mount, else the first own mount.</summary>
    /// <param name="project">The project.</param>
    /// <param name="selected">The selected folder.</param>
    /// <returns>The folder; <paramref name="selected"/> when the project has no own mount.</returns>
    public static AssetPath DefaultFolder(ProjectContext project, AssetPath selected)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(selected);
        var own = project.SourceMounts.Where(static mount => mount.Role == MountRole.Own).ToArray();
        return own.Any(mount => string.Equals(mount.Name, selected.Mount, StringComparison.Ordinal)) || own.Length == 0
            ? selected
            : AssetPath.Parse("/" + own[0].Name);
    }

    /// <summary>Gets the folder of a source in an own mount, where importing it makes it the asset's companion.</summary>
    /// <param name="project">The project.</param>
    /// <param name="sourcePath">The source's full path.</param>
    /// <returns>The folder, or null for a source anywhere else.</returns>
    public static AssetPath? OwnFolderOf(ProjectContext project, string sourcePath)
    {
        ArgumentNullException.ThrowIfNull(project);
        var file = FileSystemPath.From(Path.GetFullPath(sourcePath));
        return FindMount(project, file) is { Role: MountRole.Own } mount ? AssetPath.Create(mount.Name, file.GetPathBelow(mount.Location!)).Parent : null;
    }

    // Source mounts never overlap, so at most one holds the file.
    private static ResolvedMount? FindMount(ProjectContext project, FileSystemPath file)
        => project.SourceMounts.FirstOrDefault(mount => file != mount.Location && file.IsSameOrInside(mount.Location!));
}
