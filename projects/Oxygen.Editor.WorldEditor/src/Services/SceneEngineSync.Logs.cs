// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.WorldEditor.Services;

/// <summary>
///     Logging helpers for <see cref="SceneEngineSync"/>.
/// </summary>
public partial class SceneEngineSync
{
    [LoggerMessage(Level = LogLevel.Information, Message = "Engine is not running (State={EngineState}); deferring scene sync for '{SceneName}'")]
    private static partial void LogEngineNotRunningDeferringSceneSync(ILogger logger, EngineServiceState engineState, string sceneName);

    [Conditional("DEBUG")]
    private void LogEngineNotRunningDeferringSceneSync(Scene scene)
        => LogEngineNotRunningDeferringSceneSync(this.logger, this.engineService.State, scene.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Engine is not available (State={EngineState}); skipping scene sync for '{SceneName}'")]
    private static partial void LogEngineNotAvailableSkippingSceneSync(ILogger logger, EngineServiceState engineState, string sceneName);

    [Conditional("DEBUG")]
    private void LogEngineNotAvailableSkippingSceneSync(Scene scene)
        => LogEngineNotAvailableSkippingSceneSync(this.logger, this.engineService.State, scene.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available (EngineState={EngineState}); skipping scene sync for '{SceneName}'")]
    private static partial void LogOxygenWorldNotAvailableSkippingSceneSync(ILogger logger, EngineServiceState engineState, string sceneName);

    [Conditional("DEBUG")]
    private void LogOxygenWorldNotAvailableSkippingSceneSync(Scene scene)
        => LogOxygenWorldNotAvailableSkippingSceneSync(this.logger, this.engineService.State, scene.Name);

    [LoggerMessage(Level = LogLevel.Information, Message = "Created scene '{SceneName}' in engine")]
    private static partial void LogCreatedSceneInEngine(ILogger logger, string sceneName);

    private void LogCreatedSceneInEngine(Scene scene)
        => LogCreatedSceneInEngine(this.logger, scene.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to sync scene '{SceneName}' with engine")]
    private static partial void LogFailedToSyncSceneWithEngine(ILogger logger, Exception exception, string sceneName);

    private void LogFailedToSyncSceneWithEngine(Exception ex, Scene scene)
        => LogFailedToSyncSceneWithEngine(this.logger, ex, scene.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot create node '{NodeName}'")]
    private static partial void LogCannotCreateNode(ILogger logger, string nodeName);

    private void LogCannotCreateNode(SceneNode node)
        => LogCannotCreateNode(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Created and initialized node '{NodeName}' in engine")]
    private static partial void LogCreatedAndInitializedNode(ILogger logger, string nodeName);

    private void LogCreatedAndInitializedNode(SceneNode node)
        => LogCreatedAndInitializedNode(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to create node '{NodeName}' in engine")]
    private static partial void LogFailedToCreateNode(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToCreateNode(Exception ex, SceneNode node)
        => LogFailedToCreateNode(this.logger, ex, node.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot remove node '{NodeId}'")]
    private static partial void LogCannotRemoveNode(ILogger logger, Guid nodeId);

    private void LogCannotRemoveNode(Guid nodeId)
        => LogCannotRemoveNode(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Removed node '{NodeId}' from engine")]
    private static partial void LogRemovedNode(ILogger logger, Guid nodeId);

    private void LogRemovedNode(Guid nodeId)
        => LogRemovedNode(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to remove node '{NodeId}' from engine")]
    private static partial void LogFailedToRemoveNode(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToRemoveNode(Exception ex, Guid nodeId)
        => LogFailedToRemoveNode(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot remove node hierarchy '{NodeId}'")]
    private static partial void LogCannotRemoveNodeHierarchy(ILogger logger, Guid nodeId);

    private void LogCannotRemoveNodeHierarchy(Guid nodeId)
        => LogCannotRemoveNodeHierarchy(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot remove node hierarchies")]
    private static partial void LogCannotRemoveNodeHierarchies(ILogger logger);

    private void LogCannotRemoveNodeHierarchies()
        => LogCannotRemoveNodeHierarchies(this.logger);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot reparent node '{NodeId}'")]
    private static partial void LogCannotReparentNode(ILogger logger, Guid nodeId);

    private void LogCannotReparentNode(Guid nodeId)
        => LogCannotReparentNode(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Reparented node {NodeId} -> {ParentId}")]
    private static partial void LogReparentedNode(ILogger logger, Guid nodeId, Guid? parentId);

    private void LogReparentedNode(Guid nodeId, Guid? parentId)
        => LogReparentedNode(this.logger, nodeId, parentId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to reparent node '{NodeId}'")]
    private static partial void LogFailedToReparentNode(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToReparentNode(Exception ex, Guid nodeId)
        => LogFailedToReparentNode(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot reparent hierarchies")]
    private static partial void LogCannotReparentHierarchies(ILogger logger);

    private void LogCannotReparentHierarchies()
        => LogCannotReparentHierarchies(this.logger);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot update transform for '{NodeName}'")]
    private static partial void LogCannotUpdateTransform(ILogger logger, string nodeName);

    private void LogCannotUpdateTransform(SceneNode node)
        => LogCannotUpdateTransform(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Updated transform for node '{NodeName}'")]
    private static partial void LogUpdatedTransform(ILogger logger, string nodeName);

    private void LogUpdatedTransform(SceneNode node)
        => LogUpdatedTransform(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to update transform for node '{NodeName}'")]
    private static partial void LogFailedToUpdateTransform(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToUpdateTransform(Exception ex, SceneNode node)
        => LogFailedToUpdateTransform(this.logger, ex, node.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot attach geometry to '{NodeName}'")]
    private static partial void LogCannotAttachGeometry(ILogger logger, string nodeName);

    private void LogCannotAttachGeometry(SceneNode node)
        => LogCannotAttachGeometry(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Attached geometry to node '{NodeName}'")]
    private static partial void LogAttachedGeometry(ILogger logger, string nodeName);

    private void LogAttachedGeometry(SceneNode node)
        => LogAttachedGeometry(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "OxygenWorld is not available; cannot detach geometry for node '{NodeId}'")]
    private static partial void LogCannotDetachGeometry(ILogger logger, Guid nodeId);

    private void LogCannotDetachGeometry(Guid nodeId)
        => LogCannotDetachGeometry(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "Detached geometry (hidden) for node '{NodeId}'")]
    private static partial void LogDetachedGeometry(ILogger logger, Guid nodeId);

    private void LogDetachedGeometry(Guid nodeId)
        => LogDetachedGeometry(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to detach geometry for node '{NodeId}'")]
    private static partial void LogFailedToDetachGeometry(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToDetachGeometry(Exception ex, Guid nodeId)
        => LogFailedToDetachGeometry(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to attach geometry to node '{NodeName}'")]
    private static partial void LogFailedToAttachGeometry(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToAttachGeometry(Exception ex, SceneNode node)
        => LogFailedToAttachGeometry(this.logger, ex, node.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Node '{NodeName}' creation was not successful")]
    private static partial void LogNodeCreationNotSuccessful(ILogger logger, string nodeName);

    private void LogNodeCreationNotSuccessful(SceneNode node)
        => LogNodeCreationNotSuccessful(this.logger, node.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to reparent node '{NodeName}'")]
    private static partial void LogFailedToReparentNode(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToReparentNode(Exception ex, SceneNode node)
        => LogFailedToReparentNode(this.logger, ex, node.Name);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to set transform for node '{NodeName}'")]
    private static partial void LogFailedToSetTransform(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToSetTransform(Exception ex, SceneNode node)
        => LogFailedToSetTransform(this.logger, ex, node.Name);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to request targeted transform propagation")]
    private static partial void LogFailedToRequestTransformPropagation(ILogger logger, Exception exception);

    private void LogFailedToRequestTransformPropagation(Exception ex)
        => LogFailedToRequestTransformPropagation(this.logger, ex);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to dump scene transforms for debug")]
    private static partial void LogFailedToDumpSceneTransformsForDebug(ILogger logger, Exception exception);

    private void LogFailedToDumpSceneTransformsForDebug(Exception ex)
        => LogFailedToDumpSceneTransformsForDebug(this.logger, ex);

    [LoggerMessage(Level = LogLevel.Information, Message = "SceneTransform: node='{NodeName}' parent='{ParentId}' pos=({X:0.00},{Y:0.00},{Z:0.00}) " +
        "scale=({SX:0.00},{SY:0.00},{SZ:0.00}) rot=({RX:0.00},{RY:0.00},{RZ:0.00},{RW:0.00})")]
    private static partial void LogSceneTransform(ILogger logger, string nodeName, string parentId, float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz, float rw);

    private void LogSceneTransform(SceneNode node, Guid? parentGuid)
    {
        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (transform is null)
        {
            this.LogSceneTransformHasNoComponent(node, parentGuid);
            return;
        }

        var (pos, rot, scale) = TransformConverter.ToNative(transform);
        LogSceneTransform(
            this.logger,
            node.Name,
            parentGuid?.ToString() ?? "root",
            pos.X,
            pos.Y,
            pos.Z,
            scale.X,
            scale.Y,
            scale.Z,
            rot.X,
            rot.Y,
            rot.Z,
            rot.W);
    }

    [LoggerMessage(Level = LogLevel.Information, Message = "SceneTransform: node='{NodeName}' parent='{ParentId}' has NO Transform component")]
    private static partial void LogSceneTransformHasNoComponent(ILogger logger, string nodeName, string parentId);

    private void LogSceneTransformHasNoComponent(SceneNode node, Guid? parentGuid)
        => LogSceneTransformHasNoComponent(this.logger, node.Name, parentGuid?.ToString() ?? "root");
}
