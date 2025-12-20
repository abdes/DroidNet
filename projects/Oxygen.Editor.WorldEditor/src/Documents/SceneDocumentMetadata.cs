// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Documents;

namespace Oxygen.Editor.World.Documents;

/// <summary>
///     Metadata for a Scene document, used by Aura document tabs.
/// </summary>
/// <param name="documentId">
///     The unique identifier for the document. If <see langword="null" /> a new <c>Guid</c> will
///     be automatically assigned to <see cref="BaseDocumentMetadata.DocumentId"/>.
/// </param>
/// <remarks>
///     Scene documents are not closable by the user, hence <see cref="BaseDocumentMetadata.IsClosable"/>
///     always returns <see langword="false"/>, and attempts to set it will throw <see cref="NotSupportedException"/>.
/// </remarks>
public class SceneDocumentMetadata(Guid? documentId = null) : BaseDocumentMetadata(documentId)
{
    /// <inheritdoc/>
    public override string DocumentType => "Scene";

    /// <summary>
    ///     Gets or sets the viewport layout and configuration, only applicable for scene documents.
    /// </summary>
    public SceneViewLayout Layout { get; set; } = SceneViewLayout.OnePane;

    /// <inheritdoc/>
    public override bool IsClosable { get => false; set => throw new NotSupportedException(); }
}
