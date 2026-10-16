// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.ContentPipeline.Snapshots;

/// <summary>
///     Tells the cook about every open document: whether its file has unsaved edits, and a read lease that holds off
///     replacing its content while a cook reads its file.
/// </summary>
/// <remarks>
///     Each document is registered by its file when it opens, follows its file when a relocation moves it, and leaves the
///     registry when it closes. A lease is acquired on the thread that opened the document, where every operation on the
///     document runs.
/// </remarks>
public sealed class OpenDocumentCookInputs : IDisposable
{
    private readonly OpenDocuments documents;
    private readonly ICookDocumentRegistry registry;
    private readonly Dictionary<Document, Registration> registrations = [];
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="OpenDocumentCookInputs"/> class, and registers the documents
    ///     already open.
    /// </summary>
    /// <param name="documents">The open documents of the session.</param>
    /// <param name="registry">The cook's registry of open documents.</param>
    public OpenDocumentCookInputs(OpenDocuments documents, ICookDocumentRegistry registry)
    {
        ArgumentNullException.ThrowIfNull(documents);
        ArgumentNullException.ThrowIfNull(registry);
        this.documents = documents;
        this.registry = registry;
        documents.Opened += this.OnOpened;
        documents.Closed += this.OnClosed;
        foreach (var document in documents.Documents)
        {
            this.Register(document);
        }
    }

    /// <summary>
    ///     Stops following the open documents, and removes them from the registry.
    /// </summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Opened -= this.OnOpened;
        this.documents.Closed -= this.OnClosed;
        foreach (var registration in this.registrations.Values)
        {
            registration.Dispose();
        }

        this.registrations.Clear();
    }

    private static CookDocumentState CaptureState(Document document)
        => new(
            document.File.Path,
            document.Title,
            document.IsDirty || document.Content.HasEditsInProgress,
            document.Version.Sha256);

    private void OnOpened(object? sender, DocumentEventArgs args) => this.Register(args.Document);

    private void OnClosed(object? sender, DocumentEventArgs args)
    {
        if (this.registrations.Remove(args.Document, out var registration))
        {
            registration.Dispose();
        }
    }

    private void Register(Document document)
    {
        if (this.disposed || this.registrations.ContainsKey(document))
        {
            return;
        }

        var registration = new Registration(document, SynchronizationContext.Current);
        registration.Attach(this.registry.Register(document.File.Path, registration.AcquireAsync));
        this.registrations[document] = registration;
    }

    private sealed class Registration(Document document, SynchronizationContext? owner) : IDisposable
    {
        private ICookDocumentRegistration? registration;
        private string sourcePath = document.File.Path;

        public void Attach(ICookDocumentRegistration value)
        {
            this.registration = value;
            document.StateChanged += this.OnDocumentChanged;
            document.ContentReplaced += this.OnDocumentChanged;
            this.registration.UpdateState(CaptureState(document));
        }

        public void Dispose()
        {
            document.StateChanged -= this.OnDocumentChanged;
            document.ContentReplaced -= this.OnDocumentChanged;
            this.registration?.Dispose();
            this.registration = null;
        }

        // The lease is taken on the document's thread, so the state it captures is the content's while the lease holds.
        public async Task<CookDocumentReadLease?> AcquireAsync(CancellationToken cancellationToken)
        {
            if (owner is null || ReferenceEquals(SynchronizationContext.Current, owner))
            {
                return this.Acquire();
            }

            // The cancellation stays registered until the posted acquire runs; a lease acquired after it is released.
            var completion = new TaskCompletionSource<CookDocumentReadLease?>(TaskCreationOptions.RunContinuationsAsynchronously);
            using (cancellationToken.Register(() => completion.TrySetCanceled(cancellationToken)))
            {
                owner.Post(
                    _ =>
                    {
                        var lease = this.Acquire();
                        if (!completion.TrySetResult(lease))
                        {
                            lease?.Dispose();
                        }
                    },
                    state: null);
                return await completion.Task.ConfigureAwait(false);
            }
        }

        private CookDocumentReadLease? Acquire()
            => document.AcquireReadLease() is { } lease ? new CookDocumentReadLease(CaptureState(document), lease.Dispose) : null;

        private void OnDocumentChanged(object? sender, EventArgs args)
        {
            if (this.registration is not { } current)
            {
                return;
            }

            var state = CaptureState(document);
            if (string.Equals(this.sourcePath, state.SourcePath, StringComparison.OrdinalIgnoreCase))
            {
                current.UpdateState(state);
                return;
            }

            current.RelocateSource(state);
            this.sourcePath = state.SourcePath;
        }
    }
}
