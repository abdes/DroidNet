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
public sealed partial class SceneEngineSync : ISceneEngineSync
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
    public async Task SyncSceneWhenReadyAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        if (this.engineService.State != EngineServiceState.Running)
        {
            this.LogEngineNotRunningDeferringSceneSync(scene);
        }

        while (!cancellationToken.IsCancellationRequested)
        {
            var state = this.engineService.State;
            if (state == EngineServiceState.Running)
            {
                break;
            }

            if (state is EngineServiceState.NoEngine or EngineServiceState.Faulted)
            {
                this.LogEngineNotAvailableSkippingSceneSync(scene);
                return;
            }

            await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken).ConfigureAwait(false);
        }

        if (cancellationToken.IsCancellationRequested)
        {
            return;
        }

        await this.SyncSceneAsync(scene).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task SyncSceneAsync(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogOxygenWorldNotAvailableSkippingSceneSync(scene);
            return;
        }

        this.LogSceneTransforms(scene);

        try
        {
            // Ensure any existing scene is torn down on the engine thread
            // before creating a new one to avoid races during traversal.
            world.DestroyScene();

            // Create (or recreate) the scene in the engine and wait for
            // the native command to complete so subsequent node creation
            // occurs against the new scene.
            var created = await world.CreateSceneAsync(scene.Name).ConfigureAwait(false);
            if (!created)
            {
                this.LogFailedToSyncSceneWithEngine(new InvalidOperationException("CreateSceneAsync failed"), scene);
                return;
            }
            this.LogCreatedSceneInEngine(scene);

            // Phase 1: Create all nodes without parenting
            await this.CreateAllNodesAsync(scene, world).ConfigureAwait(false);

            // Phase 2: Resolve parent-child links and apply transforms/geometry
            this.ApplyHierarchyAndComponents(scene, world);

            // Phase 3: Update world transforms
            this.PropagateTransforms(scene, world);
        }
        catch (Exception ex)
        {
            this.LogFailedToSyncSceneWithEngine(ex, scene);
        }
    }

    /// <inheritdoc/>
    public async Task CreateNodeAsync(SceneNode node, Guid? parentGuid = null)
    {
        ArgumentNullException.ThrowIfNull(node);

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotCreateNode(node);
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

            this.LogCreatedAndInitializedNode(node);
        }
        catch (Exception ex)
        {
            this.LogFailedToCreateNode(ex, node);
            throw;
        }
    }

    /// <inheritdoc/>
    public Task RemoveNodeAsync(Guid nodeId)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotRemoveNode(nodeId);
            return Task.CompletedTask;
        }

        try
        {
            world.RemoveSceneNode(nodeId);
            this.LogRemovedNode(nodeId);
        }
        catch (Exception ex)
        {
            this.LogFailedToRemoveNode(ex, nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveNodeHierarchyAsync(Guid rootNodeId)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotRemoveNodeHierarchy(rootNodeId);
            return Task.CompletedTask;
        }

        // OxygenWorld's RemoveSceneNode handles hierarchy destruction if children exist.
        world.RemoveSceneNode(rootNodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveNodeHierarchiesAsync(IReadOnlyList<Guid> rootNodeIds)
    {
        if (rootNodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotRemoveNodeHierarchies();
            return Task.CompletedTask;
        }

        world.RemoveSceneNodes(rootNodeIds.ToArray());
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ReparentNodeAsync(Guid nodeId, Guid? newParentGuid, bool preserveWorldTransform = false)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotReparentNode(nodeId);
            return Task.CompletedTask;
        }

        try
        {
            world.ReparentSceneNode(nodeId, newParentGuid, preserveWorldTransform);
            this.LogReparentedNode(nodeId, newParentGuid);
        }
        catch (Exception ex)
        {
            this.LogFailedToReparentNode(ex, nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ReparentHierarchiesAsync(IReadOnlyList<Guid> nodeIds, Guid? newParentGuid, bool preserveWorldTransform = false)
    {
        if (nodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotReparentHierarchies();
            return Task.CompletedTask;
        }

        world.ReparentSceneNodes(nodeIds.ToArray(), newParentGuid, preserveWorldTransform);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateNodeTransformAsync(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotUpdateTransform(node);
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyTransform(world, node);
            this.LogUpdatedTransform(node);
        }
        catch (Exception ex)
        {
            this.LogFailedToUpdateTransform(ex, node);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task AttachGeometryAsync(SceneNode node, GeometryComponent geometry)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(geometry);

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotAttachGeometry(node);
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyGeometry(world, node, geometry);
            this.LogAttachedGeometry(node);
        }
        catch (Exception ex)
        {
            this.LogFailedToAttachGeometry(ex, node);
        }

        return Task.CompletedTask;
    }

    /// <summary>
    ///     Extracts mesh type from a geometry URI.
    /// </summary>
    private static string? ExtractMeshTypeFromUri(string? uri)
        => string.IsNullOrEmpty(uri) ? null : uri.Split('/', StringSplitOptions.RemoveEmptyEntries).LastOrDefault();

    private Oxygen.Interop.World.OxygenWorld? TryGetWorld()
    {
        try
        {
            return this.engineService.World;
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    /// <inheritdoc/>
    public Task DetachGeometryAsync(Guid nodeId)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotDetachGeometry(nodeId);
            return Task.CompletedTask;
        }

        try
        {
            // Request engine to detach geometry from the node
            world.DetachGeometry(nodeId);
            this.LogDetachedGeometry(nodeId);
        }
        catch (Exception ex)
        {
            this.LogFailedToDetachGeometry(ex, nodeId);
        }

        return Task.CompletedTask;
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
        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
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
        // FIXME: if the asset reference is null, the geometry component should be removed from the node in the engine.
        if (geometry.Geometry is null)
        {
            return;
        }

        var meshType = ExtractMeshTypeFromUri(geometry.Geometry.Uri?.ToString());
        if (!string.IsNullOrEmpty(meshType))
        {
            world.CreateBasicMesh(node.Id, meshType);
        }
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
                this.LogNodeCreationNotSuccessful(node);
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
                this.LogFailedToReparentNode(ex, node);
            }

            // Apply transform if present
            try
            {
                this.ApplyTransform(world, node);
            }
            catch (Exception ex)
            {
                this.LogFailedToSetTransform(ex, node);
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
                    this.LogFailedToAttachGeometry(ex, node);
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
                world.UpdateTransformsForNodes([.. handles]);
            }
        }
        catch (Exception ex)
        {
            this.LogFailedToRequestTransformPropagation(ex);
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
                        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
                        if (transform is not null)
                        {
                            // Delegate logging of transform details to helper so it can extract values itself
                            this.LogSceneTransform(node, parentGuid);
                        }
                        else
                        {
                            this.LogSceneTransformHasNoComponent(node, parentGuid);
                        }
                    });
            }
        }
        catch (Exception ex)
        {
            this.LogFailedToDumpSceneTransformsForDebug(ex);
        }
    }
}
