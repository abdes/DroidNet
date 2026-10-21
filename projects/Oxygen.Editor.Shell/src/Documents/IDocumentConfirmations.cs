// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.Shell.Documents;

/// <summary>
/// The confirmations a part's window asks before an operation discards or duplicates a document's changes. Each names
/// the document it affects. Closing asks through <see cref="IDocumentClosePrompt"/>.
/// </summary>
public interface IDocumentConfirmations
{
    /// <summary>
    /// Asks to discard a document's changes and read its file again: <c>Discard your changes to Wood and reload?</c> with
    /// <b>Reload</b> and <b>Cancel</b>.
    /// </summary>
    /// <param name="document">The document.</param>
    /// <returns><see langword="true"/> when the user chose to reload.</returns>
    public Task<bool> ConfirmReloadAsync(Document document);

    /// <summary>
    /// Asks to save documents before an operation that reads saved files: <c>Save 2 documents and play?</c> with
    /// <b>Save and Play</b> and <b>Cancel</b>, listing the documents.
    /// </summary>
    /// <param name="documents">The unsaved documents the operation needs saved.</param>
    /// <param name="operation">The operation.</param>
    /// <returns><see langword="true"/> when the user chose to save and continue.</returns>
    public Task<bool> ConfirmSaveBeforeAsync(IReadOnlyList<UnsavedDocument> documents, SaveBeforeOperation operation);

    /// <summary>Asks for the name of a copy of a document, saved beside it.</summary>
    /// <param name="document">The document.</param>
    /// <returns>The name, or <see langword="null"/> when the user cancelled.</returns>
    public Task<string?> AskCopyNameAsync(Document document);
}
