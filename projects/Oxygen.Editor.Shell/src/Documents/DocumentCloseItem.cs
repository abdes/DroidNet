// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Documents;

/// <summary>An unsaved document that a close asks about: save it, or discard its changes.</summary>
/// <param name="tab">The document's tab.</param>
/// <param name="save">Saves the document and reports failures; returns whether it saved.</param>
/// <param name="documents">The document service, which reloads the document or saves a copy when its file changed on disk.</param>
public sealed class DocumentCloseItem(DocumentTab tab, Func<Task<bool>> save, IDocumentService? documents = null)
{
    private Task pending = Task.CompletedTask;

    /// <summary>Gets the document's tab.</summary>
    public DocumentTab Tab { get; } = tab;

    /// <summary>Gets or sets a value indicating whether to save this document. The changes of an unselected document are discarded.</summary>
    public bool IsSelected { get; set; } = true;

    /// <summary>Gets a value indicating whether the selected save has succeeded.</summary>
    public bool IsSaved { get; private set; }

    /// <summary>Gets a value indicating whether the document's file changed on disk since the document read it.</summary>
    public bool HasConflict => this.Tab.Document.IsChangedOnDisk;

    /// <summary>Gets the proposed name for a copy.</summary>
    public string SuggestedCopyName => this.Tab.Title + " Copy";

    /// <summary>Gets the completion of every save and conflict action submitted so far.</summary>
    public Task Pending => this.pending;

    /// <summary>Saves the document; a save that succeeded is not repeated while the document stays clean.</summary>
    /// <returns><see langword="true"/> when the document is saved.</returns>
    public Task<bool> SaveAsync() => this.EnqueueAsync(this.SaveCoreAsync);

    /// <summary>Discards the document's changes and reads its file again, once the user confirmed the discard.</summary>
    /// <returns>The outcome.</returns>
    public Task<DocumentConflictResult> ReloadAsync() => this.EnqueueAsync(async () =>
    {
        var result = documents is null ? new DocumentConflictResult(Succeeded: false, "Reload is unavailable.")
            : await documents.ReloadAsync(this.Tab.Document).ConfigureAwait(true);
        this.IsSaved = result.Succeeded && !this.Tab.IsDirty;
        return result;
    });

    /// <summary>Saves the document's content as a new file, leaving the document unsaved.</summary>
    /// <param name="name">The copy's name, without its suffix.</param>
    /// <returns>The outcome.</returns>
    public Task<DocumentConflictResult> SaveCopyAsync(string name) => this.EnqueueAsync(() => documents is null
        ? Task.FromResult(new DocumentConflictResult(Succeeded: false, "Save a Copy is unavailable."))
        : documents.SaveCopyAsync(this.Tab.Document, name));

    private static async Task<T> RunAfterAsync<T>(Task previous, Func<Task<T>> action)
    {
        await previous.ConfigureAwait(true);
        return await action().ConfigureAwait(true);
    }

    private Task<T> EnqueueAsync<T>(Func<Task<T>> action)
    {
        var operation = RunAfterAsync(this.pending, action);

        // The caller observes the operation's result. The tail only tracks completion,
        // allowing retries after a failed action while keeping close behind pending I/O.
        this.pending = operation.ContinueWith(static completed => _ = completed.Exception, CancellationToken.None, TaskContinuationOptions.ExecuteSynchronously, TaskScheduler.Default);
        return operation;
    }

    private async Task<bool> SaveCoreAsync()
    {
        if (!this.IsSaved || this.Tab.IsDirty)
        {
            this.IsSaved = await save().ConfigureAwait(true);
        }

        return this.IsSaved;
    }
}
