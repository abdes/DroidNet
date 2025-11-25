// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Documents;

/// <summary>
///     Metadata for a Scene document, used by Aura document tabs.
///     Viewport layout and configuration are only applicable for scene documents.
/// </summary>
/// <param name="documentId">
///     The unique identifier for the document. If <see langword="null" /> a new <c>Guid</c> will
///     be automatically assigned to <see cref="BaseDocumentMetadata.DocumentId"/>.
/// </param>
public class SceneDocumentMetadata(Guid? documentId = null) : BaseDocumentMetadata(documentId)
{
    /// <inheritdoc/>
    public override string DocumentType => "Scene";

    /// <summary>
    ///     Gets or sets the viewport layout and configuration, only applicable for scene documents.
    /// </summary>
    public SceneViewLayout Layout { get; set; } = SceneViewLayout.OnePane;
}
