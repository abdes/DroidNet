// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Documents;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Marks an open scene when its file is deleted or moved outside the editor, and clears the mark when it returns.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc />
    public async Task<bool> RefreshSourcePresenceAsync(SceneDocumentCommandContext context, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(context);
        if (SceneAuthoringGate.IsRetired(context.Scene))
        {
            return context.Metadata.IsMissingOnDisk;
        }

        var missing = await this.projectManager.RefreshSceneSourcePresenceAsync(context.Scene, cancellationToken).ConfigureAwait(true);
        if (missing != context.Metadata.IsMissingOnDisk)
        {
            context.Metadata.IsMissingOnDisk = missing;
            _ = await this.documentService.UpdateMetadataAsync(this.windowId, context.DocumentId, context.Metadata).ConfigureAwait(true);
        }

        return missing;
    }
}
