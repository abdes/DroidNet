// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Documents;

namespace DroidNet.Aura.Documents;

/// <summary>
///     Provides extension methods for creating and updating <see cref="Controls.TabItem"/>
///     instances from <see cref="IDocumentMetadata"/>.
/// </summary>
internal static class TabItemExtensions
{
    /// <summary>
    ///     Creates a <see cref="Controls.TabItem"/> that represents the supplied app metadata. The
    ///     created TabItem is a UI snapshot and is safe to add to the TabStrip's Items collection.
    ///     Its <see cref="Controls.TabItem.ContentId"/> is the document's id.
    /// </summary>
    /// <param name="documentId">The id of the document the tab shows.</param>
    /// <param name="metadata">The optional app-provided metadata to populate the TabItem.</param>
    /// <returns>A new TabItem matching the supplied metadata.</returns>
    public static Controls.TabItem CreateTabItemFromMetadata(DocumentId documentId, IDocumentMetadata? metadata)
    {
        // The strip control compares tabs by a Guid; a document's tab carries the document's id.
        var tab = new Controls.TabItem
        {
            ContentId = documentId.Value,
            Header = metadata?.Title ?? string.Empty,
            IsClosable = metadata?.IsClosable ?? true,
            IsPinned = metadata?.IsPinnedHint ?? false,
            IsPinLocked = metadata?.IsPinLockedHint ?? false,
            IsDirty = metadata?.IsDirty ?? false,
        };

        // Convert Metadata.IconUri to an IconSource if available
        if (metadata?.IconUri is { } parsedUri)
        {
            tab.Icon = new Microsoft.UI.Xaml.Controls.BitmapIconSource { UriSource = parsedUri };
        }

        return tab;
    }

    /// <summary>
    ///     Updates a <see cref="Controls.TabItem"/> with values from an <see
    ///     cref="IDocumentMetadata"/>. This mirrors only UI-friendly values and does not modify
    ///     UI-only state such as <see cref="Controls.TabItem.IsSelected"/>.
    /// </summary>
    /// <param name="tab">The TabItem to update.</param>
    /// <param name="metadata">The metadata whose values are applied to the TabItem.</param>
    public static void ApplyMetadataToTab(Controls.TabItem tab, IDocumentMetadata? metadata)
    {
        ArgumentNullException.ThrowIfNull(tab);

        tab.Header = metadata?.Title ?? string.Empty;

        // The model owns the pin: it lifts the lock for the moment it sets the pinned state.
        tab.IsPinLocked = false;
        tab.IsPinned = metadata?.IsPinnedHint ?? false;
        tab.IsPinLocked = metadata?.IsPinLockedHint ?? false;
        tab.IsClosable = metadata?.IsClosable ?? tab.IsClosable;
        tab.IsDirty = metadata?.IsDirty ?? false;

        if (metadata?.IconUri is { } parsedUri)
        {
            tab.Icon = new Microsoft.UI.Xaml.Controls.BitmapIconSource { UriSource = parsedUri };
        }
    }
}
