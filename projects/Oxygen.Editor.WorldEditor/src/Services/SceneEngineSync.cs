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
    public async Task<bool> SyncSceneWhenReadyAsync(Scene scene, CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            if (this.disposed || !this.TryGetDocument(scene, out _))
            {
                return false;
            }

            this.SelectRequestedScene(scene);
        }

        if (this.engineService.State != EngineServiceState.Running)
        {
            this.LogEngineNotRunningDeferringSceneSync(scene);
        }

        while (!this.disposed && !cancellationToken.IsCancellationRequested)
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

        return !this.disposed && !cancellationToken.IsCancellationRequested
            && await this.SyncSceneCoreAsync(scene, skipIfCurrent: true, cancellationToken).ConfigureAwait(false);
    }

    /// <inheritdoc/>
    public Task<bool> SyncSceneAsync(Scene scene, CancellationToken cancellationToken = default)
        => this.SyncSceneCoreAsync(scene, skipIfCurrent: false, cancellationToken);

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
    public Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken = default)
        => this.UpdateEnvironmentAsync(scene, environment, this.CaptureRevision(scene), cancellationToken);

    /// <inheritdoc/>
    public async Task CreateNodeAsync(SceneNode node, Guid? parentGuid = null)
    {
        ArgumentNullException.ThrowIfNull(node);

        if (this.TryDeferNodeCreation(node, parentGuid, out var pending))
        {
            await pending.ConfigureAwait(false);
            return;
        }

        var world = this.TryGetWorld(node.Scene);
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
            _ = this.ApplyRenderableComponents(world, node);

            this.LogCreatedAndInitializedNode(node);
        }
        catch (Exception ex)
        {
            this.LogFailedToCreateNode(ex, node);
            throw;
        }
    }

    /// <inheritdoc/>
    public Task RemoveNodeAsync(Scene scene, Guid nodeId)
    {
        var world = this.TryGetWorld(scene);
        if (world is null)
        {
            this.LogCannotRemoveNode(nodeId);
            return Task.CompletedTask;
        }

        try
        {
            this.ExecuteOrDefer(scene, world, new RuntimeRemoveSceneNode(nodeId));
            this.LogRemovedNode(nodeId);
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToRemoveNode(ex, nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveNodeHierarchyAsync(Scene scene, Guid rootNodeId)
    {
        var world = this.TryGetWorld(scene);
        if (world is null)
        {
            this.LogCannotRemoveNodeHierarchy(rootNodeId);
            return Task.CompletedTask;
        }

        // OxygenWorld's RemoveSceneNode handles hierarchy destruction if children exist.
        this.ExecuteOrDefer(scene, world, new RuntimeRemoveSceneNode(rootNodeId));
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task RemoveNodeHierarchiesAsync(Scene scene, IReadOnlyList<Guid> rootNodeIds)
    {
        if (rootNodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        var world = this.TryGetWorld(scene);
        if (world is null)
        {
            this.LogCannotRemoveNodeHierarchies();
            return Task.CompletedTask;
        }

        this.ExecuteOrDefer(scene, world, new RuntimeRemoveSceneNodes(rootNodeIds.ToImmutableArray()));
        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ReparentNodeAsync(Scene scene, Guid nodeId, Guid? newParentGuid, bool preserveWorldTransform = false)
    {
        var world = this.TryGetWorld(scene);
        if (world is null)
        {
            this.LogCannotReparentNode(nodeId);
            return Task.CompletedTask;
        }

        try
        {
            this.ExecuteOrDefer(scene, world, new RuntimeReparentSceneNode(nodeId, newParentGuid, preserveWorldTransform));
            this.LogReparentedNode(nodeId, newParentGuid);
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToReparentNode(ex, nodeId);
        }

        return Task.CompletedTask;
    }

    /// <inheritdoc/>
    public Task ReparentHierarchiesAsync(Scene scene, IReadOnlyList<Guid> nodeIds, Guid? newParentGuid, bool preserveWorldTransform = false)
    {
        if (nodeIds.Count == 0)
        {
            return Task.CompletedTask;
        }

        var world = this.TryGetWorld(scene);
        if (world is null)
        {
            this.LogCannotReparentHierarchies();
            return Task.CompletedTask;
        }

        this.ExecuteOrDefer(scene, world, new RuntimeReparentSceneNodes(nodeIds.ToImmutableArray(), newParentGuid, preserveWorldTransform));
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
        => this.UpdatePropertiesAsync(scene, node, entries, this.CaptureRevision(scene), cancellationToken);

    /// <inheritdoc/>
    public int GetPendingPropertySyncCount(Guid sceneId)
    {
        lock (this.documentGate)
        {
            return this.pendingPropertySyncs.Count(sceneId)
                + (this.openScenes.TryGetValue(sceneId, out var lifetime) ? lifetime.PendingMutations.Count + (lifetime.PendingEnvironment is null ? 0 : 1) : 0);
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

    private static RuntimeSetEnvironment BuildEnvironmentCommand(Scene scene, SceneEnvironmentData environment)
    {
        var sky = environment.SkyAtmosphere ?? new SkyAtmosphereEnvironmentData();
        var post = environment.PostProcess ?? new PostProcessEnvironmentData();
        return new RuntimeSetEnvironment(
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
                post.AutoExposureBlackInfluence,
                post.AutoExposureTransitionDistanceEv,
                post.AutoExposureCompensationCurve.Select(static key => new RuntimeExposureCompensationKey(key.MeteredEv, key.CompensationEv)).ToImmutableArray(),
                CreateExposureMaskReference(scene, post.AutoExposureMeteringMask),
                post.BloomIntensity,
                post.BloomThreshold,
                post.Saturation,
                post.Contrast,
                post.VignetteIntensity,
                post.DisplayGamma);
    }

    private static RuntimeTextureReference? CreateExposureMaskReference(Scene scene, Uri? mask)
    {
        if (mask is null)
        {
            return null;
        }

        var path = ContentPipeline.ContentPipelinePaths.ToNativeDescriptorPath(mask, ".otex");
        var separator = path.IndexOf('/', 1);
        if (separator <= 1 || scene.Project.ProjectInfo.Location is not { } projectRoot
            || !Path.IsPathFullyQualified(projectRoot))
        {
            throw new InvalidOperationException("An exposure mask requires a saved project and a source mount.");
        }

        return new(mask,
            ContentPipeline.ContentPipelinePaths.GetCookedMountRoot(projectRoot, path[1..separator]),
            path[(separator + 1)..]);
    }

    private async Task<bool> SyncSceneCoreAsync(Scene scene, bool skipIfCurrent, CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(scene);
        lock (this.documentGate)
        {
            if (this.disposed || !this.TryGetDocument(scene, out _))
            {
                return false;
            }

            this.SelectRequestedScene(scene);
            _ = Interlocked.Increment(ref this.syncOperations);
        }

        var entered = false;
        try
        {
            await this.sceneSyncGate.WaitAsync(cancellationToken).ConfigureAwait(false);
            entered = true;
            if (this.disposed || !ReferenceEquals(this.requestedScene, scene))
            {
                return false;
            }

            if (skipIfCurrent && this.IsSceneProjectionCurrent(scene))
            {
                return true;
            }

            var world = this.TryGetWorld();
            if (world is null)
            {
                this.LogOxygenWorldNotAvailableSkippingSceneSync(scene);
                return false;
            }

            var projection = await this.CaptureSceneProjectionAsync(scene).ConfigureAwait(false);
            return projection is not null && await this.BuildSceneInEngineAsync(projection, world, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            return false;
        }
        catch (Exception exception) when (EngineInteropExceptionPolicy.IsRecoverable(exception))
        {
            this.LogFailedToSyncSceneWithEngine(exception, scene);
            return false;
        }
        finally
        {
            if (entered)
            {
                _ = this.sceneSyncGate.Release();
            }

            _ = Interlocked.Decrement(ref this.syncOperations);
            this.TryDisposeSyncGate();
        }
    }

    private async Task<EnvironmentSyncResult> ApplyEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken,
        WorldDispatch? dispatchOverride = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(environment);

        var scope = Scope(scene);
        if (dispatchOverride is null && this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditEnvironment,
                scope,
                cancellationToken,
                out var readinessOutcome))
        {
            return EnvironmentResult(readinessOutcome, readinessOutcome);
        }

        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, environment, cancellationToken, dispatchOverride).ConfigureAwait(false);
        var backgroundOutcome = await this.SyncBackgroundAsync(scene, environment, cancellationToken, dispatchOverride).ConfigureAwait(false);
        return EnvironmentResult(environmentOutcome, backgroundOutcome);
    }

    private async Task<bool> BuildSceneInEngineAsync(
        SceneProjection projection,
        WorldDispatch world,
        CancellationToken cancellationToken)
    {
        var scene = projection.Snapshot;
        cancellationToken.ThrowIfCancellationRequested();
        var target = new RuntimeSceneTarget(world.Commands.RunId, scene.Id, projection.Lifetime.Id, Guid.NewGuid()) { ProjectId = scene.Project.ProjectInfo?.Id ?? Guid.Empty };
        world = new WorldDispatch(world.Commands, target, cancellationToken);
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(projection.Lifetime))
            {
                return false;
            }

            this.activeWorld = world;
            this.activeScene = projection.Lifetime.Scene;
            this.projecting = projection;
        }

        try
        {
            var activation = await world.Commands.ActivateSceneAsync(Guid.NewGuid(), target, scene.Name, cancellationToken).ConfigureAwait(false);
            cancellationToken.ThrowIfCancellationRequested();
            return activation.Succeeded
                && await this.CreateAllNodesAsync(scene, world, cancellationToken).ConfigureAwait(false)
                && await this.CompleteSceneProjectionAsync(projection, world, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            if (ReferenceEquals(this.projecting, projection))
            {
                this.projecting = null;
                this.FinishDeferredWaiters(projection.Lifetime);
            }
        }
    }

    private async Task<bool> CompleteSceneProjectionAsync(SceneProjection projection, WorldDispatch world, CancellationToken cancellationToken)
    {
        var scene = projection.Snapshot;
        var target = world.Target;
        cancellationToken.ThrowIfCancellationRequested();
        if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != target)
        {
            return false;
        }

        if (!this.ApplyHierarchyAndComponents(scene, world) || !this.PropagateTransforms(scene, world))
        {
            return false;
        }

        world.Execute(BuildEnvironmentCommand(scene, scene.Environment));
        world.Execute(new RuntimeSetBackgroundColor(scene.Environment.BackgroundColor));
        lock (this.documentGate)
        {
            if (!this.IsDocumentCurrent(projection.Lifetime) || this.activeWorld?.Target != target)
            {
                return false;
            }

            this.SupersedePendingSyncs(projection);
        }

        while (this.IsDocumentCurrent(projection.Lifetime) && this.activeWorld?.Target == target)
        {
            var current = await this.ReplayPendingSceneSyncsAsync(projection, world).ConfigureAwait(false);
            var published = await this.PublishProjectedNodesAsync(projection, target, current).ConfigureAwait(false);
            if (!current || published)
            {
                return current;
            }
        }

        return false;
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

    private Task<SyncOutcome> SyncEnvironmentSystemsAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken,
        WorldDispatch? dispatchOverride = null)
    {
        var scope = Scope(scene);
        return dispatchOverride is null && this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditEnvironment,
                scope,
                cancellationToken,
                out var readinessOutcome)
            ? Task.FromResult(readinessOutcome)
            : this.ExecuteSceneSyncAsync(
            scene,
            SceneOperationKinds.EditEnvironment,
            LiveSyncDiagnosticCodes.EnvironmentRejected,
            LiveSyncDiagnosticCodes.EnvironmentFailed,
            world => world.Execute(BuildEnvironmentCommand(scene, environment)),
            cancellationToken,
            dispatchOverride);
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
        CancellationToken cancellationToken,
        WorldDispatch? dispatchOverride = null)
    {
        var scope = Scope(scene, node, nodeId, componentType);
        var world = dispatchOverride;
        if (world is null && this.TryGetReadyWorld(
                this.engineService,
                scene,
                operationKind,
                scope,
                cancellationToken,
                out world,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (dispatchOverride is null && this.TryDeferMutation(scene, world!, apply, operationKind, scope, out var deferred))
            {
                return Task.FromResult(deferred);
            }

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
        CancellationToken cancellationToken,
        WorldDispatch? dispatchOverride = null)
    {
        var scope = Scope(scene);
        var world = dispatchOverride;
        if (world is null && this.TryGetReadyWorld(
                this.engineService,
                scene,
                operationKind,
                scope,
                cancellationToken,
                out world,
                out var readinessOutcome))
        {
            return Task.FromResult(readinessOutcome);
        }

        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (dispatchOverride is null && this.TryDeferMutation(scene, world!, apply, operationKind, scope, out var deferred))
            {
                return Task.FromResult(deferred);
            }

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

    private void OnPendingPropertySyncCountChanged(Guid sceneId, int pendingCount)
        => this.PendingPropertySyncCountChanged?.Invoke(
            this,
            new PendingPropertySyncCountChangedEventArgs(sceneId, pendingCount));

    private WorldDispatch? TryGetWorld(Scene scene)
    {
        var world = this.TryGetWorld();
        return world is not null && ReferenceEquals(this.activeScene, scene) && world.Target.SceneId == scene.Id
            ? world
            : null;
    }

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

    private bool ApplyRenderableComponents(WorldDispatch world, SceneNode node)
    {
        var succeeded = true;
        var geometryComp = node.Components.OfType<GeometryComponent>().FirstOrDefault();
        if (geometryComp is not null)
        {
            try
            {
                ApplyGeometry(world, node, geometryComp);
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                succeeded = false;
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
                succeeded = false;
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
                succeeded = false;
                this.LogFailedToAttachLightComponent(ex, node.Id);
            }
        }

        return succeeded;
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
                world.Execute(new RuntimeSetProperties(node.Id,
                [
                    new((ushort)EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.ApertureF, perspective.ApertureF),
                    new((ushort)EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.ShutterRate, perspective.ShutterRate),
                    new((ushort)EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.Iso, perspective.Iso),
                ]));
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

    private bool ApplyHierarchyAndComponents(Scene scene, WorldDispatch world)
    {
        var succeeded = true;

        void ResolveAndApply(SceneNode node, Guid? parentGuid)
        {
            if (!node.IsActive)
            {
                this.LogNodeCreationNotSuccessful(node);
                succeeded = false;
                return;
            }

            // Reparent to the desired parent
            try
            {
                world.Execute(new RuntimeReparentSceneNode(node.Id, parentGuid, PreserveWorldTransform: false));
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                succeeded = false;
                this.LogFailedToReparentNode(ex, node);
            }

            // Apply transform if present
            try
            {
                ApplyTransform(world, node);
            }
            catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
            {
                succeeded = false;
                this.LogFailedToSetTransform(ex, node);
            }

            succeeded &= this.ApplyRenderableComponents(world, node);

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

        return succeeded;
    }

    private bool PropagateTransforms(Scene scene, WorldDispatch world)
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

            return true;
        }
        catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
        {
            this.LogFailedToRequestTransformPropagation(ex);
            return false;
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
