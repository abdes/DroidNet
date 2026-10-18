// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// Reads what <c>LayoutScopes</c> does not expose: a folder's own entry (its name and expanded state) and the size of
/// the whole explorer layout.
/// </summary>
public static class LayoutQuery
{
    /// <summary>
    /// Finds a folder's entry anywhere in a scene's explorer layout.
    /// </summary>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="folder">The folder.</param>
    /// <returns>The folder's entry, or <see langword="null"/> when the layout has no such folder.</returns>
    public static ExplorerEntryData? Folder(IList<ExplorerEntryData> layout, FolderId folder)
    {
        ArgumentNullException.ThrowIfNull(layout);
        return Find(layout, folder);
    }

    /// <summary>
    /// Counts every entry of a scene's explorer layout, nested ones included.
    /// </summary>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <returns>The number of node and folder entries.</returns>
    public static int EntryCount(IList<ExplorerEntryData> layout)
    {
        ArgumentNullException.ThrowIfNull(layout);
        return Count(layout);
    }

    private static ExplorerEntryData? Find(IEnumerable<ExplorerEntryData>? entries, FolderId folder)
    {
        foreach (var entry in entries ?? [])
        {
            if (entry.FolderId == folder)
            {
                return entry;
            }

            if (Find(entry.Children, folder) is { } found)
            {
                return found;
            }
        }

        return null;
    }

    private static int Count(IEnumerable<ExplorerEntryData>? entries)
        => (entries ?? []).Sum(static entry => 1 + Count(entry.Children));
}
