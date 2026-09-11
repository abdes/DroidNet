// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Opens, edits, and persists scalar material documents.
/// </summary>
public interface IMaterialDocumentService : IMaterialPropertyEditService
{
    /// <summary>Gets the current immutable authoring and persistence state.</summary>
    /// <param name="documentId">The open material document identity.</param>
    /// <returns>The current document snapshot.</returns>
    public MaterialDocument GetDocument(Guid documentId);

    /// <summary>Begins a grouped material gesture with an owned before snapshot.</summary>
    /// <param name="documentId">The open material document.</param>
    /// <param name="field">The user-visible edited field.</param>
    /// <returns>The document-scoped gesture identity.</returns>
    public MaterialEditSession BeginEditSession(Guid documentId, string field);

    /// <summary>Applies a validated preview without creating history or a committed revision.</summary>
    /// <param name="session">The owning gesture.</param>
    /// <param name="edit">The requested property values.</param>
    /// <param name="cancellationToken">Cancels before authoring mutation.</param>
    /// <returns>The validation and application result.</returns>
    public Task<MaterialEditResult> PreviewPropertiesAsync(MaterialEditSession session, Schemas.PropertyEdit edit, CancellationToken cancellationToken = default);

    /// <summary>Commits one history entry or restores the original gesture snapshot.</summary>
    /// <param name="session">The owning gesture.</param>
    /// <param name="commit">Whether to retain the accepted preview values.</param>
    /// <returns>The completion result.</returns>
    public MaterialEditResult CompleteEditSession(MaterialEditSession session, bool commit);

    /// <summary>Gets whether the document has a committed or active edit to undo.</summary>
    /// <param name="documentId">The open material document.</param>
    /// <returns>Whether Undo is available.</returns>
    public bool CanUndo(Guid documentId);

    /// <summary>Gets whether the document has a change to redo.</summary>
    /// <param name="documentId">The open material document.</param>
    /// <returns>Whether Redo is available.</returns>
    public bool CanRedo(Guid documentId);

    /// <summary>Restores the document's previous source through the validated authoring path.</summary>
    /// <param name="documentId">The open material document.</param>
    /// <returns>The application result.</returns>
    public MaterialEditResult Undo(Guid documentId);

    /// <summary>Reapplies the document's next source through the validated authoring path.</summary>
    /// <param name="documentId">The open material document.</param>
    /// <returns>The application result.</returns>
    public MaterialEditResult Redo(Guid documentId);

    /// <summary>
    /// Creates a new material document at the target URI.
    /// </summary>
    /// <param name="targetUri">The target source asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The opened material document.</returns>
    public Task<MaterialDocument> CreateAsync(Uri targetUri, CancellationToken cancellationToken = default);

    /// <summary>
    /// Opens an existing material document.
    /// </summary>
    /// <param name="sourceUri">The source asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The opened material document.</returns>
    public Task<MaterialDocument> OpenAsync(Uri sourceUri, CancellationToken cancellationToken = default);

    /// <summary>
    /// Applies one scalar material edit.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="edit">The material field edit.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The edit result.</returns>
    public Task<MaterialEditResult> EditScalarAsync(Guid documentId, MaterialFieldEdit edit, CancellationToken cancellationToken = default);

    /// <summary>
    /// Saves a material document.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The save result.</returns>
    public Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>Reloads source bytes after the caller explicitly confirms discarding unsaved changes.</summary>
    /// <param name="documentId">The open document.</param>
    /// <param name="cancellationToken">Cancels loading before replacement.</param>
    /// <returns>The reloaded document state.</returns>
    public Task<MaterialDocument> ReloadAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>Saves the current authoring snapshot as a distinct material without redirecting the original document.</summary>
    /// <param name="documentId">The source document.</param>
    /// <param name="targetUri">The new authored material URI.</param>
    /// <param name="cancellationToken">Cancels before publication.</param>
    /// <returns>The canonical URI of the new material.</returns>
    public Task<Uri> SaveCopyAsync(Guid documentId, Uri targetUri, CancellationToken cancellationToken = default);

    /// <summary>
    /// Cooks a material document through the editor content pipeline.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    public Task<MaterialCookResult> CookAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Closes a material document.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="discard">Whether unsaved changes should be discarded.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The close task.</returns>
    public Task CloseAsync(Guid documentId, bool discard, CancellationToken cancellationToken = default);
}
