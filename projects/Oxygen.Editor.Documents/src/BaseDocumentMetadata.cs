// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Aura.Documents;

namespace Oxygen.Editor.Documents;

/// <summary>
/// Base metadata for any WorldEditor tabbed document, implements IDocumentMetadata.
/// </summary>
public abstract class BaseDocumentMetadata : IDocumentMetadata
{
    /// <inheritdoc/>
    public Guid DocumentId { get; init; }

    /// <inheritdoc/>
    public string Title { get; set; } = string.Empty;

    /// <inheritdoc/>
    public Uri? IconUri { get; set; }

    /// <inheritdoc/>
    public bool IsDirty { get; set; }

    /// <inheritdoc/>
    public bool IsPinnedHint { get; set; }

    /// <inheritdoc/>
    public bool IsClosable { get; set; } = true;

    /// <summary>
    ///     Gets the type of document (e.g., "Scene", "Asset", "Render", etc.)
    /// </summary>
    public abstract string DocumentType { get; }
}
