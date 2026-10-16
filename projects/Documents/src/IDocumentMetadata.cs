// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Documents;

/// <summary>
///     What a tab shows for an open document. The application owns the state behind it; the tab reads it and follows
///     <see cref="INotifyPropertyChanged.PropertyChanged"/>, which may be raised on any thread.
/// </summary>
public interface IDocumentMetadata : INotifyPropertyChanged
{
    /// <summary>
    ///     Gets the id of the document, which is also its tab's key. It is never <see cref="DocumentId.Empty"/>.
    /// </summary>
    public DocumentId DocumentId { get; }

    /// <summary>
    ///     Gets the title the tab shows.
    /// </summary>
    public string Title { get; }

    /// <summary>
    ///     Gets the URI of the icon the tab shows, if any.
    /// </summary>
    public Uri? IconUri { get; }

    /// <summary>
    ///     Gets a value indicating whether the document has unsaved changes, which the tab marks.
    /// </summary>
    public bool IsDirty { get; }

    /// <summary>
    ///     Gets a value indicating whether the user can close the tab.
    /// </summary>
    public bool IsClosable { get; }

    /// <summary>
    ///     Gets a value indicating whether the tab should be pinned by default. It is a preference, not the tab's state.
    /// </summary>
    public bool IsPinnedHint { get; }
}
