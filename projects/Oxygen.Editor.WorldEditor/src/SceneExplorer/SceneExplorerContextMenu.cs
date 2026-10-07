// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.WorldEditor.Documents.Selection;

namespace Oxygen.Editor.World.SceneExplorer;

/// <summary>
/// Identifies an Explorer authoring action shared by the toolbar, context menu and keyboard.
/// </summary>
public enum SceneExplorerCommandKind
{
    /// <summary>Create a new scene node.</summary>
    NewNode,

    /// <summary>Create a new explorer folder.</summary>
    NewFolder,

    /// <summary>Rename the primary selected row.</summary>
    Rename,

    /// <summary>Stage the selection as a move (cut).</summary>
    Cut,

    /// <summary>Snapshot the selection (copy).</summary>
    Copy,

    /// <summary>Paste at the resolved destination.</summary>
    Paste,

    /// <summary>Paste as a child of a node, preserving world pose.</summary>
    PasteAsChild,

    /// <summary>Delete the selected nodes/folders.</summary>
    Delete,

    /// <summary>Remove the primary node from its folder (grouping-only).</summary>
    RemoveFromFolder,

    /// <summary>Move the primary node to the scene root.</summary>
    MoveToSceneRoot,

    /// <summary>Expand the primary row.</summary>
    Expand,

    /// <summary>Collapse the primary row.</summary>
    Collapse,

    /// <summary>Hide every captured node in editing viewports.</summary>
    Hide,

    /// <summary>Show every captured node in editing viewports.</summary>
    Show,

    /// <summary>Lock every captured node.</summary>
    Lock,

    /// <summary>Unlock every captured node.</summary>
    Unlock,

    /// <summary>Show all nodes of the captured scene in editing viewports.</summary>
    ShowAll,

    /// <summary>Look through the primary camera node in the active viewport, or return to the editor camera.</summary>
    LookThroughCamera,

    /// <summary>Pilot the primary camera node in the active viewport, or stop piloting it.</summary>
    PilotCamera,

    /// <summary>Move the primary camera node to the active viewport's editor camera.</summary>
    AlignCameraToView,
}

/// <summary>
/// A single context-menu entry: the shared action identity, its label and whether it is currently available.
/// </summary>
/// <param name="Kind">The shared action identity.</param>
/// <param name="Label">The contextual display label.</param>
/// <param name="IsEnabled">Whether the action can currently be invoked.</param>
public sealed record SceneExplorerMenuEntry(SceneExplorerCommandKind Kind, string Label, bool IsEnabled);

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
    {
        var entries = new List<SceneExplorerMenuEntry>();
        switch (kind)
        {
            case SceneSelectionKind.Node:
                entries.Add(new(SceneExplorerCommandKind.NewNode, "New child node", true));
                entries.Add(new(SceneExplorerCommandKind.NewFolder, "New folder", true));
                entries.Add(new(SceneExplorerCommandKind.Rename, "Rename", primaryIsUnlocked));
                entries.Add(new(SceneExplorerCommandKind.Cut, "Cut", primaryIsUnlocked));
                entries.Add(new(SceneExplorerCommandKind.Copy, "Copy", true));
                entries.Add(new(SceneExplorerCommandKind.Paste, "Paste", true));
                entries.Add(new(SceneExplorerCommandKind.PasteAsChild, "Paste as child", true));
                entries.Add(new(SceneExplorerCommandKind.Delete, "Delete node", primaryIsUnlocked));
                if (primaryIsInFolder)
                {
                    entries.Add(new(SceneExplorerCommandKind.RemoveFromFolder, "Remove from folder", true));
                }
                else
                {
                    entries.Add(new(SceneExplorerCommandKind.MoveToSceneRoot, "Move to scene root", true));
                }

                if (primaryHasChildren)
                {
                    entries.Add(new(SceneExplorerCommandKind.Expand, "Expand", true));
                }

                break;

            case SceneSelectionKind.Folder:
                entries.Add(new(SceneExplorerCommandKind.NewNode, "New node in folder", true));
                entries.Add(new(SceneExplorerCommandKind.NewFolder, "New subfolder", true));
                entries.Add(new(SceneExplorerCommandKind.Rename, "Rename", primaryIsUnlocked));
                entries.Add(new(SceneExplorerCommandKind.Cut, "Cut", true));
                entries.Add(new(SceneExplorerCommandKind.Copy, "Copy", true));
                entries.Add(new(SceneExplorerCommandKind.Paste, "Paste", true));
                entries.Add(new(SceneExplorerCommandKind.Delete, "Remove folder", true));
                if (primaryHasChildren)
                {
                    entries.Add(new(SceneExplorerCommandKind.Collapse, "Collapse", true));
                }

                break;

            case SceneSelectionKind.Scene:
            case SceneSelectionKind.Empty:
                if (kind == SceneSelectionKind.Scene)
                {
                    entries.Add(new(SceneExplorerCommandKind.Rename, "Rename scene", true));
                }

                entries.Add(new(SceneExplorerCommandKind.NewNode, "New node", true));
                entries.Add(new(SceneExplorerCommandKind.NewFolder, "New folder", true));
                entries.Add(new(SceneExplorerCommandKind.Paste, "Paste", true));
                break;

            case SceneSelectionKind.Mixed:
                entries.Add(new(SceneExplorerCommandKind.Cut, "Cut", primaryIsUnlocked));
                entries.Add(new(SceneExplorerCommandKind.Copy, "Copy", true));
                entries.Add(new(SceneExplorerCommandKind.Paste, "Paste", true));
                entries.Add(new(SceneExplorerCommandKind.Delete, "Delete selected items", primaryIsUnlocked));
                break;

            default:
                break;
        }

        return entries;
    }
}
