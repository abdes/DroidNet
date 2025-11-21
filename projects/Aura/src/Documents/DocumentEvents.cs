// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI;

#pragma warning disable SA1649 // File name should match first type name
#pragma warning disable SA1402 // File may only contain a single type

namespace DroidNet.Aura.Documents;

/// <summary>
///     Event arguments for <see cref="IDocumentService.DocumentOpened"/>.
/// </summary>
public sealed class DocumentOpenedEventArgs(WindowId windowId, IDocumentMetadata metadata, int indexHint, bool shouldSelect) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window in which the document was opened.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the application-provided metadata for the opened document.
    /// </summary>
    public IDocumentMetadata Metadata { get; } = metadata;

    /// <summary>
    ///     Gets a suggested insertion index for the document within the target window's documents
    ///     collection, or <c>-1</c> when no preference is specified.
    /// </summary>
    public int IndexHint { get; } = indexHint;

    /// <summary>
    ///     Gets a value indicating whether the document should be selected after opening.
    /// </summary>
    public bool ShouldSelect { get; } = shouldSelect;
}

/// <summary>
///     Event arguments used when a document is about to be closed. Handlers may register
///     asynchronous veto tasks using <see cref="AddVetoTask"/> to prevent the close.
/// </summary>
public sealed class DocumentClosingEventArgs(WindowId windowId, IDocumentMetadata metadata, bool force) : EventArgs
{
    private readonly List<Task<bool>> vetoTasks = [];

    /// <summary>
    ///     Gets the ID of the window (in which the document is open) that triggered the close request.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the application-provided document metadata for which the close was requested.
    /// </summary>
    public IDocumentMetadata Metadata { get; } = metadata;

    /// <summary>
    ///     Gets a value indicating whether the close should be forced, bypassing application veto
    ///     handlers when true.
    /// </summary>
    public bool Force { get; } = force;

    /// <summary>
    ///     Adds an asynchronous veto task which can veto the requested close. The task should
    ///     return <see langword="true"/> to approve the close, or <see langword="false"/> to veto.
    ///     Callers that raise this event should await all registered veto tasks and cancel the
    ///     close when any returns <see langword="false"/>.
    /// </summary>
    /// <param name="vetoTask">The asynchronous task that resolves to a bool indicating
    /// approval.</param>
    public void AddVetoTask(Task<bool> vetoTask)
        => this.vetoTasks.Add(vetoTask);

    /// <summary>
    ///     Awaits all registered veto tasks and returns <see langword="true"/> when all
    ///     tasks complete and approve the close. If any task returns <see langword="false"/>
    ///     or throws, the result is <see langword="false"/> indicating the close was vetoed.
    /// </summary>
    /// <param name="cancellationToken">Optional cancellation token to cancel waiting for veto tasks.</param>
    /// <returns><see langword="true"/> if all veto tasks approve the close; otherwise <see langword="false"/>.</returns>
    public async Task<bool> WaitForVetoResultAsync(CancellationToken cancellationToken = default)
    {
        List<Task<bool>> tasks;
        lock (this.vetoTasks)
        {
            tasks = [.. this.vetoTasks];
        }

        if (tasks.Count == 0)
        {
            return true;
        }

        try
        {
            var safeTasks = tasks.Select(t => t.ContinueWith(
                completedTask => completedTask.IsCompletedSuccessfully && completedTask.Result,
                cancellationToken,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default)).ToArray();

            var completed = await Task.WhenAll(safeTasks).WaitAsync(cancellationToken).ConfigureAwait(false);

            // Approve only if all tasks returned true.
            return completed.All(r => r);
        }
        catch (OperationCanceledException)
        {
            return false; // Timeout/cancel -> treat as veto
        }
    }

    /// <summary>
    ///     Internal helper for service implementations to obtain the current veto task results.
    /// </summary>
    /// <returns>An array containing the boolean results from each registered veto task.</returns>
    internal Task<bool[]> GetVetoTasks() => Task.WhenAll(this.vetoTasks);
}

/// <summary>
///     Event arguments raised after a document has been closed.
/// </summary>
public sealed class DocumentClosedEventArgs(WindowId windowId, IDocumentMetadata metadata) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window (in which the document is open) that triggered the close request.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the metadata for the document that was closed.
    /// </summary>
    public IDocumentMetadata Metadata { get; } = metadata;
}

/// <summary>
///     Event arguments raised when a document is detached from a window (for example, the user
///     initiated a tear-out operation).
/// </summary>
public sealed class DocumentDetachedEventArgs(WindowId windowId, IDocumentMetadata metadata) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window (in which the document is attached) that triggered the detach request.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>
    ///     Gets the metadata for the detached document.
    /// </summary>
    public IDocumentMetadata Metadata { get; } = metadata;
}

/// <summary>
///     Event arguments raised when a document is attached to a window (for example, dropped into
///     another window's TabStrip as part of a tear-out attach).
/// </summary>
public sealed class DocumentAttachedEventArgs(WindowId windowId, IDocumentMetadata metadata, int indexHint) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the target window, to which the document was attached.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>Gets the metadata for the attached document.</summary>
    public IDocumentMetadata Metadata { get; } = metadata;

    /// <summary>
    ///     Gets a preferred insertion index for the attached document, or <c>-1</c> when no hint is
    ///     supplied by the application.
    /// </summary>
    public int IndexHint { get; } = indexHint;
}

/// <summary>
///     Event arguments raised when application-provided metadata for a document changes.
/// </summary>
public sealed class DocumentMetadataChangedEventArgs(WindowId windowId, IDocumentMetadata newMetadata) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window (in which the document is open) where the metadata change occurred.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>Gets the updated metadata content.</summary>
    public IDocumentMetadata NewMetadata { get; } = newMetadata;
}

/// <summary>
///     Event arguments raised when the application activates (selects) a document in a window.
/// </summary>
public sealed class DocumentActivatedEventArgs(WindowId windowId, Guid documentId) : EventArgs
{
    /// <summary>
    ///     Gets the ID of the window (in which the document is open) where the activation occurred.
    /// </summary>
    public WindowId WindowId { get; } = windowId;

    /// <summary>Gets the identifier of the document that was activated.</summary>
    public Guid DocumentId { get; } = documentId;
}
