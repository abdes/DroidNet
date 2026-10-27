// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.SceneExplorer;

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

    /// <summary>Select the instance root of the primary node an instance brought in.</summary>
    SelectInstance,

    /// <summary>Make the nodes the primary instance root's source brings in the scene's own, keeping nested instances.</summary>
    Unpack,

    /// <summary>Make the nodes the primary instance root's source brings in the scene's own, down to the deepest level.</summary>
    UnpackCompletely,
}
