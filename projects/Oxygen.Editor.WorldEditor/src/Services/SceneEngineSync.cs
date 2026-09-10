// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Services;

/// <summary>
///     Default implementation of <see cref="ISceneEngineSync"/> that synchronizes scene data
///     with the native rendering engine through the <see cref="IEngineService"/>.
/// </summary>
/// <param name="engineService">The managed runtime boundary.</param>
/// <param name="loggerFactory">The optional diagnostic logger factory.</param>
/// <param name="operationResults">The shared operation-result publisher.</param>
/// <param name="hostingContext">The application's UI dispatcher context.</param>
public sealed partial class SceneEngineSync(
    IEngineService engineService,
    ILoggerFactory? loggerFactory = null,
    IOperationResultPublisher? operationResults = null,
    DroidNet.Hosting.WinUI.HostingContext? hostingContext = null) : ISceneEngineSync, IDisposable
{
    private static readonly TimeSpan NodeCreationTimeout = TimeSpan.FromSeconds(10);

    private readonly System.Runtime.CompilerServices.ConditionalWeakTable<Scene, DocumentLifetime> documentLifetimes = [];
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "The application owns the injected engine service; disposing scene synchronization must not shut down the shared runtime.")]
    private readonly IEngineService engineService = engineService ?? throw new ArgumentNullException(nameof(engineService));
    private readonly ILogger<SceneEngineSync> logger = loggerFactory?.CreateLogger<SceneEngineSync>() ?? NullLoggerFactory.Instance.CreateLogger<SceneEngineSync>();
    private readonly LiveSyncCoalescer coalescer = new();
    private readonly PendingPropertySyncQueue pendingPropertySyncs = new();
    private readonly SemaphoreSlim sceneSyncGate = new(initialCount: 1, maxCount: 1);
    private volatile WorldDispatch? activeWorld;
    private volatile Scene? activeScene;

    /// <inheritdoc/>
    public event EventHandler<PendingPropertySyncCountChangedEventArgs>? PendingPropertySyncCountChanged;

    /// <inheritdoc/>
    public void Dispose()
    {
        this.activeWorld = null;
        this.activeScene = null;
        if (this.observedWorld is { } observedWorld)
        {
            observedWorld.AssetLoadFailed -= this.OnAssetLoadFailed;
            this.observedWorld = null;
        }

        this.sceneSyncGate.Dispose();
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

        return !cancellationToken.IsCancellationRequested && await this.SyncSceneAsync(scene, cancellationToken).ConfigureAwait(false);
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
            _ = this.sceneSyncGate.Release();
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
            _ = this.sceneSyncGate.Release();
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
        return this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditGeometry,
                scope,
                cancellationToken,
                out var readinessOutcome)
            ? Task.FromResult(readinessOutcome)
            : TryClassifyUnresolvedImportedGeometry(scene, node, geometry, out var geometryOutcome)
            ? Task.FromResult(geometryOutcome)
            : this.ExecuteNodeSyncAsync(
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
            world => world.Execute(new RuntimeDetachGeometry(nodeId)),
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
        return light is null
            ? Task.FromResult(
                Rejected(
                    SceneOperationKinds.EditDirectionalLight,
                    Scope(scene, node, componentType: nameof(LightComponent)),
                    LiveSyncDiagnosticCodes.LightRejected,
                    "Node has no light component to attach."))
            : this.ExecuteNodeSyncAsync(
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
            world => world.Execute(new RuntimeDetachLight(nodeId)),
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
        return this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditPerspectiveCamera,
                scope,
                cancellationToken,
                out var readinessOutcome)
            ? Task.FromResult(readinessOutcome)
            : camera is not PerspectiveCamera
            ? Task.FromResult(
                Unsupported(
                    SceneOperationKinds.EditPerspectiveCamera,
                    scope,
                    LiveSyncDiagnosticCodes.CameraUnsupported,
                    $"Camera component '{camera.GetType().Name}' has no live sync adapter in ED-M04."))
            : this.ExecuteNodeSyncAsync(
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
            world => world.Execute(new RuntimeDetachCamera(nodeId)),
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

        return this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditMaterialSlot,
                scope,
                cancellationToken,
                out var readinessOutcome)
            ? Task.FromResult(readinessOutcome)
            : this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditMaterialSlot,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.MaterialRejected,
            LiveSyncDiagnosticCodes.MaterialFailed,
            world => world.Execute(new RuntimeSetMaterialOverride(
                node.Id,
                slotIndex,
                MaterialOverridePathMapper.ToEnginePath(materialUri))),
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
        if (this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditEnvironment,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return new EnvironmentSyncResult(readinessOutcome.Status, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal));
        }

        var sunOutcome = await this.SyncSunBindingAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        var backgroundOutcome = await this.SyncBackgroundAsync(scene, environment, cancellationToken).ConfigureAwait(false);
        return EnvironmentResult(sunOutcome, environmentOutcome, backgroundOutcome);
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
            var created = await this.CreateNodeWithCallbackAsync(
                world,
                node,
                parentGuid,
                initializeWorldAsRoot: parentGuid is null,
                CancellationToken.None).ConfigureAwait(false);
            if (!created)
            {
                throw new InvalidOperationException("The runtime did not acknowledge node creation for the current scene activation.");
            }

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
            world.Execute(new RuntimeRemoveSceneNode(nodeId));
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
        world.Execute(new RuntimeRemoveSceneNode(rootNodeId));
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

        world.Execute(new RuntimeRemoveSceneNodes(rootNodeIds.ToImmutableArray()));
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
            world.Execute(new RuntimeReparentSceneNode(nodeId, newParentGuid, preserveWorldTransform));
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

        world.Execute(new RuntimeReparentSceneNodes(nodeIds.ToImmutableArray(), newParentGuid, preserveWorldTransform));
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task UpdateNodeTransformAsync(SceneNode node)
    {
        ArgumentNullException.ThrowIfNull(node);
        return this.UpdateNodeTransformAsync(node.Scene, node, CancellationToken.None);
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
        var operationKind = GetPropertyOperationKind(entries);
        var scope = Scope(scene, node, componentType: GetPropertyComponentType(entries));
        return entries.Count == 0
            ? Task.FromResult(Accepted(operationKind, scope))
            : this.TryGetReadyWorld(
                this.engineService,
                scene,
                operationKind,
                scope,
                cancellationToken,
                out var world,
                out var readinessOutcome)
            ? Task.FromResult(this.HandlePropertySyncReadiness(scene, node, entries, readinessOutcome))
            : Task.FromResult(this.ApplyPropertySync(scene, node, entries, scope, world!, operationKind, cancellationToken));
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
            world.Execute(new RuntimeDetachGeometry(nodeId));
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
            world.Execute(new RuntimeDetachLight(nodeId));
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
            world.Execute(new RuntimeDetachCamera(nodeId));
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
        WorldDispatch world,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var target = new RuntimeSceneTarget(world.Commands.RunId, scene.Id, this.documentLifetimes.GetValue(scene, _ => new()).Id, Guid.NewGuid());
        world = new WorldDispatch(world.Commands, target, cancellationToken);
        this.activeWorld = world;
        this.activeScene = scene;
        var activation = await world.Commands.ActivateSceneAsync(Guid.NewGuid(), target, scene.Name, cancellationToken).ConfigureAwait(false);
        var created = activation.Succeeded;
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
        var sunOutcome = await this.SyncSunBindingAsync(scene, scene.Environment, cancellationToken).ConfigureAwait(false);
        if (sunOutcome.Status != SyncStatus.Accepted)
        {
            this.LogFailedToSyncSceneWithEngine(
                new InvalidOperationException($"Initial environment sun sync returned {sunOutcome.Status}: {sunOutcome.Message}"),
                scene);
            return false;
        }

        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, scene.Environment, cancellationToken).ConfigureAwait(false);
        if (environmentOutcome.Status != SyncStatus.Accepted)
        {
            this.LogFailedToSyncSceneWithEngine(
                new InvalidOperationException($"Initial environment sync returned {environmentOutcome.Status}: {environmentOutcome.Message}"),
                scene);
            return false;
        }

        var backgroundOutcome = await this.SyncBackgroundAsync(scene, scene.Environment, cancellationToken).ConfigureAwait(false);
        if (backgroundOutcome.Status != SyncStatus.Accepted)
        {
            this.LogFailedToSyncSceneWithEngine(new InvalidOperationException(backgroundOutcome.Message), scene);
            return false;
        }

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
                Message = string.Create(CultureInfo.InvariantCulture, $"The runtime engine is {this.engineService.State}; live sync was buffered. {pendingCount} pending property edit(s) will replay after scene sync."),
            };
            this.LogSetPropertiesBuffered(scene.Id, node.Id, entries.Count, pendingCount);
            this.OnPendingPropertySyncCountChanged(scene.Id, pendingCount);
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
        WorldDispatch world,
        string operationKind,
        CancellationToken cancellationToken)
    {
        try
        {
            world.Execute(new RuntimeSetProperties(node.Id, EnginePropertyWire.ToWireEntries(entries)));
            this.LogSetPropertiesEnqueued(scene.Id, node.Id, entries.Count);
            return Accepted(operationKind, scope);
        }
        catch (RuntimeDispatchException ex)
        {
            return FromRuntimeOutcome(ex.Result, operationKind, scope, GetPropertyRejectedCode(operationKind), GetPropertyFailedCode(operationKind));
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return Cancelled(operationKind, scope);
        }
        catch (ArgumentException ex)
        {
            this.LogSetPropertiesRejected(ex, scene.Id, node.Id, entries.Count);
            return Rejected(
                operationKind,
                scope,
                GetPropertyRejectedCode(operationKind),
                ex.Message,
                ex);
        }
        catch (InvalidOperationException ex)
        {
            this.LogSetPropertiesRejected(ex, scene.Id, node.Id, entries.Count);
            return Rejected(
                operationKind,
                scope,
                GetPropertyRejectedCode(operationKind),
                ex.Message,
                ex);
        }
        catch (Exception ex) when (ex is System.Runtime.InteropServices.COMException or TimeoutException)
        {
            this.LogSetPropertiesFailed(ex, scene.Id, node.Id, entries.Count);
            return Failed(
                operationKind,
                scope,
                GetPropertyFailedCode(operationKind),
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
        return sunNode is null
            ? Rejected(
                SceneOperationKinds.EditEnvironment,
                Scope(scene, nodeId: sunNodeId, componentType: nameof(DirectionalLightComponent)),
                LiveSyncDiagnosticCodes.EnvironmentRejected,
                $"Environment sun node '{sunNodeId}' does not exist in the scene.")
            : sunNode.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is null
            ? Rejected(
                SceneOperationKinds.EditEnvironment,
                Scope(scene, sunNode, componentType: nameof(DirectionalLightComponent)),
                LiveSyncDiagnosticCodes.EnvironmentRejected,
                $"Environment sun node '{sunNode.Name}' does not have a directional light component.")
            : await this.AttachLightAsync(scene, sunNode, cancellationToken).ConfigureAwait(false);
    }

    private Task<SyncOutcome> SyncEnvironmentSystemsAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken)
    {
        var scope = Scope(scene);
        if (this.TryClassifyReadiness(
                this.engineService,
                scene,
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
            world => world.Execute(new RuntimeSetEnvironment(
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
                post.DisplayGamma)),
            cancellationToken);
    }

    private Task<SyncOutcome> ExecuteNodeSyncAsync(
        Scene scene,
        SceneNode node,
        string operationKind,
        string rejectedCode,
        string failedCode,
        Action<WorldDispatch> apply,
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
        Action<WorldDispatch> apply,
        CancellationToken cancellationToken)
    {
        var scope = Scope(scene, node, nodeId, componentType);
        if (this.TryGetReadyWorld(
                this.engineService,
                scene,
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
        catch (RuntimeDispatchException ex)
        {
            return Task.FromResult(FromRuntimeOutcome(ex.Result, operationKind, scope, rejectedCode, failedCode));
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
        Action<WorldDispatch> apply,
        CancellationToken cancellationToken)
    {
        var scope = Scope(scene);
        if (this.TryGetReadyWorld(
                this.engineService,
                scene,
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
        catch (RuntimeDispatchException ex)
        {
            return Task.FromResult(FromRuntimeOutcome(ex.Result, operationKind, scope, rejectedCode, failedCode));
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

    private void ReplayPendingPropertySyncs(Scene scene, WorldDispatch world)
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
                world.Execute(new RuntimeSetProperties(pendingEntry.NodeId, EnginePropertyWire.ToWireEntries(pendingEntry.Entries)));
                replayed++;
            }
            catch (Exception ex) when (ex is ArgumentException or InvalidOperationException or System.Runtime.InteropServices.COMException)
            {
                skipped++;
                this.LogBufferedSetPropertiesReplayRejected(ex, scene.Id, pendingEntry.NodeId, pendingEntry.Entries.Count);
            }
        }

        this.LogBufferedSetPropertiesReplayCompleted(scene.Id, pendingEntries.Count, replayed, skipped);
        this.OnPendingPropertySyncCountChanged(scene.Id, pendingCount: 0);

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

    private void OnPendingPropertySyncCountChanged(Guid sceneId, int pendingCount)
        => this.PendingPropertySyncCountChanged?.Invoke(
            this,
            new PendingPropertySyncCountChangedEventArgs(sceneId, pendingCount));

    private WorldDispatch? TryGetWorld()
    {
        if (this.engineService.State != EngineServiceState.Running)
        {
            return null;
        }

        try
        {
            var commands = this.engineService.WorldCommands;
            if (commands is not null)
            {
                this.ObserveWorld(commands);
            }

            return commands is null ? null : this.activeWorld is { } active && ReferenceEquals(active.Commands, commands)
                ? active with { CancellationToken = CancellationToken.None } : new WorldDispatch(commands, default, CancellationToken.None);
        }
        catch (InvalidOperationException)
        {
            return null;
        }
    }

    private void ApplyRenderableComponents(WorldDispatch world, SceneNode node)
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

    private void ApplyCamera(WorldDispatch world, SceneNode node, CameraComponent camera)
    {
        switch (camera)
        {
            case PerspectiveCamera perspective:
                world.Execute(new RuntimeAttachPerspectiveCamera(
                    node.Id,
                    ToEngineFieldOfViewRadians(perspective.FieldOfView),
                    perspective.AspectRatio,
                    perspective.NearPlane,
                    perspective.FarPlane));
                break;

            default:
                this.LogUnsupportedCameraComponent(camera.GetType().Name, node.Id);
                break;
        }
    }

    private async Task<bool> CreateAllNodesAsync(Scene scene, WorldDispatch world, CancellationToken cancellationToken)
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
        _ = await this.CreateNodeWithCallbackAsync(
            world,
            new SceneNode(scene) { Name = "__sync_barrier__" },
            parentGuid: null,
            initializeWorldAsRoot: false,
            CancellationToken.None).ConfigureAwait(false);

        return false;
    }

    private void ApplyHierarchyAndComponents(Scene scene, WorldDispatch world)
    {
        void ResolveAndApply(SceneNode node, Guid? parentGuid)
        {
            if (!node.IsActive)
            {
                this.LogNodeCreationNotSuccessful(node);
                _ = scene.RootNodes.Remove(node);
                return;
            }

            // Reparent to the desired parent
            try
            {
                world.Execute(new RuntimeReparentSceneNode(node.Id, parentGuid, PreserveWorldTransform: false));
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

    private void PropagateTransforms(Scene scene, WorldDispatch world)
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
                world.Execute(new RuntimeUpdateTransformsForNodes([.. handles]));
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
