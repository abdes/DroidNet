// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
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
public sealed partial class SceneEngineSync : ISceneEngineSync, IDisposable
{
    private static readonly TimeSpan NodeCreationTimeout = TimeSpan.FromSeconds(10);

    private readonly IEngineService engineService;
    private readonly ILogger<SceneEngineSync> logger;
    private readonly LiveSyncCoalescer coalescer = new();
    private readonly PendingPropertySyncQueue pendingPropertySyncs = new();
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
    public void Dispose()
        => this.sceneSyncGate.Dispose();

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
            return await this.BuildSceneInEngineAsync(scene, world, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
            world => ApplyTransform(world, node),
            cancellationToken);
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
            world => ApplyGeometry(world, node, geometry),
            cancellationToken);
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
            world => world.DetachGeometry(nodeId),
            cancellationToken);
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
            world => ApplyLight(world, node, light),
            cancellationToken);
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
            world => world.DetachLight(nodeId),
            cancellationToken);
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
            world => this.ApplyCamera(world, node, camera),
            cancellationToken);
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
            world => world.DetachCamera(nodeId),
            cancellationToken);
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
            world => world.SetMaterialOverride(
                node.Id,
                slotIndex,
                MaterialOverridePathMapper.ToEnginePath(materialUri)),
            cancellationToken);
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
            return new EnvironmentSyncResult(readinessOutcome.Status, new Dictionary<string, SyncStatus>(StringComparer.Ordinal));
        }

        var sunOutcome = await this.SyncSunBindingAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var perField = new Dictionary<string, SyncStatus>(StringComparer.Ordinal)
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
            await CreateNodeWithCallbackAsync(
                world,
                node,
                parentGuid,
                initializeWorldAsRoot: parentGuid is null,
                CancellationToken.None).ConfigureAwait(false);

            ApplyTransform(world, node);
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
            return Task.FromResult(this.HandlePropertySyncReadiness(scene, node, entries, readinessOutcome));
        }

        return Task.FromResult(this.ApplyPropertySync(scene, node, entries, scope, world!, cancellationToken));
    }

    /// <inheritdoc/>
    public int GetPendingPropertySyncCount(Guid sceneId)
        => this.pendingPropertySyncs.Count(sceneId);

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
            world => ApplyGeometry(world, node, geometry),
            CancellationToken.None);
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
            world => ApplyLight(world, node, light),
            CancellationToken.None);
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToDetachLightComponent(ex, nodeId);
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
            world => this.ApplyCamera(world, node, camera),
            CancellationToken.None);
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToDetachCameraComponent(ex, nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateMaterialOverrideAsync(Guid nodeId, OverrideSlot slot)
    {
        this.LogMaterialOverrideSyncUnsupported(nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, OverrideSlot slot)
    {
        this.LogTargetedMaterialOverrideSyncUnsupported(nodeId, lodIndex, submeshIndex);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveMaterialOverrideAsync(Guid nodeId, Type slotType)
    {
        this.LogMaterialOverrideRemovalUnsupported(nodeId, slotType.Name);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveTargetedMaterialOverrideAsync(Guid nodeId, int lodIndex, int submeshIndex, Type slotType)
    {
        this.LogTargetedMaterialOverrideRemovalUnsupported(nodeId, lodIndex, submeshIndex, slotType.Name);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateLodPolicyAsync(Guid nodeId, LevelOfDetailSlot lodSlot)
    {
        this.LogLodPolicySyncUnsupported(nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateRenderingSettingsAsync(Guid nodeId, RenderingSlot renderingSlot)
    {
        this.LogRenderingSettingsSyncUnsupported(nodeId);
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateLightingSettingsAsync(Guid nodeId, LightingSlot lightingSlot)
    {
        this.LogLightingSettingsSyncUnsupported(nodeId);
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

    private async Task<bool> BuildSceneInEngineAsync(
        Scene scene,
        Oxygen.Interop.World.OxygenWorld world,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        world.DestroyScene();
        cancellationToken.ThrowIfCancellationRequested();

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

        var nodesCreated = await this.CreateAllNodesAsync(scene, world, cancellationToken).ConfigureAwait(false);
        if (!nodesCreated)
        {
            return false;
        }

        cancellationToken.ThrowIfCancellationRequested();
        this.LogApplyingHierarchyAndComponents(scene.RootNodes.Count);
        this.ApplyHierarchyAndComponents(scene, world);
        cancellationToken.ThrowIfCancellationRequested();

        this.LogPropagatingTransforms();
        this.PropagateTransforms(scene, world);
        this.ReplayPendingPropertySyncs(scene, world);
        return true;
    }

    private SyncOutcome HandlePropertySyncReadiness(
        Scene scene,
        SceneNode node,
        IReadOnlyList<EnginePropertyValueEntry> entries,
        SyncOutcome readinessOutcome)
    {
        if (LiveSyncBufferingPolicy.ShouldBufferPropertySync(readinessOutcome))
        {
            var pendingCount = this.pendingPropertySyncs.Enqueue(scene.Id, node.Id, entries);
            var bufferedOutcome = readinessOutcome with
            {
                Message = $"The runtime engine is {this.engineService.State}; live sync was buffered. {pendingCount} pending property edit(s) will replay after scene sync.",
            };
            this.LogSetPropertiesBuffered(scene.Id, node.Id, entries.Count, pendingCount);
            return bufferedOutcome;
        }

        this.LogSetPropertiesSkipped(
            scene.Id,
            node.Id,
            readinessOutcome.Status,
            readinessOutcome.Code ?? LiveSyncDiagnosticCodes.NotRunning,
            entries.Count);
        return readinessOutcome;
    }

    private SyncOutcome ApplyPropertySync(
        Scene scene,
        SceneNode node,
        IReadOnlyList<EnginePropertyValueEntry> entries,
        AffectedScope scope,
        Oxygen.Interop.World.OxygenWorld world,
        CancellationToken cancellationToken)
    {
        try
        {
            world.SetProperties(node.Id, EnginePropertyWire.ToWireEntries(entries));
            this.LogSetPropertiesEnqueued(scene.Id, node.Id, entries.Count);
            return Accepted(SceneOperationKinds.EditTransform, scope);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Cancelled(SceneOperationKinds.EditTransform, scope);
        }
        catch (ArgumentException ex)
        {
            this.LogSetPropertiesRejected(ex, scene.Id, node.Id, entries.Count);
            return Rejected(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformRejected,
                ex.Message,
                ex);
        }
        catch (InvalidOperationException ex)
        {
            this.LogSetPropertiesRejected(ex, scene.Id, node.Id, entries.Count);
            return Rejected(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformRejected,
                ex.Message,
                ex);
        }
        catch (Exception ex) when (ex is System.Runtime.InteropServices.COMException or TimeoutException)
        {
            this.LogSetPropertiesFailed(ex, scene.Id, node.Id, entries.Count);
            return Failed(
                SceneOperationKinds.EditTransform,
                scope,
                LiveSyncDiagnosticCodes.TransformFailed,
                ex.Message,
                ex);
        }
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
                post.DisplayGamma),
            cancellationToken);
    }

    private Task<SyncOutcome> ExecuteNodeSyncAsync(
        Scene scene,
        SceneNode node,
        string operationKind,
        string rejectedCode,
        string failedCode,
        Action<Oxygen.Interop.World.OxygenWorld> apply,
        CancellationToken cancellationToken)
        => this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            operationKind,
            node.Components.FirstOrDefault()?.GetType().Name,
            rejectedCode,
            failedCode,
            apply,
            cancellationToken);

    private Task<SyncOutcome> ExecuteNodeSyncAsync(
        Scene scene,
        SceneNode? node,
        Guid nodeId,
        string operationKind,
        string? componentType,
        string rejectedCode,
        string failedCode,
        Action<Oxygen.Interop.World.OxygenWorld> apply,
        CancellationToken cancellationToken)
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            return Task.FromResult(Failed(operationKind, scope, failedCode, ex.Message, ex));
        }
    }

    private Task<SyncOutcome> ExecuteSceneSyncAsync(
        Scene scene,
        string operationKind,
        string rejectedCode,
        string failedCode,
        Action<Oxygen.Interop.World.OxygenWorld> apply,
        CancellationToken cancellationToken)
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            return Task.FromResult(Failed(operationKind, scope, failedCode, ex.Message, ex));
        }
    }

    private void ReplayPendingPropertySyncs(Scene scene, Oxygen.Interop.World.OxygenWorld world)
    {
        var pendingEntries = this.pendingPropertySyncs.Drain(scene.Id);
        if (pendingEntries.Count == 0)
        {
            return;
        }

        var replayed = 0;
        var skipped = 0;
        foreach (var pendingEntry in pendingEntries)
        {
            if (!SceneContainsNode(scene, pendingEntry.NodeId))
            {
                skipped++;
                this.LogBufferedSetPropertiesReplaySkipped(scene.Id, pendingEntry.NodeId, pendingEntry.Entries.Count);
                continue;
            }

            try
            {
                world.SetProperties(pendingEntry.NodeId, EnginePropertyWire.ToWireEntries(pendingEntry.Entries));
                replayed++;
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or System.Runtime.InteropServices.COMException)
            {
                skipped++;
                this.LogBufferedSetPropertiesReplayRejected(ex, scene.Id, pendingEntry.NodeId, pendingEntry.Entries.Count);
            }
        }

        this.LogBufferedSetPropertiesReplayCompleted(scene.Id, pendingEntries.Count, replayed, skipped);

        static bool SceneContainsNode(Scene scene, Guid nodeId)
        {
            foreach (var rootNode in scene.RootNodes)
            {
                if (SceneTraversal.FindNodeById(rootNode, nodeId) is not null)
                {
                    return true;
                }
            }

            return false;
        }
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

    private void ApplyRenderableComponents(Oxygen.Interop.World.OxygenWorld world, SceneNode node)
    {
        var geometryComp = node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geometryComp is not null)
        {
            try
            {
                ApplyGeometry(world, node, geometryComp);
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                this.LogFailedToAttachCameraComponent(ex, node.Id);
            }
        }

        if (node.Components.OfType<LightComponent>().FirstOrDefault() is { } light)
        {
            try
            {
                ApplyLight(world, node, light);
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                this.LogFailedToAttachLightComponent(ex, node.Id);
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
                this.LogUnsupportedCameraComponent(camera.GetType().Name, node.Id);
                break;
        }
    }

    private async Task<bool> CreateAllNodesAsync(Scene scene, Oxygen.Interop.World.OxygenWorld world, CancellationToken cancellationToken)
    {
        var createTasks = new List<Task<bool>>();

        void EnqueueCreateRecursive(SceneNode node)
        {
            var task = CreateNodeWithCallbackAsync(
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
            this.LogEnqueueingRootNodeCreation(root);
            EnqueueCreateRecursive(root);
        }

        var results = await Task.WhenAll(createTasks).ConfigureAwait(false);
        this.LogAllNodeCreationTasksCompleted(createTasks.Count);
        if (results.All(static created => created))
        {
            return true;
        }

        // Some node creation callback timed out or was canceled. Wait for one
        // final SceneMutation command before releasing the sync gate so stale
        // queued node commands cannot leak into the next scene switch.
        _ = await CreateNodeWithCallbackAsync(
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
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                this.LogFailedToReparentNode(ex, node);
            }

            // Apply transform if present
            try
            {
                ApplyTransform(world, node);
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
            this.LogProcessingHierarchyRootNode(root);
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToDumpSceneTransformsForDebug(ex);
        }
    }
}
