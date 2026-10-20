// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Documents;
using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The tab of an open document: a view of the document's state that nobody updates by hand.
/// </summary>
/// <remarks>
///     The tab follows <see cref="Document.StateChanged"/> and <see cref="Document.ContentReplaced"/> and raises
///     <see cref="PropertyChanged"/> on the thread that raised them, which is the thread that owns the document. The
///     document area creates the tab when it shows the document and disposes it when the tab closes.
/// </remarks>
public sealed partial class DocumentTab : IDocumentMetadata, IDisposable
{
    private string title;
    private bool isDirty;
    private bool disposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DocumentTab"/> class.
    /// </summary>
    /// <param name="document">The document the tab shows.</param>
    /// <param name="editor">The editor descriptor of the document's kind, which says how the tab looks.</param>
    public DocumentTab(Document document, DocumentEditorDescriptor editor)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentNullException.ThrowIfNull(editor);
        this.Document = document;
        this.Editor = editor;
        this.IsClosable = editor.IsClosable;
        this.IconUri = editor.IconUri;
        this.title = document.Title;
        this.isDirty = document.IsDirty;
        document.StateChanged += this.OnDocumentChanged;
        document.ContentReplaced += this.OnDocumentChanged;
    }

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    ///     Gets the document the tab shows.
    /// </summary>
    public Document Document { get; }

    /// <summary>
    ///     Gets the editor descriptor of the document's kind, which creates the editor that shows the tab.
    /// </summary>
    public DocumentEditorDescriptor Editor { get; }

    /// <inheritdoc/>
    public DocumentId DocumentId => this.Document.Id;

    /// <inheritdoc/>
    public string Title => this.title;

    /// <inheritdoc/>
    public Uri? IconUri { get; }

    /// <inheritdoc/>
    /// <remarks>A document whose file is missing on disk is dirty.</remarks>
    public bool IsDirty => this.isDirty;

    /// <inheritdoc/>
    public bool IsClosable { get; }

    /// <inheritdoc/>
    public bool IsPinnedHint => false;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.Document.StateChanged -= this.OnDocumentChanged;
        this.Document.ContentReplaced -= this.OnDocumentChanged;
    }

    private void OnDocumentChanged(object? sender, EventArgs args)
    {
        if (this.disposed)
        {
            return;
        }

        if (!string.Equals(this.title, this.Document.Title, StringComparison.Ordinal))
        {
            this.title = this.Document.Title;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.Title)));
        }

        if (this.isDirty != this.Document.IsDirty)
        {
            this.isDirty = this.Document.IsDirty;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.IsDirty)));
        }
    }
}
