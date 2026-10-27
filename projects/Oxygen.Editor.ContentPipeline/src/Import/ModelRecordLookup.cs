// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Finds the model import record of a project's model source.</summary>
public static class ModelRecordLookup
{
    /// <summary>Gets the record that names <paramref name="source"/>.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="source">The model's primary source file.</param>
    /// <returns>The record.</returns>
    /// <exception cref="InvalidDataException">No record names the model: it was never imported.</exception>
    public static ModelImportRecord Find(ProjectContext project, FileSystemPath source)
        => TryFind(project, source) ?? throw new InvalidDataException($"The model '{source.FullPath}' has no import record. Import it first.");

    /// <summary>Gets the record that names <paramref name="source"/>, if any.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="source">The model's primary source file.</param>
    /// <returns>The record, or <see langword="null"/> when the model is not imported.</returns>
    public static ModelImportRecord? TryFind(ProjectContext project, FileSystemPath source)
    {
        ArgumentNullException.ThrowIfNull(project);
        return new ModelImportRecords(project.FileSystem, project.ProjectRoot.FullPath).TryFindBySource(source.FullPath, out var record)
            ? record : null;
    }

    /// <summary>Gets the record of a model of the project, by the model's asset.</summary>
    /// <param name="project">The model's project.</param>
    /// <param name="model">The model's source, such as <c>/Content/Vehicles/Car.glb</c>.</param>
    /// <returns>The record, or <see langword="null"/> when the asset is no imported model file of the project.</returns>
    public static ModelImportRecord? TryFind(ProjectContext project, AssetPath model)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(model);
        return project.FindSource(model) is { IsProjectFile: true } location ? TryFind(project, location.File) : null;
    }

    /// <summary>Gets a record's path relative to its project, as cook inputs name files.</summary>
    /// <param name="project">The record's project.</param>
    /// <param name="record">The record.</param>
    /// <returns>The project-relative path, with <c>/</c> separators.</returns>
    public static string RelativePath(ProjectContext project, ModelImportRecord record)
    {
        ArgumentNullException.ThrowIfNull(project);
        ArgumentNullException.ThrowIfNull(record);
        return FileSystemPath.From(record.RecordPath).GetPathBelow(project.ProjectRoot);
    }
}
