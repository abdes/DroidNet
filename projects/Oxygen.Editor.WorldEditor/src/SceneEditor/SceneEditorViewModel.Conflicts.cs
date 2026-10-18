// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DryIoc;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>Recovers the scene document after a save conflict: reload its file, or save a copy and keep the edits.</summary>
public partial class SceneEditorViewModel
{
    private Task pendingConflict = Task.CompletedTask;
    private bool isResolvingConflict;

    /// <inheritdoc/>
    public bool HasSaveConflict { get; private set; }

    /// <inheritdoc/>
    public string SuggestedCopyName => this.Document.Title + " Copy";

    /// <inheritdoc/>
    public Task<DocumentConflictResult> ReloadFromDiskAsync() => this.ResolveConflictAsync(async () =>
    {
        var result = await this.ReplaceSceneAsync(document => document.ReloadAsync()).ConfigureAwait(true);
        return result.Succeeded
            ? new(Succeeded: true, "Reloaded from disk. Unsaved changes and Undo/Redo history were discarded.")
            : new(Succeeded: false, (result.Problem ?? "The scene could not be reloaded.") + " Your changes are still open.");
    });

    /// <inheritdoc/>
    public Task<DocumentConflictResult> SaveCopyAsync(string name) => this.ResolveConflictAsync(async () =>
    {
        var typed = VirtualPath.NormalizeTypedName(name);
        var file = typed + AssetFileTypes.SceneDescriptor;
        var invalid = VirtualPath.ValidateSegment(file, VirtualPathRuleSet.Authored)?.ToMessage();
        var path = Path.Combine(Path.GetDirectoryName(this.Document.File.Path)!, file);
        var location = invalid is null
            ? this.container.Resolve<IProjectContextService>(IfUnresolved.ReturnDefault)?.ActiveProject?.FindByFilePath(path)
            : null;
        if (location is null)
        {
            return new(Succeeded: false, (invalid ?? "The copy would be outside the project's content folders.") + " Your original document is still open.");
        }

        var result = await this.Document.SaveCopyAsync(new DocumentFile(location.FilePath, location.AssetUri)).ConfigureAwait(true);
        return result.Succeeded
            ? new(Succeeded: true, $"Saved “{typed}” as a separate scene. The original is still unsaved; keep it open or explicitly discard its changes.")
            : new(Succeeded: false, (result.Message ?? "The copy could not be saved.") + " Your original document is still open.");
    });

    // A reload replaces the content: the old authoring cancels its open gestures and takes no further edit.
    private async Task<DocumentReloadResult> ReplaceSceneAsync(Func<Document, Task<DocumentReloadResult>> replace)
        => this.scene is null
            ? new(Succeeded: false, "The scene document is no longer open.")
            : await replace(this.Document).ConfigureAwait(true);

    private Task<DocumentConflictResult> ResolveConflictAsync(Func<Task<DocumentConflictResult>> action)
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
    private async Task<DocumentConflictResult> ResolveConflictCoreAsync(Func<Task<DocumentConflictResult>> action, Task<bool>? priorSave)
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
                : await action().ConfigureAwait(true);
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
