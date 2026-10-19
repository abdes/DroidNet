// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Projects;

/// <summary>Resolves persisted content priority independently of scan, cook or load completion timing.</summary>
public static class CookedContentOrdering
{
    /// <summary>Resolves a project manifest's mount order.</summary>
    /// <param name="project">The project manifest.</param>
    /// <returns>A complete mount order, lowest priority first.</returns>
    /// <exception cref="InvalidDataException">The manifest's libraries or saved order are invalid.</exception>
    public static IReadOnlyList<CookedContentSource> Resolve(IProjectInfo project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return Resolve(project.LocalFolderMounts, project.PakFileMounts, project.CookedContentOrder);
    }

    /// <summary>Resolves an open project's mount order.</summary>
    /// <param name="project">The open project.</param>
    /// <returns>A complete mount order, lowest priority first.</returns>
    /// <exception cref="InvalidDataException">The project's libraries or saved order are invalid.</exception>
    public static IReadOnlyList<CookedContentSource> Resolve(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        return Resolve(project.LocalFolderMounts, project.PakFileMounts, project.CookedContentOrder);
    }

    /// <summary>Expands the saved order with newly declared libraries directly below project output.</summary>
    /// <param name="folders">Local folders in declaration order, oldest first.</param>
    /// <param name="paks">Pak files in declaration order, oldest first.</param>
    /// <param name="savedOrder">Explicit source priority in mount order, lowest first.</param>
    /// <returns>A complete mount order; callers omit folders that do not contain cooked content.</returns>
    /// <exception cref="InvalidDataException">A library has no name, two libraries share a name, or the saved order names a duplicate or undeclared source.</exception>
    public static IReadOnlyList<CookedContentSource> Resolve(IEnumerable<LocalFolderMount> folders, IEnumerable<PakFileMount> paks, IEnumerable<CookedContentSource> savedOrder)
    {
        ArgumentNullException.ThrowIfNull(folders);
        ArgumentNullException.ThrowIfNull(paks);
        ArgumentNullException.ThrowIfNull(savedOrder);
        var libraries = folders.Select(static folder => new CookedContentSource(CookedContentSourceKind.LocalFolder, folder?.Name))
            .Concat(paks.Select(static pak => new CookedContentSource(CookedContentSourceKind.PakFile, pak?.Name)))
            .ToArray();
        if (libraries.Any(static library => string.IsNullOrWhiteSpace(library.Name)))
        {
            throw new InvalidDataException("Content priority requires named library declarations.");
        }

        var declared = new Dictionary<string, CookedContentSource>(StringComparer.Ordinal);
        foreach (var library in libraries)
        {
            if (!declared.TryAdd(library.Name!, library))
            {
                throw new InvalidDataException("Two libraries share a mount name.");
            }
        }

        var order = new List<CookedContentSource>();
        var included = new HashSet<string>(StringComparer.Ordinal);
        var projectIndex = -1;
        foreach (var source in savedOrder)
        {
            if (source is null)
            {
                throw new InvalidDataException("Content priority contains an empty source entry.");
            }

            if (source.Kind == CookedContentSourceKind.ProjectOutput && source.Name is null && projectIndex < 0)
            {
                projectIndex = order.Count;
                order.Add(source);
            }
            else if (source.Name is { } name && declared.TryGetValue(name, out var library)
                && library.Kind == source.Kind && included.Add(name))
            {
                order.Add(library);
            }
            else
            {
                throw new InvalidDataException("Content priority contains a duplicate, invalid or undeclared source.");
            }
        }

        if (projectIndex < 0)
        {
            projectIndex = order.Count;
            order.Add(new(CookedContentSourceKind.ProjectOutput));
        }

        foreach (var library in libraries.Where(library => !included.Contains(library.Name!)))
        {
            order.Insert(projectIndex++, library);
        }

        return order;
    }
}
