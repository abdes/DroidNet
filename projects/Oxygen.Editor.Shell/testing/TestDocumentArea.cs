// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Shell.Testing;

/// <summary>
/// Stands in for a window's document area: the test says which documents have a tab, which one is active, its editor,
/// and whether saving before an operation goes ahead.
/// </summary>
/// <remarks>
/// Closing through this area only removes the tab; a test that needs the close, open or save flows uses the Shell's area.
/// </remarks>
public sealed class TestDocumentArea : IDocumentArea
{
    private readonly List<Document> documents = [];

    /// <summary>Initializes a new instance of the <see cref="TestDocumentArea"/> class with documents that have a tab.</summary>
    /// <param name="documents">The documents, of which the first becomes active.</param>
    public TestDocumentArea(params Document[] documents)
    {
        this.documents.AddRange(documents);
        this.ActiveDocument = documents.FirstOrDefault();
    }

    /// <inheritdoc/>
    public event EventHandler? ActiveDocumentChanged;

    /// <inheritdoc/>
    public event EventHandler? ActiveEditorChanged;

    /// <inheritdoc/>
    /// <remarks>Never raised: this area does not open documents.</remarks>
    public event EventHandler<DocumentPlacedEventArgs>? Placed
    {
        add { }
        remove { }
    }

    /// <inheritdoc/>
    public IReadOnlyList<Document> Documents => this.documents;

    /// <inheritdoc/>
    public Document? ActiveDocument { get; private set; }

    /// <inheritdoc/>
    public object? ActiveEditor { get; private set; }

    /// <summary>Gets or sets a value indicating whether saving before an operation goes ahead; it does by default.</summary>
    public bool SavesBefore { get; set; } = true;

    /// <summary>Gets the operations that asked to save before they run, in order.</summary>
    public IList<SaveBeforeOperation> SavedBefore { get; } = [];

    /// <summary>Makes an editor the active document's editor.</summary>
    /// <param name="editor">The editor, or <see langword="null"/>.</param>
    public void UseEditor(object? editor)
    {
        this.ActiveEditor = editor;
        this.ActiveEditorChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Gives a document a tab without making it active.</summary>
    /// <param name="document">The document.</param>
    public void Add(Document document)
    {
        if (!this.documents.Contains(document))
        {
            this.documents.Add(document);
        }
    }

    /// <summary>Makes a document active, adding its tab when it has none, or makes no document active.</summary>
    /// <param name="document">The document, or <see langword="null"/>.</param>
    public void Activate(Document? document)
    {
        if (document is not null && !this.documents.Contains(document))
        {
            this.documents.Add(document);
        }

        this.ActiveDocument = document;
        this.ActiveDocumentChanged?.Invoke(this, EventArgs.Empty);
    }

    /// <inheritdoc/>
    public Task<DocumentPlacement> OpenAsync(AssetPath asset, CancellationToken cancellationToken = default)
        => throw new NotSupportedException("Use the Shell's document area to test opening.");

    /// <inheritdoc/>
    public Task<bool> ShowAsync(Document document)
    {
        this.Activate(document);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<bool> SaveAllAsync() => throw new NotSupportedException("Use the Shell's document area to test saving.");

    /// <inheritdoc/>
    public Task<bool> SaveBeforeAsync(
        SaveBeforeOperation operation,
        Func<CancellationToken, Task<IReadOnlyList<UnsavedDocument>>> findUnsaved,
        CancellationToken cancellationToken = default)
    {
        this.SavedBefore.Add(operation);
        return Task.FromResult(this.SavesBefore);
    }

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents) => throw new NotSupportedException("Use the Shell's document area to test closing.");

    /// <inheritdoc/>
    public Task<bool> CloseAsync(Document document, bool force = false)
    {
        _ = this.documents.Remove(document);
        if (ReferenceEquals(this.ActiveDocument, document))
        {
            this.Activate(document: null);
        }

        return Task.FromResult(true);
    }
}
