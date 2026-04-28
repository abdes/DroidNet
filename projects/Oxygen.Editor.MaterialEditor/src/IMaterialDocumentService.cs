// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Opens, edits, and persists scalar material documents.
/// </summary>
public interface IMaterialDocumentService
{
    /// <summary>
    /// Creates a new material document at the target URI.
    /// </summary>
    /// <param name="targetUri">The target source asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The opened material document.</returns>
    Task<MaterialDocument> CreateAsync(Uri targetUri, CancellationToken cancellationToken = default);

    /// <summary>
    /// Opens an existing material document.
    /// </summary>
    /// <param name="sourceUri">The source asset URI.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The opened material document.</returns>
    Task<MaterialDocument> OpenAsync(Uri sourceUri, CancellationToken cancellationToken = default);

    /// <summary>
    /// Applies one scalar material edit.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="edit">The material field edit.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The edit result.</returns>
    Task<MaterialEditResult> EditScalarAsync(Guid documentId, MaterialFieldEdit edit, CancellationToken cancellationToken = default);

    /// <summary>
    /// Saves a material document.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The save result.</returns>
    Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Cooks a material document through the editor content pipeline.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The cook result.</returns>
    Task<MaterialCookResult> CookAsync(Guid documentId, CancellationToken cancellationToken = default);

    /// <summary>
    /// Closes a material document.
    /// </summary>
    /// <param name="documentId">The material document identity.</param>
    /// <param name="discard">Whether unsaved changes should be discarded.</param>
    /// <param name="cancellationToken">The cancellation token.</param>
    /// <returns>The close task.</returns>
    Task CloseAsync(Guid documentId, bool discard, CancellationToken cancellationToken = default);
}
