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
    /// Transform component edit.
    /// </summary>
    public const string EditTransform = "Scene.Component.EditTransform";

    /// <summary>
    /// Geometry component edit.
    /// </summary>
    public const string EditGeometry = "Scene.Component.EditGeometry";

    /// <summary>
    /// Geometry material slot edit.
    /// </summary>
    public const string EditMaterialSlot = "Scene.Component.EditMaterialSlot";

    /// <summary>
    /// Perspective camera component edit.
    /// </summary>
    public const string EditPerspectiveCamera = "Scene.Component.EditCamera";

    /// <summary>
    /// Directional light component edit.
    /// </summary>
    public const string EditDirectionalLight = "Scene.Component.EditLight";

    /// <summary>
    /// Component add.
    /// </summary>
    public const string AddComponent = "Scene.Component.Add";

    /// <summary>
    /// Component removal.
    /// </summary>
    public const string RemoveComponent = "Scene.Component.Remove";

    /// <summary>
    /// Scene environment edit.
    /// </summary>
    public const string EditEnvironment = "Scene.Environment.Edit";

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
