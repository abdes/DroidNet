// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
///     The document area of the window a part runs in: the documents that have a tab there, and which one is active.
/// </summary>
/// <remarks>
///     A part gets the area of its own window from its scope and never names the window. A document's tab is in at most
///     one area. The area closes a document in <see cref="OpenDocuments"/> once its tab has closed; editors never do.
///     A tab does not move to another window: that needs the Shell's document windows.
/// </remarks>
public interface IDocumentArea
{
    /// <summary>
    ///     Raised on the UI thread after <see cref="ActiveDocument"/> changes.
    /// </summary>
    public event EventHandler? ActiveDocumentChanged;

    /// <summary>
    ///     Gets the documents that have a tab in this area.
    /// </summary>
    public IReadOnlyList<Document> Documents { get; }

    /// <summary>
    ///     Gets the document whose tab is selected, or <see langword="null"/> when no document's tab is.
    /// </summary>
    public Document? ActiveDocument { get; }

    /// <summary>
    ///     Selects the document's tab and brings its window forward, adding the tab here first when the document has
    ///     none. A document whose tab is in another window is shown there.
    /// </summary>
    /// <param name="document">An open document.</param>
    /// <returns>Whether the document's tab is selected.</returns>
    public Task<bool> ShowAsync(Document document);

    /// <summary>
    ///     Prepares closing documents' tabs: asks about their unsaved edits and lets their editors veto, without closing
    ///     yet.
    /// </summary>
    /// <param name="documents">Documents with a tab in this area.</param>
    /// <returns>
    ///     The transaction to commit, or <see langword="null"/> when the close was refused or a document has no tab here.
    /// </returns>
    public Task<DocumentCloseTransaction?> PrepareCloseAsync(IReadOnlyList<Document> documents);

    /// <summary>
    ///     Closes a document's tab, then the document.
    /// </summary>
    /// <param name="document">A document with a tab in this area.</param>
    /// <param name="force">Whether to close without asking about unsaved edits and without vetoes.</param>
    /// <returns>Whether the document closed.</returns>
    public Task<bool> CloseAsync(Document document, bool force = false);
}
