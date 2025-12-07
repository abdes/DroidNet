// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.WorldEditor.Services;

/// <summary>
///     Default implementation of <see cref="ISceneEngineSync"/> that synchronizes scene data
///     with the native rendering engine through the <see cref="IEngineService"/>.
/// </summary>
public sealed class SceneEngineSync : ISceneEngineSync
{
    private readonly IEngineService engineService;
    private readonly ILogger<SceneEngineSync> logger;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneEngineSync"/> class.
    /// </summary>
    /// <param name="engineService">The engine service for interop with the rendering engine.</param>
    /// <param name="loggerFactory">Optional logger factory for diagnostic logging.</param>
    public SceneEngineSync(IEngineService engineService, ILoggerFactory? loggerFactory = null)
    {
        this.engineService = engineService ?? throw new ArgumentNullException(nameof(engineService));
        this.logger = loggerFactory?.CreateLogger<SceneEngineSync>() ??
                      NullLoggerFactory.Instance.CreateLogger<SceneEngineSync>();
    }

    /// <inheritdoc/>
    public async Task SyncSceneAsync(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; skipping scene sync for '{SceneName}'", scene.Name);
            return;
        }

        this.LogSceneTransforms(scene);

        try
        {
            // Create (or recreate) the scene in the engine
            world.CreateScene(scene.Name);
            this.logger.LogInformation("Created scene '{SceneName}' in engine", scene.Name);

            // Phase 1: Create all nodes without parenting
            await this.CreateAllNodesAsync(scene, world).ConfigureAwait(false);

            // Phase 2: Resolve parent-child links and apply transforms/geometry
            this.ApplyHierarchyAndComponents(scene, world);

            // Phase 3: Update world transforms
            this.PropagateTransforms(scene, world);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to sync scene '{SceneName}' with engine", scene.Name);
        }
    }

    /// <inheritdoc/>
    public async Task CreateNodeAsync(SceneNode node, Guid? parentGuid = null)
    {
        ArgumentNullException.ThrowIfNull(node);

        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; cannot create node '{NodeName}'", node.Name);
            return;
        }

        try
        {
            await this.CreateNodeWithCallbackAsync(
                world,
                node,
                parentGuid,
                initializeWorldAsRoot: parentGuid is null).ConfigureAwait(false);

            this.ApplyTransform(world, node);

            this.logger.LogDebug("Created and initialized node '{NodeName}' in engine", node.Name);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to create node '{NodeName}' in engine", node.Name);
            throw;
        }
    }

    /// <inheritdoc/>
    public Task RemoveNodeAsync(Guid nodeId)
    {
        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; cannot remove node '{NodeId}'", nodeId);
            return Task.CompletedTask;
        }

        try
        {
            world.RemoveSceneNode(nodeId);
            this.logger.LogDebug("Removed node '{NodeId}' from engine", nodeId);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to remove node '{NodeId}' from engine", nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ReparentNodeAsync(Guid nodeId, Guid? newParentGuid, bool preserveWorldTransform = false)
    {
        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; cannot reparent node '{NodeId}'", nodeId);
            return Task.CompletedTask;
        }

        try
        {
            world.ReparentSceneNode(nodeId, newParentGuid, preserveWorldTransform);
            this.logger.LogDebug("Reparented node {NodeId} -> {ParentId}", nodeId, newParentGuid);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to reparent node '{NodeId}'", nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateNodeTransformAsync(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);

        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; cannot update transform for '{NodeName}'", node.Name);
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyTransform(world, node);
            this.logger.LogDebug("Updated transform for node '{NodeName}'", node.Name);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to update transform for node '{NodeName}'", node.Name);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task AttachGeometryAsync(SceneNode node, GeometryComponent geometry)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(geometry);

        var world = this.engineService.World;
        if (world is null)
        {
            this.logger.LogWarning("OxygenWorld is not available; cannot attach geometry to '{NodeName}'", node.Name);
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyGeometry(world, node, geometry);
            this.logger.LogDebug("Attached geometry to node '{NodeName}'", node.Name);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to attach geometry to node '{NodeName}'", node.Name);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task DetachGeometryAsync(Guid nodeId)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("DetachGeometryAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task UpdateMaterialOverrideAsync(Guid nodeId, OverrideSlot slot)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("UpdateMaterialOverrideAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task UpdateTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, OverrideSlot slot)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("UpdateTargetedMaterialOverrideAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task RemoveMaterialOverrideAsync(Guid nodeId, Type slotType)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("RemoveMaterialOverrideAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task RemoveTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, Type slotType)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("RemoveTargetedMaterialOverrideAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task UpdateLodPolicyAsync(Guid nodeId, LevelOfDetailSlot lodSlot)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("UpdateLodPolicyAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task UpdateRenderingSettingsAsync(Guid nodeId, RenderingSlot renderingSlot)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("UpdateRenderingSettingsAsync will be implemented when engine API supports it.");
    }

    /// <inheritdoc/>
    public Task UpdateLightingSettingsAsync(Guid nodeId, LightingSlot lightingSlot)
    {
        // TODO: Implement when engine API is available
        throw new NotImplementedException("UpdateLightingSettingsAsync will be implemented when engine API supports it.");
    }

    /// <summary>
    ///     Creates a scene node with a thread-safe callback that marshals property changes to the UI thread.
    /// </summary>
    private Task<bool> CreateNodeWithCallbackAsync(
        Oxygen.Interop.World.OxygenWorld world,
        SceneNode node,
        Guid? parentGuid,
        bool initializeWorldAsRoot)
    {
        var syncContext = SynchronizationContext.Current;
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);

        world.CreateSceneNode(
            node.Name,
            node.Id,
            parentGuid,
            handle =>
            {
                // Set IsActive on UI thread to avoid cross-thread UI updates
                void SetActive()
                {
                    try
                    {
                        node.IsActive = true;
                        tcs.SetResult(true);
                    }
                    catch (Exception ex)
                    {
                        tcs.SetException(ex);
                    }
                }

                if (syncContext is not null)
                {
                    syncContext.Post(_ => SetActive(), null);
                }
                else
                {
                    SetActive();
                }
            },
            initializeWorldAsRoot);

        return tcs.Task;
    }

    /// <summary>
    ///     Applies a transform component to a scene node in the engine.
    /// </summary>
    private void ApplyTransform(Oxygen.Interop.World.OxygenWorld world, SceneNode node)
    {
        var transform = node.Components.OfType<Transform>().FirstOrDefault();
        if (transform is not null)
        {
            var (position, rotation, scale) = TransformConverter.ToNative(transform);
            world.SetLocalTransform(node.Id, position, rotation, scale);
        }
    }

    /// <summary>
    ///     Applies a geometry component to a scene node in the engine.
    /// </summary>
    private void ApplyGeometry(Oxygen.Interop.World.OxygenWorld world, SceneNode node, GeometryComponent geometry)
    {
        var meshType = ExtractMeshTypeFromUri(geometry.Geometry.Uri?.ToString());
        if (!string.IsNullOrEmpty(meshType))
        {
            world.CreateBasicMesh(node.Id, meshType);
        }
    }

    /// <summary>
    ///     Extracts mesh type from a geometry URI.
    /// </summary>
    private static string? ExtractMeshTypeFromUri(string? uri)
    {
        if (string.IsNullOrEmpty(uri))
        {
            return null;
        }

        return uri.Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();
    }

    private async Task CreateAllNodesAsync(Scene scene, Oxygen.Interop.World.OxygenWorld world)
    {
        var syncContext = SynchronizationContext.Current;
        var createTasks = new List<Task<bool>>();

        void EnqueueCreateRecursive(SceneNode node)
        {
            var task = this.CreateNodeWithCallbackAsync(world, node, parentGuid: null, initializeWorldAsRoot: true);
            createTasks.Add(task);

            foreach (var child in node.Children)
            {
                EnqueueCreateRecursive(child);
            }
        }

        foreach (var root in scene.RootNodes)
        {
            EnqueueCreateRecursive(root);
        }

        await Task.WhenAll(createTasks).ConfigureAwait(false);
    }

    private void ApplyHierarchyAndComponents(Scene scene, Oxygen.Interop.World.OxygenWorld world)
    {
        void ResolveAndApply(SceneNode node, Guid? parentGuid)
        {
            if (!node.IsActive)
            {
                this.logger.LogError("Node '{NodeName}' creation was not successful", node.Name);
                scene.RootNodes.Remove(node);
                return;
            }

            // Reparent to the desired parent
            try
            {
                world.ReparentSceneNode(node.Id, parentGuid, preserveWorldTransform: false);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to reparent node '{NodeName}'", node.Name);
            }

            // Apply transform if present
            try
            {
                this.ApplyTransform(world, node);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to set transform for node '{NodeName}'", node.Name);
            }

            // Attach geometry if present
            var geometryComp = node.Components.OfType<GeometryComponent>().FirstOrDefault();
            if (geometryComp is not null)
            {
                try
                {
                    this.ApplyGeometry(world, node, geometryComp);
                }
                catch (Exception ex)
                {
                    this.logger.LogError(ex, "Failed to attach geometry for node '{NodeName}'", node.Name);
                }
            }

            foreach (var child in node.Children)
            {
                ResolveAndApply(child, node.Id);
            }
        }

        foreach (var root in scene.RootNodes)
        {
            ResolveAndApply(root, parentGuid: null);
        }
    }

    private void PropagateTransforms(Scene scene, Oxygen.Interop.World.OxygenWorld world)
    {
        try
        {
            var handles = new List<Guid>();

            foreach (var root in scene.RootNodes)
            {
                // Collect all active node IDs in the hierarchy
                SceneTraversal.TraverseDepthFirst(root, (node, _) =>
                {
                    if (node.IsActive)
                    {
                        handles.Add(node.Id);
                    }
                });
            }

            if (handles.Count > 0)
            {
                world.UpdateTransformsForNodes(handles.ToArray());
            }
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to request targeted transform propagation");
        }
    }

    private void LogSceneTransforms(Scene scene)
    {
        try
        {
            foreach (var root in scene.RootNodes)
            {
                SceneTraversal.TraverseDepthFirst(
                    root,
                    (node, parentGuid) =>
                    {
                        var transform = node.Components.OfType<Transform>().FirstOrDefault();
                        if (transform is not null)
                        {
                            var (pos, rot, scale) = TransformConverter.ToNative(transform);
                            this.logger.LogInformation(
                                "SceneTransform: node='{NodeName}' parent='{ParentId}' pos=({X:0.00},{Y:0.00},{Z:0.00}) " +
                                "scale=({SX:0.00},{SY:0.00},{SZ:0.00}) rot=({RX:0.00},{RY:0.00},{RZ:0.00},{RW:0.00})",
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
                        else
                        {
                            this.logger.LogInformation(
                                "SceneTransform: node='{NodeName}' parent='{ParentId}' has NO Transform component",
                                node.Name,
                                parentGuid?.ToString() ?? "root");
                        }
                    });
            }
        }
        catch (Exception ex)
        {
            this.logger.LogWarning(ex, "Failed to dump scene transforms for debug");
        }
    }
}
