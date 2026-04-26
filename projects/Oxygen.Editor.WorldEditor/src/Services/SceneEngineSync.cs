// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Assets.Catalog;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Services;

/// <summary>
///     Default implementation of <see cref="ISceneEngineSync"/> that synchronizes scene data
///     with the native rendering engine through the <see cref="IEngineService"/>.
/// </summary>
public sealed partial class SceneEngineSync : ISceneEngineSync
{
    private static readonly TimeSpan NodeCreationTimeout = TimeSpan.FromSeconds(10);

    private readonly IEngineService engineService;
    private readonly ILogger<SceneEngineSync> logger;
    private readonly SemaphoreSlim sceneSyncGate = new(initialCount: 1, maxCount: 1);

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
    public async Task<bool> SyncSceneWhenReadyAsync(Scene scene, CancellationToken cancellationToken = default)
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
                return false;
            }

            try
            {
                await Task.Delay(TimeSpan.FromMilliseconds(100), cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException)
            {
                return false;
            }
        }

        if (cancellationToken.IsCancellationRequested)
        {
            return false;
        }

        return await this.SyncSceneAsync(scene, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public async Task<bool> SyncSceneAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        try
        {
            await this.sceneSyncGate.WaitAsync(cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return false;
        }

        var world = this.TryGetWorld();
        if (world is null)
        {
            this.sceneSyncGate.Release();
            this.LogOxygenWorldNotAvailableSkippingSceneSync(scene);
            return false;
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();

            // Ensure any existing scene is torn down on the engine thread
            // before creating a new one to avoid races during traversal.
            world.DestroyScene();
            cancellationToken.ThrowIfCancellationRequested();

            // Create (or recreate) the scene in the engine and wait for
            // the native command to complete so subsequent node creation
            // occurs against the new scene.
            var created = await world.CreateSceneAsync(scene.Name).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            if (!created)
            {
                this.LogFailedToSyncSceneWithEngine(
                    new InvalidOperationException("CreateSceneAsync returned false"),
                    scene);
                return false;
            }

            this.LogCreatedSceneInEngine(scene);
            this.LogSceneTransforms(scene);

            // Phase 1: Create all nodes without parenting
            var nodesCreated = await this.CreateAllNodesAsync(scene, world, cancellationToken).ConfigureAwait(false);
            if (!nodesCreated)
            {
                return false;
            }

            cancellationToken.ThrowIfCancellationRequested();

            // Phase 2: Resolve parent-child links and apply transforms/geometry
            this.logger.LogDebug("SyncSceneAsync: Applying hierarchy and components for {Count} root nodes", scene.RootNodes.Count);
            this.ApplyHierarchyAndComponents(scene, world);
            cancellationToken.ThrowIfCancellationRequested();

            // Phase 3: Update world transforms
            this.logger.LogDebug("SyncSceneAsync: Propagating transforms");
            this.PropagateTransforms(scene, world);
            return true;
        }
        catch (Exception ex)
        {
            this.LogFailedToSyncSceneWithEngine(ex, scene);
            return false;
        }
        finally
        {
            this.sceneSyncGate.Release();
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
                initializeWorldAsRoot: parentGuid is null,
                CancellationToken.None).ConfigureAwait(false);

            this.ApplyTransform(world, node);
            this.ApplyRenderableComponents(world, node);

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
    public Task AttachLightAsync(SceneNode node, LightComponent light)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(light);

        var world = this.TryGetWorld();
        if (world is null)
        {
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyLight(world, node, light);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to attach light component to node {NodeId}", node.Id);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task DetachLightAsync(Guid nodeId)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            return Task.CompletedTask;
        }

        try
        {
            world.DetachLight(nodeId);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to detach light component from node {NodeId}", nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task AttachCameraAsync(SceneNode node, CameraComponent camera)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(camera);

        var world = this.TryGetWorld();
        if (world is null)
        {
            return Task.CompletedTask;
        }

        try
        {
            this.ApplyCamera(world, node, camera);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to attach camera component to node {NodeId}", node.Id);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task DetachCameraAsync(Guid nodeId)
    {
        var world = this.TryGetWorld();
        if (world is null)
        {
            return Task.CompletedTask;
        }

        try
        {
            world.DetachCamera(nodeId);
        }
        catch (Exception ex)
        {
            this.logger.LogError(ex, "Failed to detach camera component from node {NodeId}", nodeId);
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
    private async Task<bool> CreateNodeWithCallbackAsync(
        Oxygen.Interop.World.OxygenWorld world,
        SceneNode node,
        Guid? parentGuid,
        bool initializeWorldAsRoot,
        CancellationToken cancellationToken)
    {
        var syncContext = SynchronizationContext.Current;
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(NodeCreationTimeout);
        using var registration = timeout.Token.Register(static state =>
        {
            var completion = (TaskCompletionSource<bool>)state!;
            _ = completion.TrySetCanceled();
        }, tcs);

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
                        _ = tcs.TrySetResult(true);
                    }
                    catch (Exception ex)
                    {
                        _ = tcs.TrySetException(ex);
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

        try
        {
            return await tcs.Task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
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
        if (geometry.Geometry?.Uri != null)
        {
            var enginePath = AssetUriHelper.GetEnginePath(geometry.Geometry.Uri);
            world.SetGeometry(node.Id, enginePath);
        }
        else
        {
            world.DetachGeometry(node.Id);
        }
    }

    private void ApplyRenderableComponents(Oxygen.Interop.World.OxygenWorld world, SceneNode node)
    {
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

        if (node.Components.OfType<CameraComponent>().FirstOrDefault() is { } camera)
        {
            try
            {
                this.ApplyCamera(world, node, camera);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to attach camera component to node {NodeId}", node.Id);
            }
        }

        if (node.Components.OfType<LightComponent>().FirstOrDefault() is { } light)
        {
            try
            {
                this.ApplyLight(world, node, light);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to attach light component to node {NodeId}", node.Id);
            }
        }
    }

    private void ApplyCamera(Oxygen.Interop.World.OxygenWorld world, SceneNode node, CameraComponent camera)
    {
        switch (camera)
        {
            case PerspectiveCamera perspective:
                world.AttachPerspectiveCamera(
                    node.Id,
                    ToEngineFieldOfViewRadians(perspective.FieldOfView),
                    perspective.AspectRatio,
                    perspective.NearPlane,
                    perspective.FarPlane);
                break;

            default:
                this.logger.LogWarning(
                    "Camera component type {CameraType} on node {NodeId} is not supported by live engine sync",
                    camera.GetType().Name,
                    node.Id);
                break;
        }
    }

    private void ApplyLight(Oxygen.Interop.World.OxygenWorld world, SceneNode node, LightComponent light)
    {
        switch (light)
        {
            case DirectionalLightComponent directional:
                world.AttachDirectionalLight(
                    node.Id,
                    directional.IntensityLux,
                    directional.AngularSizeRadians,
                    directional.Color,
                    directional.AffectsWorld,
                    directional.CastsShadows,
                    directional.ExposureCompensation,
                    directional.EnvironmentContribution,
                    directional.IsSunLight);
                break;

            case PointLightComponent point:
                world.AttachPointLight(
                    node.Id,
                    point.LuminousFluxLumens,
                    point.Range,
                    point.SourceRadius,
                    point.DecayExponent,
                    point.Color,
                    point.AffectsWorld,
                    point.CastsShadows,
                    point.ExposureCompensation);
                break;

            case SpotLightComponent spot:
                world.AttachSpotLight(
                    node.Id,
                    spot.LuminousFluxLumens,
                    spot.Range,
                    spot.SourceRadius,
                    spot.DecayExponent,
                    spot.InnerConeAngleRadians,
                    spot.OuterConeAngleRadians,
                    spot.Color,
                    spot.AffectsWorld,
                    spot.CastsShadows,
                    spot.ExposureCompensation);
                break;
        }
    }

    private async Task<bool> CreateAllNodesAsync(Scene scene, Oxygen.Interop.World.OxygenWorld world, CancellationToken cancellationToken)
    {
        var createTasks = new List<Task<bool>>();

        void EnqueueCreateRecursive(SceneNode node)
        {
            var task = this.CreateNodeWithCallbackAsync(
                world,
                node,
                parentGuid: null,
                initializeWorldAsRoot: true,
                cancellationToken);
            createTasks.Add(task);

            foreach (var child in node.Children)
            {
                EnqueueCreateRecursive(child);
            }
        }

        foreach (var root in scene.RootNodes)
        {
            this.logger.LogDebug("CreateAllNodesAsync: Enqueueing creation for root node {NodeName} ({NodeId})", root.Name, root.Id);
            EnqueueCreateRecursive(root);
        }

        var results = await Task.WhenAll(createTasks).ConfigureAwait(false);
        this.logger.LogDebug("CreateAllNodesAsync: All {Count} creation tasks completed", createTasks.Count);
        if (results.All(static created => created))
        {
            return true;
        }

        // Some node creation callback timed out or was canceled. Wait for one
        // final SceneMutation command before releasing the sync gate so stale
        // queued node commands cannot leak into the next scene switch.
        _ = await this.CreateNodeWithCallbackAsync(
            world,
            new SceneNode(scene) { Name = "__sync_barrier__" },
            parentGuid: null,
            initializeWorldAsRoot: false,
            CancellationToken.None).ConfigureAwait(false);

        return false;
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

            this.ApplyRenderableComponents(world, node);

            foreach (var child in node.Children)
            {
                ResolveAndApply(child, node.Id);
            }
        }

        foreach (var root in scene.RootNodes)
        {
            this.logger.LogDebug("ApplyHierarchyAndComponents: Processing root node {NodeName} ({NodeId})", root.Name, root.Id);
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

    private static float ToEngineFieldOfViewRadians(float fieldOfViewDegrees)
    {
        var degrees = float.IsFinite(fieldOfViewDegrees) && fieldOfViewDegrees > 0.0f
            ? fieldOfViewDegrees
            : PerspectiveCamera.DefaultFieldOfViewDegrees;

        return degrees * (MathF.PI / 180.0f);
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
