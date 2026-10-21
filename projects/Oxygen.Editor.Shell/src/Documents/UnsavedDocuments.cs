// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The open documents with unsaved changes, which operations that read saved files ask about: the cook, Play and
///     relocations.
/// </summary>
/// <remarks>
///     The documents are followed on the thread that edits them, where it is created; what they report is kept as an
///     list that is replaced, never changed, so any thread reads it.
/// </remarks>
public sealed partial class UnsavedDocuments : IUnsavedDocuments, IDisposable
{
    private readonly OpenDocuments documents;
    private UnsavedDocument[] unsaved = [];
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="UnsavedDocuments"/> class, following the open documents.</summary>
    /// <param name="documents">The open documents of the session.</param>
    public UnsavedDocuments(OpenDocuments documents)
    {
        ArgumentNullException.ThrowIfNull(documents);
        this.documents = documents;
        documents.Opened += this.OnDocumentOpened;
        documents.Closed += this.OnDocumentClosed;
        foreach (var document in documents.Documents)
        {
            this.Follow(document);
        }

        this.Refresh();
    }

    /// <inheritdoc/>
    public event EventHandler? Changed;

    /// <inheritdoc/>
    public IReadOnlyList<UnsavedDocument> Find(IEnumerable<FileSystemPath> paths)
    {
        ArgumentNullException.ThrowIfNull(paths);
        var wanted = paths.ToArray();
        return [.. Volatile.Read(ref this.unsaved).Where(document => wanted.Any(document.File.Path.IsSameOrInside))];
    }

    /// <summary>Stops following the open documents.</summary>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.documents.Opened -= this.OnDocumentOpened;
        this.documents.Closed -= this.OnDocumentClosed;
        foreach (var document in this.documents.Documents)
        {
            this.Unfollow(document);
        }
    }

    private static UnsavedDocument? Describe(Document document)
        => document.IsDirty || document.Content.HasEditsInProgress ? new(document.Id, document.File, document.Title) : null;

    private void Follow(Document document)
    {
        document.StateChanged += this.OnDocumentChanged;
        document.ContentReplaced += this.OnDocumentChanged;
    }

    private void Unfollow(Document document)
    {
        document.StateChanged -= this.OnDocumentChanged;
        document.ContentReplaced -= this.OnDocumentChanged;
    }

    private void OnDocumentOpened(object? sender, DocumentEventArgs args)
    {
        this.Follow(args.Document);
        this.Refresh();
    }

    private void OnDocumentClosed(object? sender, DocumentEventArgs args)
    {
        this.Unfollow(args.Document);
        this.Refresh();
    }

    private void OnDocumentChanged(object? sender, EventArgs args) => this.Refresh();

    private void Refresh()
    {
        var current = this.documents.Documents
            .Select(Describe)
            .OfType<UnsavedDocument>()
            .OrderBy(static document => document.Title, StringComparer.CurrentCultureIgnoreCase)
            .ToArray();
        if (current.SequenceEqual(Volatile.Read(ref this.unsaved)))
        {
            return;
        }

        Volatile.Write(ref this.unsaved, current);
        this.Changed?.Invoke(this, EventArgs.Empty);
    }
}
