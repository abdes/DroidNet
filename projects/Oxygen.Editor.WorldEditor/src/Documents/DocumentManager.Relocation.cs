// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Documents;

/// <summary>
/// Editor-made file changes to open documents. Open documents follow renames and moves in memory as relocation
/// participants; here the tab of a document whose file the editor deleted closes, and a moved material's tab refreshes.
/// </summary>
public sealed partial class DocumentManager
{
    private void RegisterRelocationMessages() => this.relocation.FilesChanged += this.OnFilesChanged;

    private void OnFilesChanged(object? sender, AssetFilesChangedEventArgs args) => _ = this.FollowChangedFilesAsync(args);

    private async Task FollowChangedFilesAsync(AssetFilesChangedEventArgs change)
    {
        if (this.disposed || this.projectContextService.ActiveProject is not { } project)
        {
            return;
        }

        foreach (var metadata in this.documentService.GetOpenDocuments(this.windowId).ToArray())
        {
            if (FilePathOf(project, metadata) is not { } path)
            {
                continue;
            }

            if (change.DeletedFiles.Any(item => string.Equals(path, item, StringComparison.OrdinalIgnoreCase)
                || path.StartsWith(item.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)))
            {
                // Deleting an open asset in the editor closes its document; the delete already asked about it.
                if (await this.documentService.CloseDocumentAsync(this.windowId, metadata.DocumentId, force: true).ConfigureAwait(true))
                {
                    this.LogDeletedDocumentClosed(metadata.Title, path);
                }
                else
                {
                    this.LogDeletedDocumentNotClosed(metadata.Title, path);
                }
            }
            else if (metadata is MaterialDocumentMetadata && change.Moves.Any(move => string.Equals(path, move.Target, StringComparison.OrdinalIgnoreCase)
                || (move.IsDirectory && path.StartsWith(move.Target + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))))
            {
                _ = await this.documentService.UpdateMetadataAsync(this.windowId, metadata.DocumentId, metadata).ConfigureAwait(true);
            }
        }
    }

    // A material tab's identity is its material URI; a scene tab's is its document's file.
    private static string? FilePathOf(ProjectContext project, DroidNet.Documents.IDocumentMetadata metadata)
        => metadata switch
        {
            MaterialDocumentMetadata material => RelocationPaths.ToPhysical(project, Uri.UnescapeDataString(material.MaterialUri.AbsolutePath)),
            SceneDocumentMetadata { Document: { } document } => document.File.Path,
            _ => null,
        };
}
