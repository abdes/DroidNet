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
    /// <remarks>A document whose file is missing on disk is unsaved even without edits.</remarks>
    public bool IsDirty
    {
        get => this.ChangeVersion != this.SavedVersion || this.IsMissingOnDisk;
        set
        {
            if (value)
            {
                this.ChangeVersion++;
            }
            else
            {
                this.SavedVersion = this.ChangeVersion;
            }
        }
    }

    /// <summary>
    /// Gets or sets a value indicating whether the document's file was deleted or moved outside the editor. The document
    /// stays open; saving writes the file again at its location, and closing discards it.
    /// </summary>
    public bool IsMissingOnDisk { get; set; }

    /// <summary>Gets the authoring version used to detect edits arriving during a save.</summary>
    public long ChangeVersion { get; private set; }

    /// <summary>Gets the latest successfully persisted authoring version.</summary>
    public long SavedVersion { get; private set; }

    /// <inheritdoc/>
    public bool IsPinnedHint { get; set; }

    /// <inheritdoc/>
    public virtual bool IsClosable { get; set; } = true;

    /// <summary>
    ///     Gets the type of document (e.g., "Scene", "Asset", "Render", etc.)
    /// </summary>
    public abstract string DocumentType { get; }

    /// <summary>Acknowledges a persisted snapshot without clearing newer changes.</summary>
    /// <param name="version">The version captured with the persisted snapshot.</param>
    public void MarkSaved(long version)
    {
        ArgumentOutOfRangeException.ThrowIfGreaterThan(version, this.ChangeVersion);
        this.SavedVersion = Math.Max(this.SavedVersion, version);
    }
}
