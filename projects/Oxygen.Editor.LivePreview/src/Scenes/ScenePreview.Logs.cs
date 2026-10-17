// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>
///     Logging helpers for <see cref="ScenePreview"/>.
/// </summary>
internal sealed partial class ScenePreview
{
    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Engine is not running (State={EngineState}); deferring scene sync for '{SceneName}'")]
    private static partial void LogEngineNotRunningDeferringSceneSync(ILogger logger, EngineServiceState engineState, string sceneName);

    private void LogEngineNotRunningDeferringSceneSync(Scene scene)
        => LogEngineNotRunningDeferringSceneSync(this.logger, this.engineService.State, scene.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available (EngineState={EngineState}); skipping scene sync for '{SceneName}'")]
    private static partial void LogOxygenWorldNotAvailableSkippingSceneSync(ILogger logger, EngineServiceState engineState, string sceneName);

    private void LogOxygenWorldNotAvailableSkippingSceneSync(Scene scene)
        => LogOxygenWorldNotAvailableSkippingSceneSync(this.logger, this.engineService.State, scene.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Following scene document {DocumentId} ('{DocumentTitle}')")]
    private static partial void LogFollowingDocument(ILogger logger, DroidNet.Documents.DocumentId documentId, string documentTitle);

    [Conditional("DEBUG")]
    private void LogFollowingDocument(Oxygen.Editor.Documents.Document document)
        => LogFollowingDocument(this.logger, document.Id, document.Title);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Showing scene '{SceneName}' of document {DocumentId} in the engine")]
    private static partial void LogShowingDocument(ILogger logger, string sceneName, DroidNet.Documents.DocumentId documentId);

    private void LogShowingDocument(Scene scene, DroidNet.Documents.DocumentId documentId)
        => LogShowingDocument(this.logger, scene.Name, documentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Retired the engine projection of scene '{SceneName}' (document {DocumentId}): {Reason}")]
    private static partial void LogRetiredDocument(ILogger logger, string sceneName, DroidNet.Documents.DocumentId documentId, string reason);

    private void LogRetiredDocument(Scene scene, DroidNet.Documents.DocumentId documentId, string reason)
        => LogRetiredDocument(this.logger, scene.Name, documentId, reason);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The engine left Running (State={EngineState}); {Count} scene documents are no longer ready")]
    private static partial void LogReadinessCleared(ILogger logger, EngineServiceState engineState, int count);

    private void LogReadinessCleared(EngineServiceState state, int count)
        => LogReadinessCleared(this.logger, state, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "The engine runs again (run {RunId}); showing scene '{SceneName}'")]
    private static partial void LogResumingScene(ILogger logger, Guid runId, string sceneName);

    private void LogResumingScene(Guid runId, Scene scene)
        => LogResumingScene(this.logger, runId, scene.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Created scene '{SceneName}' in engine")]
    private static partial void LogCreatedSceneInEngine(ILogger logger, string sceneName);

    private void LogCreatedSceneInEngine(Scene scene)
        => LogCreatedSceneInEngine(this.logger, scene.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to sync scene '{SceneName}' with engine")]
    private static partial void LogFailedToSyncSceneWithEngine(ILogger logger, Exception exception, string sceneName);

    private void LogFailedToSyncSceneWithEngine(Exception ex, Scene scene)
        => LogFailedToSyncSceneWithEngine(this.logger, ex, scene.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot create node '{NodeName}'")]
    private static partial void LogCannotCreateNode(ILogger logger, string nodeName);

    private void LogCannotCreateNode(SceneNode node)
        => LogCannotCreateNode(this.logger, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Created and initialized node '{NodeName}' in engine")]
    private static partial void LogCreatedAndInitializedNode(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogCreatedAndInitializedNode(SceneNode node)
        => LogCreatedAndInitializedNode(this.logger, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to create node '{NodeName}' in engine")]
    private static partial void LogFailedToCreateNode(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToCreateNode(Exception ex, SceneNode node)
        => LogFailedToCreateNode(this.logger, ex, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot remove node '{NodeId}'")]
    private static partial void LogCannotRemoveNode(ILogger logger, NodeId nodeId);

    private void LogCannotRemoveNode(NodeId nodeId)
        => LogCannotRemoveNode(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Removed node '{NodeId}' from engine")]
    private static partial void LogRemovedNode(ILogger logger, NodeId nodeId);

    [Conditional("DEBUG")]
    private void LogRemovedNode(NodeId nodeId)
        => LogRemovedNode(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to remove node '{NodeId}' from engine")]
    private static partial void LogFailedToRemoveNode(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToRemoveNode(Exception ex, NodeId nodeId)
        => LogFailedToRemoveNode(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot remove node hierarchy '{NodeId}'")]
    private static partial void LogCannotRemoveNodeHierarchy(ILogger logger, NodeId nodeId);

    private void LogCannotRemoveNodeHierarchy(NodeId nodeId)
        => LogCannotRemoveNodeHierarchy(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot remove node hierarchies")]
    private static partial void LogCannotRemoveNodeHierarchies(ILogger logger);

    private void LogCannotRemoveNodeHierarchies()
        => LogCannotRemoveNodeHierarchies(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot reparent node '{NodeId}'")]
    private static partial void LogCannotReparentNode(ILogger logger, NodeId nodeId);

    private void LogCannotReparentNode(NodeId nodeId)
        => LogCannotReparentNode(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Reparented node {NodeId} -> {ParentId}")]
    private static partial void LogReparentedNode(ILogger logger, NodeId nodeId, NodeId? parentId);

    [Conditional("DEBUG")]
    private void LogReparentedNode(NodeId nodeId, NodeId? parentId)
        => LogReparentedNode(this.logger, nodeId, parentId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to reparent node '{NodeId}'")]
    private static partial void LogFailedToReparentNode(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToReparentNode(Exception ex, NodeId nodeId)
        => LogFailedToReparentNode(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot reparent hierarchies")]
    private static partial void LogCannotReparentHierarchies(ILogger logger);

    private void LogCannotReparentHierarchies()
        => LogCannotReparentHierarchies(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot attach geometry to '{NodeName}'")]
    private static partial void LogCannotAttachGeometry(ILogger logger, string nodeName);

    private void LogCannotAttachGeometry(SceneNode node)
        => LogCannotAttachGeometry(this.logger, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Attached geometry to node '{NodeName}'")]
    private static partial void LogAttachedGeometry(ILogger logger, string nodeName);

    [Conditional("DEBUG")]
    private void LogAttachedGeometry(SceneNode node)
        => LogAttachedGeometry(this.logger, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "OxygenWorld is not available; cannot detach geometry for node '{NodeId}'")]
    private static partial void LogCannotDetachGeometry(ILogger logger, NodeId nodeId);

    private void LogCannotDetachGeometry(NodeId nodeId)
        => LogCannotDetachGeometry(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "Detached geometry (hidden) for node '{NodeId}'")]
    private static partial void LogDetachedGeometry(ILogger logger, NodeId nodeId);

    [Conditional("DEBUG")]
    private void LogDetachedGeometry(NodeId nodeId)
        => LogDetachedGeometry(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to detach geometry for node '{NodeId}'")]
    private static partial void LogFailedToDetachGeometry(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToDetachGeometry(Exception ex, NodeId nodeId)
        => LogFailedToDetachGeometry(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to attach geometry to node '{NodeName}'")]
    private static partial void LogFailedToAttachGeometry(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToAttachGeometry(Exception ex, SceneNode node)
        => LogFailedToAttachGeometry(this.logger, ex, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Node '{NodeName}' creation was not successful")]
    private static partial void LogNodeCreationNotSuccessful(ILogger logger, string nodeName);

    private void LogNodeCreationNotSuccessful(SceneNode node)
        => LogNodeCreationNotSuccessful(this.logger, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to reparent node '{NodeName}'")]
    private static partial void LogFailedToReparentNode(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToReparentNode(Exception ex, SceneNode node)
        => LogFailedToReparentNode(this.logger, ex, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to set transform for node '{NodeName}'")]
    private static partial void LogFailedToSetTransform(ILogger logger, Exception exception, string nodeName);

    private void LogFailedToSetTransform(Exception ex, SceneNode node)
        => LogFailedToSetTransform(this.logger, ex, node.Name);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SetProperties buffered. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount} PendingCount={PendingCount}")]
    private static partial void LogSetPropertiesBuffered(ILogger logger, SceneId sceneId, NodeId nodeId, int entryCount, int pendingCount);

    private void LogSetPropertiesBuffered(SceneId sceneId, NodeId nodeId, int entryCount, int pendingCount)
        => LogSetPropertiesBuffered(this.logger, sceneId, nodeId, entryCount, pendingCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SetProperties skipped. SceneId={SceneId} NodeId={NodeId} Status={Status} Code={Code} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesSkipped(ILogger logger, SceneId sceneId, NodeId nodeId, SyncStatus status, string code, int entryCount);

    private void LogSetPropertiesSkipped(SceneId sceneId, NodeId nodeId, SyncStatus status, string code, int entryCount)
        => LogSetPropertiesSkipped(this.logger, sceneId, nodeId, status, code, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SetProperties enqueued. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesEnqueued(ILogger logger, SceneId sceneId, NodeId nodeId, int entryCount);

    [Conditional("DEBUG")]
    private void LogSetPropertiesEnqueued(SceneId sceneId, NodeId nodeId, int entryCount)
        => LogSetPropertiesEnqueued(this.logger, sceneId, nodeId, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "SetProperties rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesRejected(ILogger logger, Exception exception, SceneId sceneId, NodeId nodeId, int entryCount);

    private void LogSetPropertiesRejected(Exception ex, SceneId sceneId, NodeId nodeId, int entryCount)
        => LogSetPropertiesRejected(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "SetProperties failed. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogSetPropertiesFailed(ILogger logger, Exception exception, SceneId sceneId, NodeId nodeId, int entryCount);

    private void LogSetPropertiesFailed(Exception ex, SceneId sceneId, NodeId nodeId, int entryCount)
        => LogSetPropertiesFailed(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Buffered SetProperties replay completed. SceneId={SceneId} PendingCount={PendingCount} ReplayedCount={ReplayedCount} SkippedCount={SkippedCount}")]
    private static partial void LogBufferedSetPropertiesReplayCompleted(ILogger logger, SceneId sceneId, int pendingCount, int replayedCount, int skippedCount);

    private void LogBufferedSetPropertiesReplayCompleted(SceneId sceneId, int pendingCount, int replayedCount, int skippedCount)
        => LogBufferedSetPropertiesReplayCompleted(this.logger, sceneId, pendingCount, replayedCount, skippedCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "Buffered SetProperties replay skipped because the node no longer exists. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogBufferedSetPropertiesReplaySkipped(ILogger logger, SceneId sceneId, NodeId nodeId, int entryCount);

    private void LogBufferedSetPropertiesReplaySkipped(SceneId sceneId, NodeId nodeId, int entryCount)
        => LogBufferedSetPropertiesReplaySkipped(this.logger, sceneId, nodeId, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Buffered SetProperties replay rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}")]
    private static partial void LogBufferedSetPropertiesReplayRejected(ILogger logger, Exception exception, SceneId sceneId, NodeId nodeId, int entryCount);

    private void LogBufferedSetPropertiesReplayRejected(Exception ex, SceneId sceneId, NodeId nodeId, int entryCount)
        => LogBufferedSetPropertiesReplayRejected(this.logger, ex, sceneId, nodeId, entryCount);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to request targeted transform propagation")]
    private static partial void LogFailedToRequestTransformPropagation(ILogger logger, Exception exception);

    private void LogFailedToRequestTransformPropagation(Exception ex)
        => LogFailedToRequestTransformPropagation(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Failed to dump scene transforms for debug")]
    private static partial void LogFailedToDumpSceneTransformsForDebug(ILogger logger, Exception exception);

    private void LogFailedToDumpSceneTransformsForDebug(Exception ex)
        => LogFailedToDumpSceneTransformsForDebug(this.logger, ex);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SyncSceneAsync: Applying hierarchy and components for {Count} root nodes")]
    private static partial void LogApplyingHierarchyAndComponents(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogApplyingHierarchyAndComponents(int count)
        => LogApplyingHierarchyAndComponents(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "SyncSceneAsync: Propagating transforms")]
    private static partial void LogPropagatingTransforms(ILogger logger);

    [Conditional("DEBUG")]
    private void LogPropagatingTransforms()
        => LogPropagatingTransforms(this.logger);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to detach light component from node {NodeId}")]
    private static partial void LogFailedToDetachLightComponent(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToDetachLightComponent(Exception ex, NodeId nodeId)
        => LogFailedToDetachLightComponent(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to detach camera component from node {NodeId}")]
    private static partial void LogFailedToDetachCameraComponent(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToDetachCameraComponent(Exception ex, NodeId nodeId)
        => LogFailedToDetachCameraComponent(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Live LOD policy sync is unsupported for node {NodeId}")]
    private static partial void LogLodPolicySyncUnsupported(ILogger logger, NodeId nodeId);

    private void LogLodPolicySyncUnsupported(NodeId nodeId)
        => LogLodPolicySyncUnsupported(this.logger, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to attach camera component to node {NodeId}")]
    private static partial void LogFailedToAttachCameraComponent(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToAttachCameraComponent(Exception ex, NodeId nodeId)
        => LogFailedToAttachCameraComponent(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to attach light component to node {NodeId}")]
    private static partial void LogFailedToAttachLightComponent(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToAttachLightComponent(Exception ex, NodeId nodeId)
        => LogFailedToAttachLightComponent(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to attach local fog volume to node {NodeId}")]
    private static partial void LogFailedToAttachLocalFogVolume(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToAttachLocalFogVolume(Exception ex, NodeId nodeId)
        => LogFailedToAttachLocalFogVolume(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Failed to apply authored rendering flags to node {NodeId}")]
    private static partial void LogFailedToApplyNodeRendering(ILogger logger, Exception exception, NodeId nodeId);

    private void LogFailedToApplyNodeRendering(Exception ex, NodeId nodeId)
        => LogFailedToApplyNodeRendering(this.logger, ex, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Warning,
        Message = "Camera component type {CameraType} on node {NodeId} is not supported by live engine sync")]
    private static partial void LogUnsupportedCameraComponent(ILogger logger, string cameraType, NodeId nodeId);

    private void LogUnsupportedCameraComponent(string cameraType, NodeId nodeId)
        => LogUnsupportedCameraComponent(this.logger, cameraType, nodeId);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateAllNodesAsync: Enqueueing creation for root node {NodeName} ({NodeId})")]
    private static partial void LogEnqueueingRootNodeCreation(ILogger logger, string nodeName, NodeId nodeId);

    [Conditional("DEBUG")]
    private void LogEnqueueingRootNodeCreation(SceneNode node)
        => LogEnqueueingRootNodeCreation(this.logger, node.Name, node.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "CreateAllNodesAsync: All {Count} creation tasks completed")]
    private static partial void LogAllNodeCreationTasksCompleted(ILogger logger, int count);

    [Conditional("DEBUG")]
    private void LogAllNodeCreationTasksCompleted(int count)
        => LogAllNodeCreationTasksCompleted(this.logger, count);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Debug,
        Message = "ApplyHierarchyAndComponents: Processing root node {NodeName} ({NodeId})")]
    private static partial void LogProcessingHierarchyRootNode(ILogger logger, string nodeName, NodeId nodeId);

    [Conditional("DEBUG")]
    private void LogProcessingHierarchyRootNode(SceneNode node)
        => LogProcessingHierarchyRootNode(this.logger, node.Name, node.Id);

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SceneTransform: node='{NodeName}' parent='{ParentId}' pos=({X:0.00},{Y:0.00},{Z:0.00}) " + "scale=({SX:0.00},{SY:0.00},{SZ:0.00}) rot=({RX:0.00},{RY:0.00},{RZ:0.00},{RW:0.00})")]
    private static partial void LogSceneTransform(ILogger logger, string nodeName, string parentId, float x, float y, float z, float sx, float sy, float sz, float rx, float ry, float rz, float rw);

    private void LogSceneTransform(SceneNode node, NodeId? parentGuid)
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

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Information,
        Message = "SceneTransform: node='{NodeName}' parent='{ParentId}' has NO Transform component")]
    private static partial void LogSceneTransformHasNoComponent(ILogger logger, string nodeName, string parentId);

    private void LogSceneTransformHasNoComponent(SceneNode node, NodeId? parentGuid)
    {
        if (!this.logger.IsEnabled(LogLevel.Information))
        {
            return;
        }

        var parentId = parentGuid.HasValue ? parentGuid.GetValueOrDefault().ToString() : "root";
        LogSceneTransformHasNoComponent(this.logger, node.Name, parentId);
    }

    [LoggerMessage(
        SkipEnabledCheck = true,
        Level = LogLevel.Error,
        Message = "Projecting a scene change ({Change}) into the engine failed")]
    private static partial void LogProjectionFailed(ILogger logger, Exception exception, string change);

    private void LogProjectionFailed(Exception exception, string change)
        => LogProjectionFailed(this.logger, exception, change);
}
