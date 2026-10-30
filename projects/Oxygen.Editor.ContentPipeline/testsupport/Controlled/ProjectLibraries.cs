// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>Adds cooked and pak libraries to a test project the way a machine binds them: a declared placeholder and a <c>.mounts.json</c>.</summary>
internal static class ProjectLibraries
{
    /// <summary>Adds cooked folder libraries, replacing the libraries of the same names.</summary>
    /// <param name="context">The project.</param>
    /// <param name="libraries">Each library's mount name and absolute folder.</param>
    /// <returns>The project with the libraries bound.</returns>
    public static ProjectContext WithCookedLibraries(this ProjectContext context, params (string Name, string Folder)[] libraries)
        => With(context, MountKind.Cooked, libraries);

    /// <summary>Adds pak libraries, replacing the libraries of the same names.</summary>
    /// <param name="context">The project.</param>
    /// <param name="libraries">Each library's mount name and absolute pak file.</param>
    /// <returns>The project with the libraries bound.</returns>
    public static ProjectContext WithPakLibraries(this ProjectContext context, params (string Name, string File)[] libraries)
        => With(context, MountKind.Pak, libraries);

    private static ProjectContext With(ProjectContext context, MountKind kind, (string Name, string Location)[] libraries)
    {
        var names = libraries.Select(static library => library.Name).ToHashSet(StringComparer.Ordinal);
        var declared = context.DeclaredMounts.Where(mount => !names.Contains(mount.Name))
            .Concat(libraries.Select(library => new ProjectMount(library.Name, kind))).ToArray();
        var bindings = context.LibraryMounts.Where(mount => mount.Location is not null && mount.DeclaredPath is null && !names.Contains(mount.Name)).Select(static mount => (mount.Name, Location: mount.Location!.FullPath))
            .Concat(libraries)
            .ToDictionary(static binding => binding.Name, static binding => binding.Location.Replace('\\', '/'), StringComparer.Ordinal);
        var mountsFile = context.ProjectRoot.Combine(ProjectMounts.FileName).FullPath;
        context.FileSystem.File.WriteAllText(mountsFile, JsonSerializer.Serialize(new { version = 1, mounts = bindings }));
        var table = ProjectMounts.Resolve(context.FileSystem, context.ProjectRoot, declared);
        return context with { Mounts = table.Mounts, MountFindings = table.Findings };
    }
}
