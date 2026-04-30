// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Services;

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

    [LoggerMessage(Level = LogLevel.Information, Message = "SetProperties buffered. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount} PendingCount={PendingCount}")]
    private static partial void LogSetPropertiesBuffered(ILogger logger, Guid sceneId, Guid nodeId, int entryCount, int pendingCount);

    private void LogSetPropertiesBuffered(Guid sceneId, Guid nodeId, int entryCount, int pendingCount)
        => LogSetPropertiesBuffered(this.logger, sceneId, nodeId, entryCount, pendingCount);

    [LoggerMessage(Level = LogLevel.Information, Message = "SetProperties skipped. SceneId={SceneId} NodeId={NodeId} Status={Status} Code={Code} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesSkipped(ILogger logger, Guid sceneId, Guid nodeId, SyncStatus status, string code, int entryCount);

    private void LogSetPropertiesSkipped(Guid sceneId, Guid nodeId, SyncStatus status, string code, int entryCount)
        => LogSetPropertiesSkipped(this.logger, sceneId, nodeId, status, code, entryCount);

    [LoggerMessage(Level = LogLevel.Debug, Message = "SetProperties enqueued. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesEnqueued(ILogger logger, Guid sceneId, Guid nodeId, int entryCount);

    private void LogSetPropertiesEnqueued(Guid sceneId, Guid nodeId, int entryCount)
        => LogSetPropertiesEnqueued(this.logger, sceneId, nodeId, entryCount);

    [LoggerMessage(Level = LogLevel.Warning, Message = "SetProperties rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesRejected(ILogger logger, Exception exception, Guid sceneId, Guid nodeId, int entryCount);

    private void LogSetPropertiesRejected(Exception ex, Guid sceneId, Guid nodeId, int entryCount)
        => LogSetPropertiesRejected(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(Level = LogLevel.Error, Message = "SetProperties failed. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesFailed(ILogger logger, Exception exception, Guid sceneId, Guid nodeId, int entryCount);

    private void LogSetPropertiesFailed(Exception ex, Guid sceneId, Guid nodeId, int entryCount)
        => LogSetPropertiesFailed(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(Level = LogLevel.Information, Message = "Buffered SetProperties replay completed. SceneId={SceneId} PendingCount={PendingCount} ReplayedCount={ReplayedCount} SkippedCount={SkippedCount}")]
    private static partial void LogBufferedSetPropertiesReplayCompleted(ILogger logger, Guid sceneId, int pendingCount, int replayedCount, int skippedCount);

    private void LogBufferedSetPropertiesReplayCompleted(Guid sceneId, int pendingCount, int replayedCount, int skippedCount)
        => LogBufferedSetPropertiesReplayCompleted(this.logger, sceneId, pendingCount, replayedCount, skippedCount);

    [LoggerMessage(Level = LogLevel.Information, Message = "Buffered SetProperties replay skipped because the node no longer exists. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogBufferedSetPropertiesReplaySkipped(ILogger logger, Guid sceneId, Guid nodeId, int entryCount);

    private void LogBufferedSetPropertiesReplaySkipped(Guid sceneId, Guid nodeId, int entryCount)
        => LogBufferedSetPropertiesReplaySkipped(this.logger, sceneId, nodeId, entryCount);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Buffered SetProperties replay rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogBufferedSetPropertiesReplayRejected(ILogger logger, Exception exception, Guid sceneId, Guid nodeId, int entryCount);

    private void LogBufferedSetPropertiesReplayRejected(Exception ex, Guid sceneId, Guid nodeId, int entryCount)
        => LogBufferedSetPropertiesReplayRejected(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to request targeted transform propagation")]
    private static partial void LogFailedToRequestTransformPropagation(ILogger logger, Exception exception);

    private void LogFailedToRequestTransformPropagation(Exception ex)
        => LogFailedToRequestTransformPropagation(this.logger, ex);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Failed to dump scene transforms for debug")]
    private static partial void LogFailedToDumpSceneTransformsForDebug(ILogger logger, Exception exception);

    private void LogFailedToDumpSceneTransformsForDebug(Exception ex)
        => LogFailedToDumpSceneTransformsForDebug(this.logger, ex);

    [LoggerMessage(Level = LogLevel.Debug, Message = "SyncSceneAsync: Applying hierarchy and components for {Count} root nodes")]
    private static partial void LogApplyingHierarchyAndComponents(ILogger logger, int count);

    private void LogApplyingHierarchyAndComponents(int count)
        => LogApplyingHierarchyAndComponents(this.logger, count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "SyncSceneAsync: Propagating transforms")]
    private static partial void LogPropagatingTransforms(ILogger logger);

    private void LogPropagatingTransforms()
        => LogPropagatingTransforms(this.logger);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to detach light component from node {NodeId}")]
    private static partial void LogFailedToDetachLightComponent(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToDetachLightComponent(Exception ex, Guid nodeId)
        => LogFailedToDetachLightComponent(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to detach camera component from node {NodeId}")]
    private static partial void LogFailedToDetachCameraComponent(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToDetachCameraComponent(Exception ex, Guid nodeId)
        => LogFailedToDetachCameraComponent(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live material override sync is unsupported for node {NodeId}")]
    private static partial void LogMaterialOverrideSyncUnsupported(ILogger logger, Guid nodeId);

    private void LogMaterialOverrideSyncUnsupported(Guid nodeId)
        => LogMaterialOverrideSyncUnsupported(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live targeted material override sync is unsupported for node {NodeId}, LOD {LodIndex}, submesh {SubmeshIndex}")]
    private static partial void LogTargetedMaterialOverrideSyncUnsupported(ILogger logger, Guid nodeId, int lodIndex, int submeshIndex);

    private void LogTargetedMaterialOverrideSyncUnsupported(Guid nodeId, int lodIndex, int submeshIndex)
        => LogTargetedMaterialOverrideSyncUnsupported(this.logger, nodeId, lodIndex, submeshIndex);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live material override removal is unsupported for node {NodeId}, slot {SlotType}")]
    private static partial void LogMaterialOverrideRemovalUnsupported(ILogger logger, Guid nodeId, string slotType);

    private void LogMaterialOverrideRemovalUnsupported(Guid nodeId, string slotType)
        => LogMaterialOverrideRemovalUnsupported(this.logger, nodeId, slotType);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live targeted material override removal is unsupported for node {NodeId}, LOD {LodIndex}, submesh {SubmeshIndex}, slot {SlotType}")]
    private static partial void LogTargetedMaterialOverrideRemovalUnsupported(ILogger logger, Guid nodeId, int lodIndex, int submeshIndex, string slotType);

    private void LogTargetedMaterialOverrideRemovalUnsupported(Guid nodeId, int lodIndex, int submeshIndex, string slotType)
        => LogTargetedMaterialOverrideRemovalUnsupported(this.logger, nodeId, lodIndex, submeshIndex, slotType);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live LOD policy sync is unsupported for node {NodeId}")]
    private static partial void LogLodPolicySyncUnsupported(ILogger logger, Guid nodeId);

    private void LogLodPolicySyncUnsupported(Guid nodeId)
        => LogLodPolicySyncUnsupported(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live rendering-settings sync is unsupported for node {NodeId}")]
    private static partial void LogRenderingSettingsSyncUnsupported(ILogger logger, Guid nodeId);

    private void LogRenderingSettingsSyncUnsupported(Guid nodeId)
        => LogRenderingSettingsSyncUnsupported(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Live lighting-settings sync is unsupported for node {NodeId}")]
    private static partial void LogLightingSettingsSyncUnsupported(ILogger logger, Guid nodeId);

    private void LogLightingSettingsSyncUnsupported(Guid nodeId)
        => LogLightingSettingsSyncUnsupported(this.logger, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to attach camera component to node {NodeId}")]
    private static partial void LogFailedToAttachCameraComponent(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToAttachCameraComponent(Exception ex, Guid nodeId)
        => LogFailedToAttachCameraComponent(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Error, Message = "Failed to attach light component to node {NodeId}")]
    private static partial void LogFailedToAttachLightComponent(ILogger logger, Exception exception, Guid nodeId);

    private void LogFailedToAttachLightComponent(Exception ex, Guid nodeId)
        => LogFailedToAttachLightComponent(this.logger, ex, nodeId);

    [LoggerMessage(Level = LogLevel.Warning, Message = "Camera component type {CameraType} on node {NodeId} is not supported by live engine sync")]
    private static partial void LogUnsupportedCameraComponent(ILogger logger, string cameraType, Guid nodeId);

    private void LogUnsupportedCameraComponent(string cameraType, Guid nodeId)
        => LogUnsupportedCameraComponent(this.logger, cameraType, nodeId);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateAllNodesAsync: Enqueueing creation for root node {NodeName} ({NodeId})")]
    private static partial void LogEnqueueingRootNodeCreation(ILogger logger, string nodeName, Guid nodeId);

    private void LogEnqueueingRootNodeCreation(SceneNode node)
        => LogEnqueueingRootNodeCreation(this.logger, node.Name, node.Id);

    [LoggerMessage(Level = LogLevel.Debug, Message = "CreateAllNodesAsync: All {Count} creation tasks completed")]
    private static partial void LogAllNodeCreationTasksCompleted(ILogger logger, int count);

    private void LogAllNodeCreationTasksCompleted(int count)
        => LogAllNodeCreationTasksCompleted(this.logger, count);

    [LoggerMessage(Level = LogLevel.Debug, Message = "ApplyHierarchyAndComponents: Processing root node {NodeName} ({NodeId})")]
    private static partial void LogProcessingHierarchyRootNode(ILogger logger, string nodeName, Guid nodeId);

    private void LogProcessingHierarchyRootNode(SceneNode node)
        => LogProcessingHierarchyRootNode(this.logger, node.Name, node.Id);

    [LoggerMessage(Level = LogLevel.Information, Message = "SceneTransform: node='{NodeName}' parent='{ParentId}' pos=({X:0.00},{Y:0.00},{Z:0.00}) " +
        "scale=({SX:0.00},{SY:0.00},{SZ:0.00}) rot=({RX:0.00},{RY:0.00},{RZ:0.00},{RW:0.00})")]
    private static partial void LogSceneTransform(ILogger logger, string nodeName, string parentId, float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz, float rw);

    private void LogSceneTransform(SceneNode node, Guid? parentGuid)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (transform is null)
        {
            this.LogSceneTransformHasNoComponent(node, parentGuid);
            return;
        }

        var parentId = parentGuid.HasValue ? parentGuid.GetValueOrDefault().ToString() : "root";
        var (pos, rot, scale) = TransformConverter.ToNative(transform);
        LogSceneTransform(
            this.logger,
            node.Name,
            parentId,
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
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var parentId = parentGuid.HasValue ? parentGuid.GetValueOrDefault().ToString() : "root";
        LogSceneTransformHasNoComponent(this.logger, node.Name, parentId);
    }
}
