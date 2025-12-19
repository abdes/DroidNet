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
    ///     The caller is responsible for ensuring the <paramref name="contentId"/> equals the
    ///     document id when the tab represents a document.
    /// </summary>
    /// <param name="contentId">
    ///     The UI content identifier for the tab (equal to DocumentId for document tabs).
    /// </param>
    /// <param name="metadata">The optional app-provided metadata to populate the TabItem.</param>
    /// <returns>A new TabItem matching the supplied metadata.</returns>
    public static Controls.TabItem CreateTabItemFromMetadata(Guid contentId, IDocumentMetadata? metadata)
    {
        var tab = new Controls.TabItem
        {
            ContentId = contentId,
            Header = metadata?.Title ?? string.Empty,
            IsClosable = metadata?.IsClosable ?? true,
            IsPinned = metadata?.IsPinnedHint ?? false,
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
        tab.IsPinned = metadata?.IsPinnedHint ?? false;
        tab.IsClosable = metadata?.IsClosable ?? tab.IsClosable;
        tab.IsDirty = metadata?.IsDirty ?? false;

        if (metadata?.IconUri is { } parsedUri)
        {
            tab.Icon = new Microsoft.UI.Xaml.Controls.BitmapIconSource { UriSource = parsedUri };
        }
    }
}
