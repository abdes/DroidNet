// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>
/// Stands in for a window's document area: the test says which documents have a tab and which one is active.
/// </summary>
/// <remarks>Closing through this area only removes the tab; a test that needs the close flow uses the Shell's area.</remarks>
internal sealed class TestDocumentArea : IDocumentArea
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
    public IReadOnlyList<Document> Documents => this.documents;

    /// <inheritdoc/>
    public Document? ActiveDocument { get; private set; }

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
    public Task<bool> ShowAsync(Document document)
    {
        this.Activate(document);
        return Task.FromResult(true);
    }

    /// <inheritdoc/>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents) => throw new NotSupportedException("Use the Shell's document area to test closing.");

    /// <inheritdoc/>
    public Task<bool> CloseAsync(Document document, bool force = false)
    {
        _ = this.documents.Remove(document);
        if (ReferenceEquals(this.ActiveDocument, document))
        {
            this.Activate(null);
        }

        return Task.FromResult(true);
    }
}
