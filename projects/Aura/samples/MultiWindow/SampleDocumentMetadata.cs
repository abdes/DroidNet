// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.CompilerServices;
using DroidNet.Documents;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
///     Represents metadata for a sample document, including its ID, title, icon, and state flags. Its tabs follow
///     every change through <see cref="PropertyChanged"/>.
/// </summary>
internal sealed class SampleDocumentMetadata : IDocumentMetadata
{
    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    ///     Gets the unique identifier for the document.
    /// </summary>
    public DocumentId DocumentId { get; init; } = DocumentId.New();

    /// <summary>
    ///     Gets or sets the title of the document.
    /// </summary>
    public string Title { get; set => this.Set(ref field, value); } = string.Empty;

    /// <summary>
    ///     Gets or sets the URI of the document's icon.
    /// </summary>
    public Uri? IconUri { get; set => this.Set(ref field, value); }

    /// <summary>
    ///     Gets or sets a value indicating whether the document has unsaved changes.
    /// </summary>
    public bool IsDirty { get; set => this.Set(ref field, value); }

    /// <summary>
    ///     Gets or sets a value indicating whether the document is pinned.
    /// </summary>
    public bool IsPinnedHint { get; set => this.Set(ref field, value); }

    /// <inheritdoc/>
    public bool IsPinLockedHint { get; set => this.Set(ref field, value); }

    /// <inheritdoc/>
    public bool IsClosable { get; set => this.Set(ref field, value); } = true;

    private void Set<T>(ref T field, T value, [CallerMemberName] string? property = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return;
        }

        field = value;
        this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(property));
    }
}
