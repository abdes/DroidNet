// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>The files documents edit, in the content folders of a project session's project.</summary>
/// <param name="session">The project session, whose content mounts are read at each call.</param>
public sealed class ProjectDocumentFiles(IProjectSession session) : IDocumentFiles
{
    /// <inheritdoc/>
    /// <remarks>Only assets authored in the project's own content folders have a document file.</remarks>
    public DocumentFile? Find(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return session.Context.LocateSource(asset) is { MountKind: ProjectMountKind.Authoring, IsMountRoot: false } location
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
