// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable scene authoring operation-kind names used by editor operation results.
/// </summary>
public static class SceneOperationKinds
{
    /// <summary>
    /// Scene node creation.
    /// </summary>
    public const string NodeCreate = "Scene.Node.Create";

    /// <summary>
    /// Procedural primitive scene node creation.
    /// </summary>
    public const string NodeCreatePrimitive = "Scene.Node.CreatePrimitive";

    /// <summary>
    /// Light scene node creation.
    /// </summary>
    public const string NodeCreateLight = "Scene.Node.CreateLight";

    /// <summary>
    /// Scene node rename.
    /// </summary>
    public const string NodeRename = "Scene.Node.Rename";

    /// <summary>
    /// Scene node hierarchy deletion.
    /// </summary>
    public const string NodeDelete = "Scene.Node.Delete";

    /// <summary>
    /// Scene node hierarchy reparent.
    /// </summary>
    public const string NodeReparent = "Scene.Node.Reparent";

    /// <summary>
    /// Scene explorer layout folder creation.
    /// </summary>
    public const string ExplorerFolderCreate = "Scene.ExplorerFolder.Create";

    /// <summary>
    /// Scene explorer layout folder rename.
    /// </summary>
    public const string ExplorerFolderRename = "Scene.ExplorerFolder.Rename";

    /// <summary>
    /// Scene explorer layout folder deletion.
    /// </summary>
    public const string ExplorerFolderDelete = "Scene.ExplorerFolder.Delete";

    /// <summary>
    /// Scene explorer layout-only move.
    /// </summary>
    public const string ExplorerLayoutMoveNode = "Scene.ExplorerLayout.MoveNode";

    /// <summary>
    /// Scene document save.
    /// </summary>
    public const string Save = "Scene.Save";
}
