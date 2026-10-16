// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>Read leases: a reader that captures the document's state keeps its content in place until it is done.</summary>
public sealed partial class Document
{
    private readonly object leaseSync = new();

    // Disposes content the lifetime of which outlived the document because a reader still held it.
    private readonly SynchronizationContext? owner = SynchronizationContext.Current;
    private int readLeases;
    private TaskCompletionSource? readsReleased;
    private IDocumentContent? releasedOnLastRead;

    /// <summary>Holds the document's content in place while the caller reads the document's file and state.</summary>
    /// <returns>The lease, whose disposal ends the read; or <see langword="null"/> once the document is closed.</returns>
    /// <remarks>
    /// <para>
    /// While a lease is held, a reload decodes the file but replaces the content only once every lease is released,
    /// so a state captured under the lease stays the state of the content in place.
    /// </para>
    /// <para>
    /// Closing the document does not wait: the content it held is disposed when the last lease is released, on the
    /// thread the document was opened on. A lease may be released from any thread.
    /// </para>
    /// </remarks>
    public IDisposable? AcquireReadLease()
    {
        lock (this.leaseSync)
        {
            if (this.disposed)
            {
                return null;
            }

            this.readLeases++;
        }

        return new ReadLease(this);
    }

    private Task ReadsReleasedAsync()
    {
        lock (this.leaseSync)
        {
            return this.readLeases == 0
                ? Task.CompletedTask
                : (this.readsReleased ??= new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }
    }

    // Disposes the content now, or when the last read lease is released.
    private void DisposeContentAfterReads(IDocumentContent content)
    {
        lock (this.leaseSync)
        {
            if (this.readLeases != 0)
            {
                this.releasedOnLastRead = content;
                return;
            }
        }

        (content as IDisposable)?.Dispose();
    }

    private void ReleaseRead()
    {
        TaskCompletionSource? released;
        IDocumentContent? content;
        lock (this.leaseSync)
        {
            if (--this.readLeases != 0)
            {
                return;
            }

            released = this.readsReleased;
            this.readsReleased = null;
            content = this.releasedOnLastRead;
            this.releasedOnLastRead = null;
        }

        _ = released?.TrySetResult();
        if (content is IDisposable disposable)
        {
            if (this.owner is { } context && SynchronizationContext.Current != context)
            {
                context.Post(static state => ((IDisposable)state!).Dispose(), disposable);
            }
            else
            {
                disposable.Dispose();
            }
        }
    }

    private sealed class ReadLease(Document document) : IDisposable
    {
        private Document? document = document;

        public void Dispose() => Interlocked.Exchange(ref this.document, value: null)?.ReleaseRead();
    }
}
