// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Documents;

/// <summary>
///     Represents the minimal, app-provided metadata required by Aura to render a document in the
///     UI. This is the only DTO that crosses the Aura boundary — concrete metadata implementations
///     are app-owned and may include more fields.
/// </summary>
public interface IDocumentMetadata
{
    /// <summary>
    ///     Gets the stable document identifier assigned by the application. A valid document
    ///     metadata must always have a valid (non empty) document identifier.
    /// </summary>
    public Guid DocumentId { get; }

    /// <summary>
    ///     Gets or sets the UI-facing title to display for this document. The app may update this
    ///     property to reflect renames and changes.
    /// </summary>
    public string Title { get; set; }

    /// <summary>
    ///     Gets or sets optional URI of an icon to display alongside the tab header. The app
    ///     provides a URI string; Aura will attempt to resolve it to an <see
    ///     cref="Microsoft.UI.Xaml.Controls.IconSource"/>.
    /// </summary>
    public Uri? IconUri { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether hints whether the document has unsaved changes.
    ///     This is used by UI to render an unsaved or dirty indicator if the application exposes
    ///     such behavior.
    /// </summary>
    public bool IsDirty { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether a hint indicating whether the document should be
    ///     pinned by default. This serves as an application-provided preference and is not
    ///     authoritative UI state.
    /// </summary>
    public bool IsPinnedHint { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the document can be closed by the user.
    ///     If false, the close button will be hidden or disabled.
    /// </summary>
    public bool IsClosable { get; set; }
}
