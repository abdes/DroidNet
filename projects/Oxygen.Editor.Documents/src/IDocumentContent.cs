// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>What a document of one kind edits: the kind's model, and the work on it that needs to know its format.</summary>
/// <remarks>
/// Editors reach the model through the kind's own content type. A content that is <see cref="IDisposable"/> is disposed
/// when a reload replaces it or its document closes.
/// </remarks>
public interface IDocumentContent
{
    /// <summary>Gets a value indicating whether an edit is in progress, such as an open drag, that the history has not recorded yet.</summary>
    /// <remarks>A document with an edit in progress does not reload by itself when its file changes.</remarks>
    public bool HasEditsInProgress { get; }

    /// <summary>Finishes the edits in progress, such as an open drag or edit session, as one history entry each.</summary>
    /// <param name="cancellationToken">Cancels waiting for the edits.</param>
    /// <returns>The task that completes once no edit is in progress.</returns>
    public Task FinishEditsAsync(CancellationToken cancellationToken);

    /// <summary>Takes what a save of the document's own file writes.</summary>
    /// <returns>The capture, or why the content cannot be saved.</returns>
    /// <remarks>Capturing is quick and runs on the thread that edits the model; encoding the capture can run anywhere.</remarks>
    public DocumentCapture Capture();

    /// <summary>Takes what a new asset copied from this content writes.</summary>
    /// <param name="copy">The new asset's file, whose identity the copy takes.</param>
    /// <returns>The capture, or why the content cannot be saved.</returns>
    public DocumentCapture CaptureCopy(DocumentFile copy);

    /// <summary>Applies a relocation that rewrote the document's file: its own new identity and its references.</summary>
    /// <param name="relocation">The document's file after the relocation and the assets it moved.</param>
    public void ApplyRelocation(DocumentRelocation relocation);
}
