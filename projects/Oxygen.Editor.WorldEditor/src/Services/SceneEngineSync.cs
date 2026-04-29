// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Assets.Catalog;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
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
    private readonly LiveSyncCoalescer coalescer = new();
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
    public Task<SyncOutcome> UpdateNodeTransformAsync(
        Scene scene,
        SceneNode node,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        return this.ExecuteNodeSyncAsync(
            scene,
            node,
            SceneOperationKinds.EditTransform,
            LiveSyncDiagnosticCodes.TransformRejected,
            LiveSyncDiagnosticCodes.TransformFailed,
            cancellationToken,
            world => this.ApplyTransform(world, node));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> AttachGeometryAsync(
        Scene scene,
        SceneNode node,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        var geometry = node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geometry is null)
        {
            return Task.FromResult(
                Rejected(
                    SceneOperationKinds.EditGeometry,
                    Scope(scene, node, componentType: nameof(GeometryComponent)),
                    LiveSyncDiagnosticCodes.GeometryRejected,
                    "Node has no geometry component to attach."));
        }

        var scope = Scope(scene, node, componentType: nameof(GeometryComponent));
        if (TryClassifyReadiness(
                this.engineService,
                SceneOperationKinds.EditGeometry,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        if (TryClassifyUnresolvedImportedGeometry(scene, node, geometry, out var geometryOutcome))
        {
            return Task.FromResult(geometryOutcome);
        }

        return this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditGeometry,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.GeometryRejected,
            LiveSyncDiagnosticCodes.GeometryFailed,
            cancellationToken,
            world => this.ApplyGeometry(world, node, geometry));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> DetachGeometryAsync(
        Scene scene,
        Guid nodeId,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        return this.ExecuteNodeSyncAsync(
            scene,
            node: null,
            nodeId,
            SceneOperationKinds.EditGeometry,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.GeometryRejected,
            LiveSyncDiagnosticCodes.GeometryFailed,
            cancellationToken,
            world => world.DetachGeometry(nodeId));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> AttachLightAsync(
        Scene scene,
        SceneNode node,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        var light = node.Components.OfType<LightComponent>().FirstOrDefault();
        if (light is null)
        {
            return Task.FromResult(
                Rejected(
                    SceneOperationKinds.EditDirectionalLight,
                    Scope(scene, node, componentType: nameof(LightComponent)),
                    LiveSyncDiagnosticCodes.LightRejected,
                    "Node has no light component to attach."));
        }

        return this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditDirectionalLight,
            light.GetType().Name,
            LiveSyncDiagnosticCodes.LightRejected,
            LiveSyncDiagnosticCodes.LightFailed,
            cancellationToken,
            world => this.ApplyLight(world, node, light));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> DetachLightAsync(
        Scene scene,
        Guid nodeId,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        return this.ExecuteNodeSyncAsync(
            scene,
            node: null,
            nodeId,
            SceneOperationKinds.EditDirectionalLight,
            nameof(LightComponent),
            LiveSyncDiagnosticCodes.LightRejected,
            LiveSyncDiagnosticCodes.LightFailed,
            cancellationToken,
            world => world.DetachLight(nodeId));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> AttachCameraAsync(
        Scene scene,
        SceneNode node,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        var camera = node.Components.OfType<CameraComponent>().FirstOrDefault();
        if (camera is null)
        {
            return Task.FromResult(
                Rejected(
                    SceneOperationKinds.EditPerspectiveCamera,
                    Scope(scene, node, componentType: nameof(CameraComponent)),
                    LiveSyncDiagnosticCodes.CameraRejected,
                    "Node has no camera component to attach."));
        }

        var scope = Scope(scene, node, componentType: camera.GetType().Name, componentName: camera.Name);
        if (TryClassifyReadiness(
                this.engineService,
                SceneOperationKinds.EditPerspectiveCamera,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        if (camera is not PerspectiveCamera)
        {
            return Task.FromResult(
                Unsupported(
                    SceneOperationKinds.EditPerspectiveCamera,
                    scope,
                    LiveSyncDiagnosticCodes.CameraUnsupported,
                    $"Camera component '{camera.GetType().Name}' has no live sync adapter in ED-M04."));
        }

        return this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditPerspectiveCamera,
            camera.GetType().Name,
            LiveSyncDiagnosticCodes.CameraRejected,
            LiveSyncDiagnosticCodes.CameraFailed,
            cancellationToken,
            world => this.ApplyCamera(world, node, camera));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> DetachCameraAsync(
        Scene scene,
        Guid nodeId,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        return this.ExecuteNodeSyncAsync(
            scene,
            node: null,
            nodeId,
            SceneOperationKinds.EditPerspectiveCamera,
            nameof(CameraComponent),
            LiveSyncDiagnosticCodes.CameraRejected,
            LiveSyncDiagnosticCodes.CameraFailed,
            cancellationToken,
            world => world.DetachCamera(nodeId));
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> UpdateMaterialSlotAsync(
        Scene scene,
        SceneNode node,
        int slotIndex,
        Uri? materialUri,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        var scope = Scope(
            scene,
            node,
            componentType: nameof(GeometryComponent),
            assetVirtualPath: materialUri?.ToString());

        if (TryClassifyReadiness(
                this.engineService,
                SceneOperationKinds.EditMaterialSlot,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        return this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditMaterialSlot,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.MaterialRejected,
            LiveSyncDiagnosticCodes.MaterialFailed,
            cancellationToken,
            world => world.SetMaterialOverride(
                node.Id,
                slotIndex,
                MaterialOverridePathMapper.ToEnginePath(materialUri)));
    }

    /// <inheritdoc/>
    public async Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(environment);

        var scope = Scope(scene);
        if (TryClassifyReadiness(
                this.engineService,
                SceneOperationKinds.EditEnvironment,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return new EnvironmentSyncResult(readinessOutcome.Status, new Dictionary<string, SyncStatus>());
        }

        var sunOutcome = await this.SyncSunBindingAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var perField = new Dictionary<string, SyncStatus>
        {
            [nameof(SceneEnvironmentData.AtmosphereEnabled)] = environmentOutcome.Status,
            [nameof(SceneEnvironmentData.SunNodeId)] = sunOutcome.Status,
            [nameof(SceneEnvironmentData.ExposureMode)] = environmentOutcome.Status,
            [nameof(SceneEnvironmentData.ManualExposureEv)] = environmentOutcome.Status,
            [nameof(SceneEnvironmentData.ExposureCompensation)] = environmentOutcome.Status,
            [nameof(SceneEnvironmentData.ToneMapping)] = environmentOutcome.Status,
            [nameof(SceneEnvironmentData.BackgroundColor)] = SyncStatus.Unsupported,
            [nameof(SceneEnvironmentData.SkyAtmosphere)] = environmentOutcome.Status,
        };

        return new EnvironmentSyncResult(Worst(perField.Values), perField);
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
        return this.UpdateNodeTransformAsync(node.Scene, node);
    }

    /// <inheritdoc/>
    public Task<SyncOutcome> UpdatePropertiesAsync(
        Scene scene,
        SceneNode node,
        IReadOnlyList<EnginePropertyValueEntry> entries,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(entries);
        var scope = Scope(scene, node, componentType: nameof(TransformComponent));
        if (entries.Count == 0)
        {
            return Task.FromResult(Accepted(SceneOperationKinds.EditTransform, scope));
        }

        if (TryGetReadyWorld(
                this.engineService,
                SceneOperationKinds.EditTransform,
                scope,
                cancellationToken,
                out var world,
                out var readinessOutcome))
        {
            this.logger.LogInformation(
                "SetProperties skipped. SceneId={SceneId} NodeId={NodeId} Status={Status} Code={Code} EntryCount={EntryCount}",
                scene.Id,
                node.Id,
                readinessOutcome.Status,
                readinessOutcome.Code,
                entries.Count);
            return Task.FromResult(readinessOutcome);
        }

        var wire = new Oxygen.Interop.World.PropertyValueEntry[entries.Count];
        for (var i = 0; i < entries.Count; i++)
        {
            wire[i] = new Oxygen.Interop.World.PropertyValueEntry
            {
                ComponentId = (ushort)entries[i].Component,
                FieldId = entries[i].FieldId,
                Value = entries[i].Value,
            };
        }

        try
        {
            world!.SetProperties(node.Id, wire);
            this.logger.LogDebug(
                "SetProperties enqueued. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}",
                scene.Id,
                node.Id,
                entries.Count);
            return Task.FromResult(Accepted(SceneOperationKinds.EditTransform, scope));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Task.FromResult(Cancelled(SceneOperationKinds.EditTransform, scope));
        }
        catch (ArgumentException ex)
        {
            this.logger.LogWarning(
                ex,
                "SetProperties rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}",
                scene.Id,
                node.Id,
                entries.Count);
            return Task.FromResult(Rejected(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformRejected,
                ex.Message,
                ex));
        }
        catch (InvalidOperationException ex)
        {
            this.logger.LogWarning(
                ex,
                "SetProperties rejected. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}",
                scene.Id,
                node.Id,
                entries.Count);
            return Task.FromResult(Rejected(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformRejected,
                ex.Message,
                ex));
        }
        catch (Exception ex)
        {
            this.logger.LogError(
                ex,
                "SetProperties failed. SceneId={SceneId} NodeId={NodeId} EntryCount={EntryCount}",
                scene.Id,
                node.Id,
                entries.Count);
            return Task.FromResult(Failed(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformFailed,
                ex.Message,
                ex));
        }
    }

    /// <inheritdoc/>
    public Task AttachGeometryAsync(SceneNode node, GeometryComponent geometry)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(geometry);

        return this.ExecuteNodeSyncAsync(
            node.Scene,
            node,
            node.Id,
            SceneOperationKinds.EditGeometry,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.GeometryRejected,
            LiveSyncDiagnosticCodes.GeometryFailed,
            CancellationToken.None,
            world => this.ApplyGeometry(world, node, geometry));
    }

    private Oxygen.Interop.World.OxygenWorld? TryGetWorld()
    {
        if (this.engineService.State != EngineServiceState.Running)
        {
            return null;
        }

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
        // Legacy callers do not provide a scene, so retain log-only behavior.
        var world = this.TryGetWorld();
        if (world is null)
        {
            this.LogCannotDetachGeometry(nodeId);
            return Task.CompletedTask;
        }

        try
        {
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

        return this.ExecuteNodeSyncAsync(
            node.Scene,
            node,
            node.Id,
            SceneOperationKinds.EditDirectionalLight,
            light.GetType().Name,
            LiveSyncDiagnosticCodes.LightRejected,
            LiveSyncDiagnosticCodes.LightFailed,
            CancellationToken.None,
            world => this.ApplyLight(world, node, light));
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

        return this.ExecuteNodeSyncAsync(
            node.Scene,
            node,
            node.Id,
            SceneOperationKinds.EditPerspectiveCamera,
            camera.GetType().Name,
            LiveSyncDiagnosticCodes.CameraRejected,
            LiveSyncDiagnosticCodes.CameraFailed,
            CancellationToken.None,
            world => this.ApplyCamera(world, node, camera));
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
        this.logger.LogWarning("Live material override sync is unsupported for node {NodeId}", nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, OverrideSlot slot)
    {
        this.logger.LogWarning(
            "Live targeted material override sync is unsupported for node {NodeId}, LOD {LodIndex}, submesh {SubmeshIndex}",
            nodeId,
            lodIndex,
            submeshIndex);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveMaterialOverrideAsync(Guid nodeId, Type slotType)
    {
        this.logger.LogWarning("Live material override removal is unsupported for node {NodeId}, slot {SlotType}", nodeId, slotType.Name);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, Type slotType)
    {
        this.logger.LogWarning(
            "Live targeted material override removal is unsupported for node {NodeId}, LOD {LodIndex}, submesh {SubmeshIndex}, slot {SlotType}",
            nodeId,
            lodIndex,
            submeshIndex,
            slotType.Name);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateLodPolicyAsync(Guid nodeId, LevelOfDetailSlot lodSlot)
    {
        this.logger.LogWarning("Live LOD policy sync is unsupported for node {NodeId}", nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateRenderingSettingsAsync(Guid nodeId, RenderingSlot renderingSlot)
    {
        this.logger.LogWarning("Live rendering-settings sync is unsupported for node {NodeId}", nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateLightingSettingsAsync(Guid nodeId, LightingSlot lightingSlot)
    {
        this.logger.LogWarning("Live lighting-settings sync is unsupported for node {NodeId}", nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public bool ShouldIssuePreviewSync(Guid sceneId, Guid nodeId, DateTimeOffset observedAt)
        => this.coalescer.ShouldIssuePreview(new SyncCoalescingKey(sceneId, nodeId), observedAt);

    /// <inheritdoc/>
    public async Task<SyncOutcome?> TryPreviewSyncAsync(
        Guid sceneId,
        Guid nodeId,
        DateTimeOffset observedAt,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sync);

        return this.ShouldIssuePreviewSync(sceneId, nodeId, observedAt)
            ? await sync(cancellationToken).ConfigureAwait(false)
            : null;
    }

    /// <inheritdoc/>
    public bool CompleteTerminalSync(Guid sceneId, Guid nodeId)
        => this.coalescer.CompleteTerminalSync(new SyncCoalescingKey(sceneId, nodeId));

    /// <inheritdoc/>
    public async Task<SyncOutcome> CompleteTerminalSyncAsync(
        Guid sceneId,
        Guid nodeId,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sync);

        _ = this.CompleteTerminalSync(sceneId, nodeId);
        return await sync(cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public void CancelPreviewSync(Guid sceneId, Guid nodeId)
        => this.coalescer.Cancel(new SyncCoalescingKey(sceneId, nodeId));

    /// <inheritdoc/>
    public async Task<SyncOutcome> CancelPreviewSyncAsync(
        Guid sceneId,
        Guid nodeId,
        Func<CancellationToken, Task<SyncOutcome>> revertSync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(revertSync);

        this.CancelPreviewSync(sceneId, nodeId);
        return await revertSync(cancellationToken).ConfigureAwait(false);
    }

    private async Task<SyncOutcome> SyncSunBindingAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken)
    {
        if (environment.SunNodeId is not { } sunNodeId)
        {
            return Accepted(SceneOperationKinds.EditEnvironment, Scope(scene));
        }

        var sunNode = FindNode(scene, sunNodeId);
        if (sunNode is null)
        {
            return Rejected(
                SceneOperationKinds.EditEnvironment,
                Scope(scene, nodeId: sunNodeId, componentType: nameof(DirectionalLightComponent)),
                LiveSyncDiagnosticCodes.EnvironmentRejected,
                $"Environment sun node '{sunNodeId}' does not exist in the scene.");
        }

        if (sunNode.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is null)
        {
            return Rejected(
                SceneOperationKinds.EditEnvironment,
                Scope(scene, sunNode, componentType: nameof(DirectionalLightComponent)),
                LiveSyncDiagnosticCodes.EnvironmentRejected,
                $"Environment sun node '{sunNode.Name}' does not have a directional light component.");
        }

        return await this.AttachLightAsync(scene, sunNode, cancellationToken).ConfigureAwait(false);
    }

    private Task<SyncOutcome> SyncEnvironmentSystemsAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken)
    {
        var scope = Scope(scene);
        if (TryClassifyReadiness(
                this.engineService,
                SceneOperationKinds.EditEnvironment,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        var sky = environment.SkyAtmosphere ?? new SkyAtmosphereEnvironmentData();
        var post = environment.PostProcess ?? new PostProcessEnvironmentData();
        return this.ExecuteSceneSyncAsync(
            scene,
            SceneOperationKinds.EditEnvironment,
            LiveSyncDiagnosticCodes.EnvironmentRejected,
            LiveSyncDiagnosticCodes.EnvironmentFailed,
            cancellationToken,
            world => world.SetEnvironment(
                environment.AtmosphereEnabled,
                sky.SunDiskEnabled,
                sky.PlanetRadiusMeters,
                sky.AtmosphereHeightMeters,
                sky.GroundAlbedoRgb,
                sky.RayleighScaleHeightMeters,
                sky.MieScaleHeightMeters,
                sky.MieAnisotropy,
                sky.SkyLuminanceFactorRgb,
                sky.AerialPerspectiveDistanceScale,
                sky.AerialScatteringStrength,
                sky.AerialPerspectiveStartDepthMeters,
                sky.HeightFogContribution,
                (int)post.ExposureMode,
                post.ExposureEnabled,
                post.ExposureKey,
                post.ManualExposureEv,
                post.ExposureCompensationEv,
                (int)post.ToneMapper,
                (int)post.AutoExposureMeteringMode,
                post.AutoExposureMinEv,
                post.AutoExposureMaxEv,
                post.AutoExposureSpeedUp,
                post.AutoExposureSpeedDown,
                post.AutoExposureLowPercentile,
                post.AutoExposureHighPercentile,
                post.AutoExposureMinLogLuminance,
                post.AutoExposureLogLuminanceRange,
                post.AutoExposureTargetLuminance,
                post.AutoExposureSpotMeterRadius,
                post.BloomIntensity,
                post.BloomThreshold,
                post.Saturation,
                post.Contrast,
                post.VignetteIntensity,
                post.DisplayGamma));
    }

    private static bool TryClassifyUnresolvedImportedGeometry(
        Scene scene,
        SceneNode node,
        GeometryComponent geometry,
        out SyncOutcome outcome)
    {
        if (geometry.Geometry?.Uri is { } uri &&
            string.Equals(AssetUriHelper.GetMountPoint(uri), "Imported", StringComparison.OrdinalIgnoreCase) &&
            geometry.Geometry.Asset is null)
        {
            outcome = Rejected(
                SceneOperationKinds.EditGeometry,
                Scope(
                    scene,
                    node,
                    componentType: nameof(GeometryComponent),
                    componentName: geometry.Name,
                    assetVirtualPath: uri.ToString()),
                LiveSyncDiagnosticCodes.GeometryUnresolvedAtRuntime,
                $"Imported geometry '{uri}' is not resolved in the authoring catalog; live sync was skipped.");
            return true;
        }

        outcome = null!;
        return false;
    }

    private static SceneNode? FindNode(Scene scene, Guid nodeId)
    {
        foreach (var root in scene.RootNodes)
        {
            var found = SceneTraversal.FindNodeById(root, nodeId);
            if (found is not null)
            {
                return found;
            }
        }

        return null;
    }

    private Task<SyncOutcome> ExecuteNodeSyncAsync(
        Scene scene,
        SceneNode node,
        string operationKind,
        string rejectedCode,
        string failedCode,
        CancellationToken cancellationToken,
        Action<Oxygen.Interop.World.OxygenWorld> apply)
        => this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            operationKind,
            node.Components.FirstOrDefault()?.GetType().Name,
            rejectedCode,
            failedCode,
            cancellationToken,
            apply);

    private Task<SyncOutcome> ExecuteNodeSyncAsync(
        Scene scene,
        SceneNode? node,
        Guid nodeId,
        string operationKind,
        string? componentType,
        string rejectedCode,
        string failedCode,
        CancellationToken cancellationToken,
        Action<Oxygen.Interop.World.OxygenWorld> apply)
    {
        var scope = Scope(scene, node, nodeId, componentType);
        if (TryGetReadyWorld(
                this.engineService,
                operationKind,
                scope,
                cancellationToken,
                out var world,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            apply(world!);
            return Task.FromResult(Accepted(operationKind, scope));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Task.FromResult(Cancelled(operationKind, scope));
        }
        catch (NotImplementedException ex)
        {
            return Task.FromResult(Unsupported(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (NotSupportedException ex)
        {
            return Task.FromResult(Unsupported(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (ArgumentException ex)
        {
            return Task.FromResult(Rejected(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (InvalidOperationException ex)
        {
            return Task.FromResult(Rejected(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (Exception ex)
        {
            return Task.FromResult(Failed(operationKind, scope, failedCode, ex.Message, ex));
        }
    }

    private Task<SyncOutcome> ExecuteSceneSyncAsync(
        Scene scene,
        string operationKind,
        string rejectedCode,
        string failedCode,
        CancellationToken cancellationToken,
        Action<Oxygen.Interop.World.OxygenWorld> apply)
    {
        var scope = Scope(scene);
        if (TryGetReadyWorld(
                this.engineService,
                operationKind,
                scope,
                cancellationToken,
                out var world,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            apply(world!);
            return Task.FromResult(Accepted(operationKind, scope));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Task.FromResult(Cancelled(operationKind, scope));
        }
        catch (NotImplementedException ex)
        {
            return Task.FromResult(Unsupported(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (NotSupportedException ex)
        {
            return Task.FromResult(Unsupported(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (ArgumentException ex)
        {
            return Task.FromResult(Rejected(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (InvalidOperationException ex)
        {
            return Task.FromResult(Rejected(operationKind, scope, rejectedCode, ex.Message, ex));
        }
        catch (Exception ex)
        {
            return Task.FromResult(Failed(operationKind, scope, failedCode, ex.Message, ex));
        }
    }

    private static bool TryClassifyReadiness(
        IEngineService engineService,
        string operationKind,
        AffectedScope scope,
        CancellationToken cancellationToken,
        out SyncOutcome outcome)
    {
        var classified = TryGetReadyWorld(
            engineService,
            operationKind,
            scope,
            cancellationToken,
            out _,
            out outcome);
        return classified;
    }

    private static bool TryGetReadyWorld(
        IEngineService engineService,
        string operationKind,
        AffectedScope scope,
        CancellationToken cancellationToken,
        out Oxygen.Interop.World.OxygenWorld? world,
        out SyncOutcome outcome)
    {
        if (cancellationToken.IsCancellationRequested)
        {
            world = null;
            outcome = Cancelled(operationKind, scope);
            return true;
        }

        var state = engineService.State;
        if (state == EngineServiceState.Faulted)
        {
            world = null;
            outcome = new SyncOutcome(
                SyncStatus.SkippedNotRunning,
                operationKind,
                scope,
                LiveSyncDiagnosticCodes.RuntimeFaulted,
                "The runtime engine is faulted; live sync was skipped.");
            return true;
        }

        if (state != EngineServiceState.Running)
        {
            world = null;
            outcome = new SyncOutcome(
                SyncStatus.SkippedNotRunning,
                operationKind,
                scope,
                LiveSyncDiagnosticCodes.NotRunning,
                $"The runtime engine is {state}; live sync was skipped.");
            return true;
        }

        try
        {
            world = engineService.World;
        }
        catch (InvalidOperationException ex)
        {
            world = null;
            outcome = new SyncOutcome(
                SyncStatus.SkippedNotRunning,
                operationKind,
                scope,
                LiveSyncDiagnosticCodes.NotRunning,
                "The runtime world is not available; live sync was skipped.",
                ex);
            return true;
        }

        if (world is null)
        {
            outcome = new SyncOutcome(
                SyncStatus.SkippedNotRunning,
                operationKind,
                scope,
                LiveSyncDiagnosticCodes.NotRunning,
                "The runtime world is not available; live sync was skipped.");
            return true;
        }

        outcome = null!;
        return false;
    }

    private static SyncOutcome Accepted(string operationKind, AffectedScope scope)
        => new(SyncStatus.Accepted, operationKind, scope);

    private static SyncOutcome Unsupported(
        string operationKind,
        AffectedScope scope,
        string code,
        string message,
        Exception? exception = null)
        => new(SyncStatus.Unsupported, operationKind, scope, code, message, exception);

    private static SyncOutcome Rejected(
        string operationKind,
        AffectedScope scope,
        string code,
        string message,
        Exception? exception = null)
        => new(SyncStatus.Rejected, operationKind, scope, code, message, exception);

    private static SyncOutcome Failed(
        string operationKind,
        AffectedScope scope,
        string code,
        string message,
        Exception? exception = null)
        => new(SyncStatus.Failed, operationKind, scope, code, message, exception);

    private static SyncOutcome Cancelled(string operationKind, AffectedScope scope)
        => new(
            SyncStatus.Cancelled,
            operationKind,
            scope,
            LiveSyncDiagnosticCodes.Cancelled,
            "Live sync was cancelled.");

    private static SyncStatus Worst(IEnumerable<SyncStatus> statuses)
    {
        var worst = SyncStatus.Accepted;
        foreach (var status in statuses)
        {
            if (Rank(status) > Rank(worst))
            {
                worst = status;
            }
        }

        return worst;
    }

    private static int Rank(SyncStatus status)
        => status switch
        {
            SyncStatus.Accepted => 0,
            SyncStatus.SkippedNotRunning => 1,
            SyncStatus.Unsupported => 2,
            SyncStatus.Rejected => 3,
            SyncStatus.Cancelled => 4,
            SyncStatus.Failed => 5,
            _ => 0,
        };

    private static AffectedScope Scope(
        Scene scene,
        SceneNode? node = null,
        Guid? nodeId = null,
        string? componentType = null,
        string? componentName = null,
        string? assetVirtualPath = null)
        => new()
        {
            SceneId = scene.Id,
            SceneName = scene.Name,
            NodeId = node?.Id ?? nodeId,
            NodeName = node?.Name,
            ComponentType = componentType,
            ComponentName = componentName,
            AssetVirtualPath = assetVirtualPath,
        };

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
            ApplyMaterialOverrides(world, node, geometry);
        }
        else
        {
            world.DetachGeometry(node.Id);
        }
    }

    private static void ApplyMaterialOverrides(Oxygen.Interop.World.OxygenWorld world, SceneNode node, GeometryComponent geometry)
    {
        var slots = geometry.OverrideSlots.OfType<MaterialsSlot>().ToList();
        for (var index = 0; index < slots.Count; index++)
        {
            var materialUri = slots[index].Material.Uri;
            world.SetMaterialOverride(node.Id, index, MaterialOverridePathMapper.ToEnginePath(materialUri));
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
