// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>A model import record of a project, where it lives and the source it names.</summary>
/// <param name="RecordPath">The record's full path.</param>
/// <param name="Settings">The record's content.</param>
/// <param name="SourcePath">The full path of the model's primary source file.</param>
public sealed record ModelImportRecord(string RecordPath, ModelImportSettings Settings, string SourcePath)
{
    /// <summary>Gets the model's identity, which is where the record lives.</summary>
    public AssetPath Identity => this.Settings.Identity;

    /// <summary>Gets the source files that belong to the model: when its source is beside its record with its name, or in a
    /// folder of that name, that source and what it read in its folder. A source anywhere else is only referenced.</summary>
    /// <param name="project">The project.</param>
    /// <returns>The files; empty for a referenced source.</returns>
    public IReadOnlyList<FileSystemPath> CompanionFiles(ProjectContext project)
    {
        ArgumentNullException.ThrowIfNull(project);
        var primary = FileSystemPath.From(this.SourcePath);
        return project.FindByFilePath(FileSystemPath.From(this.RecordPath))?.Path is { } record
            && project.FindByFilePath(primary)?.Path is { } source
            && AssetFileTypes.CompanionsOf(record, source).Contains(source)
                ? [.. this.Settings.FilesRead(primary).Where(file => file.IsSameOrInside(primary.Parent!))]
                : [];
    }
}
