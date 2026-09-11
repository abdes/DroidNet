// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Documents;

/// <summary>A dirty document presented for an explicit save or discard decision.</summary>
/// <param name="metadata">The document metadata.</param>
/// <param name="save">The operation that saves and reports failures.</param>
/// <param name="conflicts">Optional authoring operations for external-file conflicts.</param>
public sealed class DocumentCloseItem(IDocumentMetadata metadata, Func<Task<bool>> save, IDocumentConflictParticipant? conflicts = null)
{
    private Task pending = Task.CompletedTask;

    /// <summary>Gets the document metadata.</summary>
    public IDocumentMetadata Metadata { get; } = metadata;

    /// <summary>Gets or sets a value indicating whether to save this document. Unselected documents will be discarded.</summary>
    public bool IsSelected { get; set; } = true;

    /// <summary>Gets a value indicating whether the selected save has succeeded.</summary>
    public bool IsSaved { get; private set; }

    /// <summary>Gets a value indicating whether this document has an unresolved save conflict.</summary>
    public bool HasConflict => conflicts?.HasSaveConflict == true;

    /// <summary>Gets the proposed name for a new copy.</summary>
    public string SuggestedCopyName => conflicts?.SuggestedCopyName ?? (this.Metadata.Title + " Copy");

    /// <summary>Gets completion of all submitted save and conflict actions.</summary>
    public Task Pending => this.pending;

    /// <summary>Saves the document, retaining successful saves across retries.</summary>
    /// <returns>True when the document is saved.</returns>
    public Task<bool> SaveAsync() => this.EnqueueAsync(this.SaveCoreAsync);

    /// <summary>Reloads after explicit discard confirmation and records whether this document can close.</summary>
    /// <returns>The reload result.</returns>
    public Task<DocumentConflictResult> ReloadAsync() => this.EnqueueAsync(async () =>
    {
        var result = conflicts is null ? new DocumentConflictResult(Succeeded: false, "Reload is unavailable.")
            : await conflicts.ReloadFromDiskAsync().ConfigureAwait(true);
        this.IsSaved = result.Succeeded && !this.Metadata.IsDirty;
        return result;
    });

    /// <summary>Saves a copy without acknowledging or discarding changes in the original.</summary>
    /// <param name="name">The distinct authored file-name stem.</param>
    /// <returns>The copy result.</returns>
    public Task<DocumentConflictResult> SaveCopyAsync(string name) => this.EnqueueAsync(() => conflicts is null
        ? Task.FromResult(new DocumentConflictResult(Succeeded: false, "Save Copy is unavailable."))
        : conflicts.SaveCopyAsync(name));

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
        if (!this.IsSaved || this.Metadata.IsDirty)
        {
            this.IsSaved = await save().ConfigureAwait(true);
        }

        return this.IsSaved;
    }
}
