// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>The files documents edit, in the content folders of a project session's project.</summary>
/// <param name="session">The project session, whose content mounts are read at each call.</param>
public sealed class ProjectDocumentFiles(IProjectSession session) : IDocumentFiles
{
    /// <inheritdoc/>
    /// <remarks>Only assets authored in the project's own content folders have a document file. A model's document opens
    /// on its source, which its record names, or else on the file at its identity.</remarks>
    public DocumentFile? Find(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        var project = session.Context;
        if (AssetFileTypes.Is(AssetFileTypes.ToIdentity(asset).Name, AssetFileCategory.Model))
        {
            var model = AssetFileTypes.ToIdentity(asset);
            return Import.ModelRecordLookup.TryFind(project, model) is { } record
                ? new DocumentFile(FileSystemPath.From(record.SourcePath), model)
                : project.Find(model) is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } file ? new DocumentFile(file.File, model) : null;
        }

        return project.LocateSource(asset) is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location
            ? new DocumentFile(location.File, location.Identity)
            : null;
    }

    /// <inheritdoc/>
    public DocumentFile? Find(FileSystemPath path)
    {
        ArgumentNullException.ThrowIfNull(path);
        return session.Context.FindByFilePath(path) is { IsMountRoot: false } location
            ? new DocumentFile(location.File, location.Identity)
            : null;
    }
}
