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
///     The tab shows the state of its <see cref="Document"/>, which <see cref="Refresh"/> copies.
/// </remarks>
public class SceneDocumentMetadata(Guid? documentId = null) : BaseDocumentMetadata(documentId)
{
    /// <inheritdoc/>
    public override string DocumentType => "Scene";

    /// <summary>
    ///     Gets or sets the viewport layout and configuration, only applicable for scene documents.
    /// </summary>
    public SceneViewLayout Layout { get; set; } = SceneViewLayout.OnePane;

    /// <summary>Gets the scene document this tab shows.</summary>
    public Document? Document { get; init; }

    /// <summary>Gets the identity of the scene document this tab shows; the tab is keyed by its value.</summary>
    public Oxygen.Editor.Documents.DocumentId SceneDocumentId => this.Document?.Id ?? Oxygen.Editor.Documents.DocumentId.Empty;

    /// <summary>Gets the scene the tab's document edits, or null without a document.</summary>
    public Scene? Scene => (this.Document?.Content as SceneContent)?.Scene;

    /// <inheritdoc/>
    public override bool IsClosable { get => false; set => throw new NotSupportedException(); }

    /// <summary>Creates the tab of a scene document.</summary>
    /// <param name="document">The scene document.</param>
    /// <returns>The tab's metadata, showing the document's state.</returns>
    public static SceneDocumentMetadata For(Document document)
    {
        ArgumentNullException.ThrowIfNull(document);
        var metadata = new SceneDocumentMetadata(document.Id.Value) { Document = document };
        _ = metadata.Refresh();
        return metadata;
    }

    /// <summary>Copies the document's title, dirtiness and presence on disk into the tab.</summary>
    /// <returns>Whether anything the tab shows changed.</returns>
    public bool Refresh()
    {
        if (this.Document is not { } document)
        {
            return false;
        }

        var changed = !string.Equals(this.Title, document.Title, StringComparison.Ordinal)
            || this.IsDirty != document.IsDirty || this.IsMissingOnDisk != document.IsMissingOnDisk;
        this.Title = document.Title;
        this.IsMissingOnDisk = document.IsMissingOnDisk;

        // A missing file makes the tab dirty by itself; the change counter follows the document's edits only.
        var edits = document.IsDirty && !document.IsMissingOnDisk;
        if (edits != (this.ChangeVersion != this.SavedVersion))
        {
            this.IsDirty = edits;
        }

        return changed;
    }
}
