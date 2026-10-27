// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Projects the open scene documents into the embedded engine.</summary>
/// <remarks>
/// The preview follows the open documents that show a scene, a model document's preview scene included: it shows the one
/// opened last until <see cref="Show"/> names another, projects its changes in the order the scene raised them, takes its
/// reloaded content and retires it when it closes. Whatever the engine does not take yet waits and is replayed once the
/// engine runs.
/// </remarks>
internal sealed partial class ScenePreview : IScenePreview, IDisposable
{
    private static readonly TimeSpan NodeCreationTimeout = TimeSpan.FromSeconds(10);

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Usage", "CA2213:Disposable fields should be disposed", Justification = "The application owns the injected engine service; disposing the preview must not shut down the shared runtime.")]
    private readonly IEngineService engineService;
    private readonly IProjectSession session;
    private readonly SceneStructureStore structureStore;
    private readonly ILogger<ScenePreview> logger;
    private readonly IOperationResultPublisher? operationResults;
    private readonly Oxygen.Editor.Assets.IAssetFeed? assetFeed;
    private readonly LiveSyncCoalescer coalescer = new();
    private readonly PendingPropertySyncQueue pendingPropertySyncs = new();
    private readonly SemaphoreSlim sceneSyncGate = new(initialCount: 1, maxCount: 1);
    private volatile WorldDispatch? activeWorld;
    private volatile Scene? activeScene;

    /// <summary>Initializes a new instance of the <see cref="ScenePreview"/> class, following the open documents.</summary>
    /// <param name="engineService">The embedded engine.</param>
    /// <param name="documents">The workspace's open documents.</param>
    /// <param name="scenes">The scene each open document shows, a model document's preview scene included.</param>
    /// <param name="session">The project session whose scene documents the preview shows.</param>
    /// <param name="structureStore">Keeps the structures the preview reads of the instanced sources.</param>
    /// <param name="loggerFactory">The optional diagnostic logger factory.</param>
    /// <param name="operationResults">Publishes the engine's rejections and failures.</param>
    /// <param name="assetFeed">Reports which assets are mounted, so geometry waits for its asset.</param>
    public ScenePreview(
        IEngineService engineService,
        OpenDocuments documents,
        DocumentScenes scenes,
        IProjectSession session,
        SceneStructureStore structureStore,
        ILoggerFactory? loggerFactory = null,
        IOperationResultPublisher? operationResults = null,
        Oxygen.Editor.Assets.IAssetFeed? assetFeed = null)
    {
        this.engineService = engineService ?? throw new ArgumentNullException(nameof(engineService));
        this.documents = documents ?? throw new ArgumentNullException(nameof(documents));
        this.scenes = scenes ?? throw new ArgumentNullException(nameof(scenes));
        this.session = session ?? throw new ArgumentNullException(nameof(session));
        this.structureStore = structureStore ?? throw new ArgumentNullException(nameof(structureStore));
        this.logger = loggerFactory?.CreateLogger<ScenePreview>() ?? NullLogger<ScenePreview>.Instance;
        this.operationResults = operationResults;
        this.assetFeed = assetFeed;
        this.FollowDocuments();
    }

    /// <summary>Rebuilds a followed scene in the engine from a snapshot, even when the engine already shows it.</summary>
    /// <param name="scene">The scene of a followed document.</param>
    /// <param name="cancellationToken">Cancels the rebuild.</param>
    /// <returns><see langword="true"/> when the engine shows the scene's current content.</returns>
    internal Task<bool> SyncSceneAsync(Scene scene, CancellationToken cancellationToken = default)
        => this.SyncSceneCoreAsync(scene, skipIfCurrent: false, cancellationToken);

    internal Task<SyncOutcome> UpdateNodeTransformAsync(
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

    internal Task<SyncOutcome> AttachGeometryAsync(
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

    /// <summary>
    /// Sends a node's overrides without attaching its geometry again: clears the parts whose overrides all went away, then,
    /// when asked, sets each part the node overrides.
    /// </summary>
    /// <remarks>Attaching the geometry again would start a new geometry load and make the engine reject these as stale.</remarks>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <param name="cleared">The parts whose overrides all went away.</param>
    /// <param name="overrides">Whether to send the overrides the node has.</param>
    /// <param name="cancellationToken">Cancels the projection.</param>
    /// <returns>The engine's outcome.</returns>
    internal Task<SyncOutcome> UpdateOverridesAsync(
        Scene scene,
        SceneNode node,
        IReadOnlyCollection<OverridePart> cleared,
        bool overrides,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(cleared);
        var scope = Scope(scene, node, componentType: nameof(GeometryComponent));
        return this.TryClassifyReadiness(
                this.engineService,
                scene,
                SceneOperationKinds.EditGeometry,
                scope,
                cancellationToken,
                out var readinessOutcome)
            ? Task.FromResult(readinessOutcome)
            : this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditGeometry,
            nameof(GeometryComponent),
            LiveSyncDiagnosticCodes.GeometryRejected,
            LiveSyncDiagnosticCodes.GeometryFailed,
            world =>
            {
                if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is not { } geometry)
                {
                    return;
                }

                foreach (var part in cleared)
                {
                    world.Execute(SetOverrides(node.Id, geometry, part, []));
                }

                if (overrides)
                {
                    ApplyOverrides(world, node, geometry);
                }
            },
            cancellationToken);
    }

    internal Task<SyncOutcome> DetachGeometryAsync(
        Scene scene,
        NodeId nodeId,
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

    internal Task<SyncOutcome> AttachLightAsync(
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

    internal Task<SyncOutcome> DetachLightAsync(
        Scene scene,
        NodeId nodeId,
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

    internal Task<SyncOutcome> AttachLocalFogVolumeAsync(
        Scene scene,
        SceneNode node,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);

        var volume = node.Components.OfType<LocalFogVolumeComponent>().FirstOrDefault();
        return volume is null
            ? Task.FromResult(
                Rejected(
                    SceneOperationKinds.EditLocalFogVolume,
                    Scope(scene, node, componentType: nameof(LocalFogVolumeComponent)),
                    LiveSyncDiagnosticCodes.LocalFogVolumeRejected,
                    "Node has no local fog volume to attach."))
            : this.ExecuteNodeSyncAsync(
            scene,
            node,
            node.Id,
            SceneOperationKinds.EditLocalFogVolume,
            nameof(LocalFogVolumeComponent),
            LiveSyncDiagnosticCodes.LocalFogVolumeRejected,
            LiveSyncDiagnosticCodes.LocalFogVolumeFailed,
            world => ApplyLocalFogVolume(world, node, volume),
            cancellationToken);
    }

    internal Task<SyncOutcome> DetachLocalFogVolumeAsync(
        Scene scene,
        NodeId nodeId,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(scene);

        return this.ExecuteNodeSyncAsync(
            scene,
            node: null,
            nodeId,
            SceneOperationKinds.EditLocalFogVolume,
            nameof(LocalFogVolumeComponent),
            LiveSyncDiagnosticCodes.LocalFogVolumeRejected,
            LiveSyncDiagnosticCodes.LocalFogVolumeFailed,
            world => world.Execute(new RuntimeDetachLocalFogVolume(nodeId)),
            cancellationToken);
    }

    internal Task<SyncOutcome> AttachCameraAsync(
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

    internal Task<SyncOutcome> DetachCameraAsync(
        Scene scene,
        NodeId nodeId,
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

    internal Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
        Scene scene,
        SceneEnvironmentData environment,
        CancellationToken cancellationToken = default)
        => this.UpdateEnvironmentAsync(scene, environment, this.CaptureRevision(scene), cancellationToken);

    internal async Task CreateNodeAsync(SceneNode node, NodeId? parentGuid = null)
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

    internal Task RemoveNodeHierarchiesAsync(Scene scene, IReadOnlyList<NodeId> rootNodeIds)
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

        this.ExecuteOrDefer(scene, world, new RuntimeRemoveSceneNodes([.. rootNodeIds]));
        return Task.CompletedTask;
    }

    internal Task ReparentHierarchiesAsync(Scene scene, IReadOnlyList<NodeId> nodeIds, NodeId? newParentGuid, bool preserveWorldTransform = false)
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

        this.ExecuteOrDefer(scene, world, new RuntimeReparentSceneNodes([.. nodeIds], newParentGuid, preserveWorldTransform));
        return Task.CompletedTask;
    }

    internal Task<SyncOutcome> RenameNodeAsync(Scene scene, NodeId nodeId, string newName)
    {
        ArgumentNullException.ThrowIfNull(scene);

        return this.ExecuteNodeSyncAsync(
            scene,
            node: null,
            nodeId,
            SceneOperationKinds.NodeRename,
            componentType: null,
            LiveSyncDiagnosticCodes.RenameRejected,
            LiveSyncDiagnosticCodes.RenameFailed,
            world => world.Execute(new RuntimeRenameSceneNode(nodeId, newName)),
            CancellationToken.None);
    }

    internal Task<SyncOutcome> UpdatePropertiesAsync(
        Scene scene,
        SceneNode node,
        IReadOnlyList<EnginePropertyValueEntry> entries,
        CancellationToken cancellationToken = default)
        => this.UpdatePropertiesAsync(scene, node, entries, this.CaptureRevision(scene), cancellationToken);

    /// <summary>Gets the number of a scene's fields and mutations still waiting for the engine.</summary>
    /// <param name="sceneId">The scene.</param>
    /// <returns>The number of waiting deliveries.</returns>
    internal int GetPendingPropertySyncCount(SceneId sceneId)
    {
        lock (this.documentGate)
        {
            return this.pendingPropertySyncs.Count(sceneId)
                + (this.openScenes.TryGetValue(sceneId, out var lifetime) ? lifetime.PendingMutations.Count + (lifetime.PendingEnvironment is null ? 0 : 1) : 0);
        }
    }

    internal bool ShouldIssuePreviewSync(SceneId sceneId, Guid previewTarget, DateTimeOffset observedAt)
        => this.coalescer.ShouldIssuePreview(new SyncCoalescingKey(sceneId, previewTarget), observedAt);

    /// <summary>Projects one sample of a gesture, unless the gesture's target was projected less than 16 ms ago.</summary>
    /// <remarks>A dropped sample loses nothing: the gesture's next sample, or its commit, carries the newer value.</remarks>
    /// <param name="sceneId">The scene.</param>
    /// <param name="previewTarget">What the gesture edits: a node, or the scene for its environment.</param>
    /// <param name="observedAt">When the sample was taken.</param>
    /// <param name="sync">Projects the sample.</param>
    /// <param name="cancellationToken">Cancels the projection.</param>
    /// <returns>The engine's outcome, or <see langword="null"/> when the sample was dropped.</returns>
    internal async Task<SyncOutcome?> TryPreviewSyncAsync(
        SceneId sceneId,
        Guid previewTarget,
        DateTimeOffset observedAt,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sync);

        return this.ShouldIssuePreviewSync(sceneId, previewTarget, observedAt)
            ? await sync(cancellationToken).ConfigureAwait(false)
            : null;
    }

    internal bool CompleteTerminalSync(SceneId sceneId, Guid previewTarget)
        => this.coalescer.CompleteTerminalSync(new SyncCoalescingKey(sceneId, previewTarget));

    /// <summary>Projects the committed value of a gesture, always, and ends the throttling of its target.</summary>
    /// <param name="sceneId">The scene.</param>
    /// <param name="previewTarget">What the gesture edited.</param>
    /// <param name="sync">Projects the committed value.</param>
    /// <param name="cancellationToken">Cancels the projection.</param>
    /// <returns>The engine's outcome.</returns>
    internal async Task<SyncOutcome> CompleteTerminalSyncAsync(
        SceneId sceneId,
        Guid previewTarget,
        Func<CancellationToken, Task<SyncOutcome>> sync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(sync);

        _ = this.CompleteTerminalSync(sceneId, previewTarget);
        return await sync(cancellationToken).ConfigureAwait(false);
    }

    internal void CancelPreviewSync(SceneId sceneId, Guid previewTarget)
        => this.coalescer.Cancel(new SyncCoalescingKey(sceneId, previewTarget));

    /// <summary>Projects the value a cancelled gesture restored, always, and ends the throttling of its target.</summary>
    /// <param name="sceneId">The scene.</param>
    /// <param name="previewTarget">What the gesture edited.</param>
    /// <param name="revertSync">Projects the restored value.</param>
    /// <param name="cancellationToken">Cancels the projection.</param>
    /// <returns>The engine's outcome.</returns>
    internal async Task<SyncOutcome> CancelPreviewSyncAsync(
        SceneId sceneId,
        Guid previewTarget,
        Func<CancellationToken, Task<SyncOutcome>> revertSync,
        CancellationToken cancellationToken = default)
    {
        ArgumentNullException.ThrowIfNull(revertSync);

        this.CancelPreviewSync(sceneId, previewTarget);
        return await revertSync(cancellationToken).ConfigureAwait(false);
    }

    private static RuntimeSetEnvironment BuildEnvironmentCommand(SceneEnvironmentData environment)
    {
        var sky = environment.SkyAtmosphere ?? new SkyAtmosphereEnvironmentData();
        var post = environment.PostProcess ?? new PostProcessEnvironmentData();
        var skySphere = environment.SkySphere ?? new SkySphereEnvironmentData();
        var skyLight = environment.SkyLight ?? new SkyLightEnvironmentData();
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
                [.. post.AutoExposureCompensationCurve.Select(static key => new RuntimeExposureCompensationKey(key.MeteredEv, key.CompensationEv))],
                ToTextureReference(post.AutoExposureMeteringMask),
                post.BloomIntensity,
                post.BloomThreshold,
                post.Saturation,
                post.Contrast,
                post.VignetteIntensity,
                post.DisplayGamma)
        {
            Fog = environment.Fog ?? new(),
            SkySphere = skySphere,

            // Only a cubemap the renderer uses is loaded, so a stale reference
            // behind another source or a disabled system cannot reject the edit.
            SkySphereCubemap = ToTextureReference(skySphere.ActiveCubemap),
            SkyLight = skyLight,
            SkyLightCubemap = ToTextureReference(skyLight.ActiveCubemap),
            Background = environment.Background ?? new(),
        };
    }

    // The engine resolves a texture by its identity's virtual path in whatever
    // content is mounted, project or engine, so the reference is the identity.
    private static AssetPath? ToTextureReference(AssetPath? texture)
    {
        if (texture is not null)
        {
            _ = ContentPipeline.ContentPipelinePaths.ToNativeDescriptorPath(texture, AssetFileTypes.Texture);
        }

        return texture;
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
                return this.PublishExistingSceneReady(scene);
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
            return EnvironmentResult(readinessOutcome);
        }

        var environmentOutcome = await this.SyncEnvironmentSystemsAsync(scene, environment, cancellationToken, dispatchOverride).ConfigureAwait(false);
        return EnvironmentResult(environmentOutcome);
    }

    private async Task<bool> BuildSceneInEngineAsync(
        SceneProjection projection,
        WorldDispatch world,
        CancellationToken cancellationToken)
    {
        var scene = projection.Snapshot;
        cancellationToken.ThrowIfCancellationRequested();
        var target = new RuntimeSceneTarget(world.Commands.RunId, scene.Id, projection.Lifetime.Id, Guid.NewGuid()) { ProjectId = this.session.Context.ProjectId };
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

        world.Execute(BuildEnvironmentCommand(scene.Environment));
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
            world => world.Execute(BuildEnvironmentCommand(environment)),
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
        NodeId nodeId,
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

    // Every component is applied even after one failed, so one failure does not hide the others.
    private bool ApplyRenderableComponents(WorldDispatch world, SceneNode node)
    {
        var succeeded = TryApply(() => ApplyNodeFlags(world, node), exception => this.LogFailedToApplyNodeFlags(exception, node.Id));
        if (node.Components.OfType<GeometryComponent>().FirstOrDefault() is { } geometry)
        {
            succeeded &= TryApply(() => ApplyGeometry(world, node, geometry), exception => this.LogFailedToAttachGeometry(exception, node));
        }

        if (node.Components.OfType<CameraComponent>().FirstOrDefault() is { } camera)
        {
            succeeded &= TryApply(() => this.ApplyCamera(world, node, camera), exception => this.LogFailedToAttachCameraComponent(exception, node.Id));
        }

        if (node.Components.OfType<LightComponent>().FirstOrDefault() is { } light)
        {
            succeeded &= TryApply(() => ApplyLight(world, node, light), exception => this.LogFailedToAttachLightComponent(exception, node.Id));
        }

        if (node.Components.OfType<LocalFogVolumeComponent>().FirstOrDefault() is { } volume)
        {
            succeeded &= TryApply(() => ApplyLocalFogVolume(world, node, volume), exception => this.LogFailedToAttachLocalFogVolume(exception, node.Id));
        }

        if (InstanceOf(node) is { } instance)
        {
            succeeded &= TryApply(() => ApplyInstance(world, node, instance), exception => this.LogFailedToInstantiate(exception, node.Id));
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
                    perspective.FarPlane,
                    perspective.AspectMode));
                ApplyCameraExposure(world, node, RuntimeComponentId.PerspectiveCamera, perspective, (ushort)PerspectiveCameraField.ApertureF, (ushort)PerspectiveCameraField.ShutterRate, (ushort)PerspectiveCameraField.Iso);
                break;

            case OrthographicCamera orthographic:
                world.Execute(new RuntimeAttachOrthographicCamera(
                    node.Id,
                    orthographic.OrthographicSize,
                    orthographic.AspectRatio,
                    orthographic.NearPlane,
                    orthographic.FarPlane,
                    orthographic.AspectMode));
                ApplyCameraExposure(world, node, RuntimeComponentId.OrthographicCamera, orthographic, (ushort)OrthographicCameraField.ApertureF, (ushort)OrthographicCameraField.ShutterRate, (ushort)OrthographicCameraField.Iso);
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

            foreach (var child in node.Hierarchy.Children)
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
            new SceneNode(scene, "__sync_barrier__"),
            parentGuid: null,
            initializeWorldAsRoot: false,
            CancellationToken.None).ConfigureAwait(false);

        return false;
    }

    private bool ApplyHierarchyAndComponents(Scene scene, WorldDispatch world)
    {
        var succeeded = true;

        // Every node exists natively: the projection only gets here once all of them were created.
        void ResolveAndApply(SceneNode node, NodeId? parentGuid)
        {
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

            foreach (var child in node.Hierarchy.Children)
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
            // Every node of the projected scene exists natively.
            var handles = scene.AllNodes.Select(static node => node.Id).ToImmutableArray();
            if (handles.Length > 0)
            {
                world.Execute(new RuntimeUpdateTransformsForNodes(handles));
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
                        var transform = node.Transform;
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
