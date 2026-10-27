// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Selection;

namespace Oxygen.Editor.SceneExplorer;

/// <summary>
/// Builds the context-menu shape for a captured Explorer selection. This is the single owner of the
/// action definitions; the toolbar and keyboard surface resolve their availability through the same rules.
/// </summary>
public static class SceneExplorerContextMenu
{
    /// <summary>
    /// Builds the ordered menu entries for a captured selection context.
    /// </summary>
    /// <param name="kind">The classified selection kind.</param>
    /// <param name="primaryIsInFolder">Whether the primary node is grouped in a folder.</param>
    /// <param name="primaryHasChildren">Whether the primary row has children (for expand/collapse).</param>
    /// <param name="primaryIsUnlocked">Whether the primary row is not locked (for rename/delete).</param>
    /// <returns>The ordered menu entries.</returns>
    public static IReadOnlyList<SceneExplorerMenuEntry> Build(
        SceneSelectionKind kind,
        bool primaryIsInFolder,
        bool primaryHasChildren,
        bool primaryIsUnlocked)
        => kind switch
        {
            SceneSelectionKind.Node => NodeEntries(primaryIsInFolder, primaryHasChildren, primaryIsUnlocked),
            SceneSelectionKind.Folder => FolderEntries(primaryHasChildren, primaryIsUnlocked),
            SceneSelectionKind.Scene or SceneSelectionKind.Empty => RootEntries(kind == SceneSelectionKind.Scene),
            SceneSelectionKind.Mixed => MixedEntries(primaryIsUnlocked),
            _ => [],
        };

    private static List<SceneExplorerMenuEntry> NodeEntries(bool primaryIsInFolder, bool primaryHasChildren, bool primaryIsUnlocked)
    {
        List<SceneExplorerMenuEntry> entries =
        [
            new(SceneExplorerCommandKind.NewNode, "New child node", IsEnabled: true),
            new(SceneExplorerCommandKind.NewFolder, "New folder", IsEnabled: true),
            new(SceneExplorerCommandKind.Rename, "Rename", primaryIsUnlocked),
            new(SceneExplorerCommandKind.Cut, "Cut", primaryIsUnlocked),
            new(SceneExplorerCommandKind.Copy, "Copy", IsEnabled: true),
            new(SceneExplorerCommandKind.Paste, "Paste", IsEnabled: true),
            new(SceneExplorerCommandKind.PasteAsChild, "Paste as child", IsEnabled: true),
            new(SceneExplorerCommandKind.Delete, "Delete node", primaryIsUnlocked),
            primaryIsInFolder
                ? new(SceneExplorerCommandKind.RemoveFromFolder, "Remove from folder", IsEnabled: true)
                : new(SceneExplorerCommandKind.MoveToSceneRoot, "Move to scene root", IsEnabled: true),
        ];
        if (primaryHasChildren)
        {
            entries.Add(new(SceneExplorerCommandKind.Expand, "Expand", IsEnabled: true));
        }

        return entries;
    }

    private static List<SceneExplorerMenuEntry> FolderEntries(bool primaryHasChildren, bool primaryIsUnlocked)
    {
        List<SceneExplorerMenuEntry> entries =
        [
            new(SceneExplorerCommandKind.NewNode, "New node in folder", IsEnabled: true),
            new(SceneExplorerCommandKind.NewFolder, "New subfolder", IsEnabled: true),
            new(SceneExplorerCommandKind.Rename, "Rename", primaryIsUnlocked),
            new(SceneExplorerCommandKind.Cut, "Cut", IsEnabled: true),
            new(SceneExplorerCommandKind.Copy, "Copy", IsEnabled: true),
            new(SceneExplorerCommandKind.Paste, "Paste", IsEnabled: true),
            new(SceneExplorerCommandKind.Delete, "Remove folder", IsEnabled: true),
        ];
        if (primaryHasChildren)
        {
            entries.Add(new(SceneExplorerCommandKind.Collapse, "Collapse", IsEnabled: true));
        }

        return entries;
    }

    // The scene row, or no selection: creating and pasting at the root.
    private static List<SceneExplorerMenuEntry> RootEntries(bool isScene)
    {
        List<SceneExplorerMenuEntry> entries = isScene ? [new(SceneExplorerCommandKind.Rename, "Rename scene", IsEnabled: true)] : [];
        entries.Add(new(SceneExplorerCommandKind.NewNode, "New node", IsEnabled: true));
        entries.Add(new(SceneExplorerCommandKind.NewFolder, "New folder", IsEnabled: true));
        entries.Add(new(SceneExplorerCommandKind.Paste, "Paste", IsEnabled: true));
        return entries;
    }

    private static List<SceneExplorerMenuEntry> MixedEntries(bool primaryIsUnlocked)
        =>
        [
            new(SceneExplorerCommandKind.Cut, "Cut", primaryIsUnlocked),
            new(SceneExplorerCommandKind.Copy, "Copy", IsEnabled: true),
            new(SceneExplorerCommandKind.Paste, "Paste", IsEnabled: true),
            new(SceneExplorerCommandKind.Delete, "Delete selected items", primaryIsUnlocked),
        ];
}
