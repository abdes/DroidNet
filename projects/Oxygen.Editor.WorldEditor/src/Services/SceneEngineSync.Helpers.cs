// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Catalog;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Services;

/// <summary>
/// Static helpers for <see cref="SceneEngineSync"/>.
/// </summary>
public sealed partial class SceneEngineSync
{
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
            outcome = RuntimeFaulted(operationKind, scope);
            return true;
        }

        if (state != EngineServiceState.Running)
        {
            world = null;
            outcome = RuntimeNotRunning(operationKind, scope, state);
            return true;
        }

        try
        {
            world = engineService.World;
        }
        catch (InvalidOperationException ex)
        {
            world = null;
            outcome = RuntimeWorldUnavailable(operationKind, scope, ex);
            return true;
        }

        if (world is null)
        {
            outcome = RuntimeWorldUnavailable(operationKind, scope);
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

    private static SyncOutcome RuntimeFaulted(string operationKind, AffectedScope scope)
        => new(
            SyncStatus.SkippedNotRunning,
            operationKind,
            scope,
            LiveSyncDiagnosticCodes.RuntimeFaulted,
            "The runtime engine is faulted; live sync was skipped.");

    private static SyncOutcome RuntimeNotRunning(
        string operationKind,
        AffectedScope scope,
        EngineServiceState state)
        => new(
            SyncStatus.SkippedNotRunning,
            operationKind,
            scope,
            LiveSyncDiagnosticCodes.NotRunning,
            $"The runtime engine is {state}; live sync was skipped.");

    private static SyncOutcome RuntimeWorldUnavailable(
        string operationKind,
        AffectedScope scope,
        Exception? exception = null)
        => new(
            SyncStatus.SkippedNotRunning,
            operationKind,
            scope,
            LiveSyncDiagnosticCodes.NotRunning,
            "The runtime world is not available; live sync was skipped.",
            exception);

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

    private static async Task<bool> CreateNodeWithCallbackAsync(
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
        using var registration = timeout.Token.Register(
            static state =>
            {
                var completion = (TaskCompletionSource<bool>)state!;
                _ = completion.TrySetCanceled();
            },
            tcs);

        world.CreateSceneNode(
            node.Name,
            node.Id,
            parentGuid,
            nativeHandle =>
            {
                void SetActive()
                {
                    try
                    {
                        node.IsActive = true;
                        _ = tcs.TrySetResult(true);
                    }
                    catch (Exception ex) when (EngineInteropExceptionPolicy.IsRecoverable(ex))
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

    private static void ApplyTransform(Oxygen.Interop.World.OxygenWorld world, SceneNode node)
    {
        var transform = node.Components.OfType<TransformComponent>().FirstOrDefault();
        if (transform is not null)
        {
            var (position, rotation, scale) = TransformConverter.ToNative(transform);
            world.SetLocalTransform(node.Id, position, rotation, scale);
        }
    }

    private static void ApplyGeometry(Oxygen.Interop.World.OxygenWorld world, SceneNode node, GeometryComponent geometry)
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

    private static void ApplyLight(Oxygen.Interop.World.OxygenWorld world, SceneNode node, LightComponent light)
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

    private static float ToEngineFieldOfViewRadians(float fieldOfViewDegrees)
    {
        var degrees = float.IsFinite(fieldOfViewDegrees) && fieldOfViewDegrees > 0.0f
            ? fieldOfViewDegrees
            : PerspectiveCamera.DefaultFieldOfViewDegrees;

        return degrees * (MathF.PI / 180.0f);
    }
}
