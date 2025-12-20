// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace Oxygen.Editor.Documents;

/// <summary>
/// Base metadata for any WorldEditor tabbed document, implements IDocumentMetadata.
/// </summary>
/// <param name="documentId">
///     The unique identifier for the document. If <see langword="null" /> a new <c>Guid</c> will
///     be automatically assigned to <see cref="DocumentId"/>.
/// </param>
public abstract class BaseDocumentMetadata(Guid? documentId = null) : IDocumentMetadata
{
    /// <summary>
    /// Gets the document identifier.
    /// </summary>
    public Guid DocumentId { get; } = documentId ?? Guid.NewGuid();

    /// <inheritdoc/>
    public string Title { get; set; } = string.Empty;

    /// <inheritdoc/>
    public Uri? IconUri { get; set; }

    /// <inheritdoc/>
    public bool IsDirty { get; set; }

    /// <inheritdoc/>
    public bool IsPinnedHint { get; set; }

    /// <inheritdoc/>
    public virtual bool IsClosable { get; set; } = true;

    /// <summary>
    ///     Gets the type of document (e.g., "Scene", "Asset", "Render", etc.)
    /// </summary>
    public abstract string DocumentType { get; }
}
