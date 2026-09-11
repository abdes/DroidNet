// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Messages;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Coordinates inline recovery with the owning scene document and its active presentation.</summary>
public partial class SceneEditorViewModel
{
    private Task pendingConflict = Task.CompletedTask;
    private bool isResolvingConflict;

    /// <inheritdoc/>
    public bool HasSaveConflict { get; private set; }

    /// <inheritdoc/>
    public string SuggestedCopyName => (this.scene?.Name ?? this.Metadata.Title) + " Copy";

    /// <inheritdoc/>
    public Task<DocumentConflictResult> ReloadFromDiskAsync() => this.ResolveConflictAsync(async context =>
    {
        var result = await this.commandService.ReloadSceneAsync(context, CancellationToken.None).ConfigureAwait(true);
        if (!result.Succeeded || result.Value is not { } replacement)
        {
            return new(Succeeded: false, result.FailureMessage ?? "The scene could not be reloaded. Your changes are still open.");
        }

        this.scene = replacement;
        this.sceneReady = false;
        this.HasSaveConflict = false;
        if (!this.isDisposed && this.documentService.GetActiveDocumentId(this.windowId) == this.Metadata.DocumentId)
        {
            _ = this.messenger.Send(new SceneAuthoringLoadedMessage(replacement, this.Metadata));
            var message = this.messenger.Send(new SceneReloadedMessage(replacement, this.Metadata, this.windowId));
            if (message.HasReceivedResponse && !await message.Response.ConfigureAwait(true))
            {
                return new(Succeeded: true, "Reloaded from disk. The scene tree could not be refreshed; reopen the tab to refresh its display.");
            }
        }

        return new(Succeeded: true, "Reloaded from disk. Unsaved changes and Undo/Redo history were discarded.");
    });

    /// <inheritdoc/>
    public Task<DocumentConflictResult> SaveCopyAsync(string name) => this.ResolveConflictAsync(async context =>
    {
        var result = await this.commandService.SaveSceneCopyAsync(context, name.Trim()).ConfigureAwait(true);
        return result.Succeeded ? new(Succeeded: true, $"Saved “{result.Value!.Name}” as a separate scene. The original is still unsaved; keep it open or explicitly discard its changes.")
            : new(Succeeded: false, result.FailureMessage ?? "The copy could not be saved. Your original document is still open.");
    });

    private Task<DocumentConflictResult> ResolveConflictAsync(Func<SceneDocumentCommandContext, Task<DocumentConflictResult>> action)
    {
        if (this.isDisposed || this.isResolvingConflict)
        {
            return Task.FromResult(new DocumentConflictResult(Succeeded: false, "The document is unavailable or another recovery action is running."));
        }

        var operation = this.ResolveConflictCoreAsync(action, this.pendingSave);
        this.pendingConflict = operation;
        return operation;
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Design", "CA1031:Do not catch general exception types", Justification = "Recovery failures are returned to the inline prompt while the document stays owned by this editor.")]
    private async Task<DocumentConflictResult> ResolveConflictCoreAsync(Func<SceneDocumentCommandContext, Task<DocumentConflictResult>> action, Task<bool>? priorSave)
    {
        this.isResolvingConflict = true;
        try
        {
            if (priorSave is not null)
            {
                _ = await priorSave.ConfigureAwait(true);
            }

            await this.inputCommitter.CommitAsync(this.windowId).ConfigureAwait(true);
            return this.isDisposed || this.scene is null
                ? new(Succeeded: false, "The scene document is no longer open.")
                : await action(this.CreateCommandContext()).ConfigureAwait(true);
        }
        catch (Exception exception)
        {
            return new(Succeeded: false, $"Recovery failed. Your document is still open. {exception.Message}");
        }
        finally
        {
            this.isResolvingConflict = false;
        }
    }
}
