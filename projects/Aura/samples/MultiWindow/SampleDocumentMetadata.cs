// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     Represents metadata for a sample document, including its ID, title, icon, and state flags.
/// </summary>
internal sealed class SampleDocumentMetadata : IDocumentMetadata
{
    /// <summary>
    ///     Gets or sets the unique identifier for the document.
    /// </summary>
    public Guid DocumentId { get; set; } = Guid.NewGuid();

    /// <summary>
    ///     Gets or sets the title of the document.
    /// </summary>
    public string Title { get; set; } = string.Empty;

    /// <summary>
    ///     Gets or sets the URI of the document's icon.
    /// </summary>
    public Uri? IconUri { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the document has unsaved changes.
    /// </summary>
    public bool IsDirty { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the document is pinned.
    /// </summary>
    public bool IsPinnedHint { get; set; }

    /// <inheritdoc/>
    public bool IsClosable { get; set; } = true;
}
