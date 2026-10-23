// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Runtime.Diagnostics;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Scenes;

/// <summary>Builds the outcomes and scopes of projections.</summary>
internal sealed partial class ScenePreview
{
    private static SceneNode? FindNode(Scene scene, NodeId nodeId)
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

    private static string GetPropertyOperationKind(IReadOnlyList<EnginePropertyValueEntry> entries)
        => GetFirstComponent(entries) switch
        {
            RuntimeComponentId.PerspectiveCamera => SceneOperationKinds.EditPerspectiveCamera,
            RuntimeComponentId.OrthographicCamera => SceneOperationKinds.EditOrthographicCamera,
            RuntimeComponentId.DirectionalLight => SceneOperationKinds.EditDirectionalLight,
            RuntimeComponentId.PointLight => SceneOperationKinds.EditPointLight,
            RuntimeComponentId.SpotLight => SceneOperationKinds.EditSpotLight,
            RuntimeComponentId.LocalFogVolume => SceneOperationKinds.EditLocalFogVolume,
            RuntimeComponentId.Node => SceneOperationKinds.EditNodeFlags,
            _ => SceneOperationKinds.EditTransform,
        };

    private static string GetPropertyComponentType(IReadOnlyList<EnginePropertyValueEntry> entries)
        => GetFirstComponent(entries) switch
        {
            RuntimeComponentId.PerspectiveCamera => nameof(PerspectiveCamera),
            RuntimeComponentId.OrthographicCamera => nameof(OrthographicCamera),
            RuntimeComponentId.DirectionalLight => nameof(DirectionalLightComponent),
            RuntimeComponentId.PointLight => nameof(PointLightComponent),
            RuntimeComponentId.SpotLight => nameof(SpotLightComponent),
            RuntimeComponentId.LocalFogVolume => nameof(LocalFogVolumeComponent),
            RuntimeComponentId.Node => nameof(SceneNode),
            _ => nameof(TransformComponent),
        };

    private static RuntimeComponentId GetFirstComponent(IReadOnlyList<EnginePropertyValueEntry> entries)
        => entries.Count == 0 ? RuntimeComponentId.Transform : entries[0].Component;

    private static string GetPropertyRejectedCode(string operationKind)
        => operationKind switch
        {
            SceneOperationKinds.EditPerspectiveCamera or SceneOperationKinds.EditOrthographicCamera => LiveSyncDiagnosticCodes.CameraRejected,
            SceneOperationKinds.EditDirectionalLight or SceneOperationKinds.EditPointLight or SceneOperationKinds.EditSpotLight => LiveSyncDiagnosticCodes.LightRejected,
            SceneOperationKinds.EditLocalFogVolume => LiveSyncDiagnosticCodes.LocalFogVolumeRejected,
            SceneOperationKinds.EditNodeFlags => LiveSyncDiagnosticCodes.MutationRejected,
            _ => LiveSyncDiagnosticCodes.TransformRejected,
        };

    private static string GetPropertyFailedCode(string operationKind)
        => operationKind switch
        {
            SceneOperationKinds.EditPerspectiveCamera or SceneOperationKinds.EditOrthographicCamera => LiveSyncDiagnosticCodes.CameraFailed,
            SceneOperationKinds.EditDirectionalLight or SceneOperationKinds.EditPointLight or SceneOperationKinds.EditSpotLight => LiveSyncDiagnosticCodes.LightFailed,
            SceneOperationKinds.EditLocalFogVolume => LiveSyncDiagnosticCodes.LocalFogVolumeFailed,
            SceneOperationKinds.EditNodeFlags => LiveSyncDiagnosticCodes.MutationFailed,
            _ => LiveSyncDiagnosticCodes.TransformFailed,
        };

    // Runs one engine projection step; a recoverable engine failure is reported and lets the next step run.
    private static bool TryApply(Action apply, Action<Exception> reportFailure)
    {
        try
        {
            apply();
            return true;
        }
        catch (Exception exception) when (EngineInteropExceptionPolicy.IsRecoverable(exception))
        {
            reportFailure(exception);
            return false;
        }
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
        NodeId? nodeId = null,
        string? componentType = null,
        string? componentName = null,
        string? assetVirtualPath = null)
        => new()
        {
            SceneId = scene.Id,
            SceneName = scene.Name,
            NodeId = node?.Id ?? nodeId,
            NodeName = node?.Metadata.Name,
            ComponentType = componentType,
            ComponentName = componentName,
            AssetVirtualPath = assetVirtualPath,
        };

    private static void ApplyTransform(WorldDispatch world, SceneNode node)
    {
        var transform = node.Transform;
        if (transform is not null)
        {
            var (position, rotation, scale) = TransformConverter.ToNative(transform);
            world.Execute(new RuntimeSetLocalTransform(node.Id, position, rotation, scale));
        }
    }

    private static void ApplyCameraExposure(
        WorldDispatch world,
        SceneNode node,
        RuntimeComponentId component,
        CameraComponent camera,
        ushort apertureField,
        ushort shutterField,
        ushort isoField)
    {
        ImmutableArray<RuntimePropertyValue> exposure =
        [
            new(component, apertureField, new RuntimeFieldValue.Scalar(camera.ApertureF)),
            new(component, shutterField, new RuntimeFieldValue.Scalar(camera.ShutterRate)),
            new(component, isoField, new RuntimeFieldValue.Scalar(camera.Iso)),
        ];
        world.Execute(new RuntimeSetProperties(node.Id, exposure));
    }

    private static void ApplyNodeFlags(WorldDispatch world, SceneNode node)
    {
        var source = node.Flags;
        ImmutableArray<RuntimePropertyValue> flags =
        [
            Value(NodeField.Visible, new RuntimeFieldValue.Integer((int)source.Visibility)),
            Value(NodeField.CastsShadows, new RuntimeFieldValue.Integer((int)source.CastsShadows)),
            Value(NodeField.ReceivesShadows, new RuntimeFieldValue.Integer((int)source.ReceivesShadows)),
            Value(NodeField.Static, new RuntimeFieldValue.Flag(source.IsStatic)),
            Value(NodeField.RayCastingSelectable, new RuntimeFieldValue.Flag(source.IsRayCastingSelectable)),
            Value(NodeField.IgnoreParentTransform, new RuntimeFieldValue.Flag(source.IgnoreParentTransform)),
        ];
        world.Execute(new RuntimeSetProperties(node.Id, flags));

        // Visibility and the shadow flags send their source mode's wire value; the others their local bool.
        static RuntimePropertyValue Value(NodeField field, RuntimeFieldValue value)
            => new(RuntimeComponentId.Node, (ushort)field, value);
    }

    private static void ApplyGeometry(WorldDispatch world, SceneNode node, GeometryComponent geometry)
    {
        world.Execute(new RuntimeSetGeometry(node.Id, GeometryPathMapper.ToEnginePath(geometry.Geometry)));
        ApplyOverrides(world, node, geometry);
    }

    // The engine takes a geometry's overrides one part at a time, each with the part's whole state: the component's own
    // values, then each surface's.
    private static void ApplyOverrides(WorldDispatch world, SceneNode node, GeometryComponent geometry)
    {
        foreach (var part in OverridePartsOf(geometry))
        {
            world.Execute(SetOverrides(node.Id, geometry, part, geometry.Overrides.All.Where(entry => entry.Part == part.Part)));
        }
    }

    // A part with no values clears every override of the part.
    private static RuntimeSetOverrides SetOverrides(NodeId node, GeometryComponent geometry, OverridePart part, IEnumerable<SourceOverride> values)
        => new(
            node,
            GeometryPathMapper.ToEnginePath(geometry.Geometry),
            part.Part is { } surface ? new RuntimeOverridePart(SurfaceIdOf(surface), part.LayoutRevision!) : null,
            [.. values.Select(ToRuntimeOverride)],
            OverrideIntent.RetainedOverride);

    // Surfaces are the only part kind of a geometry.
    private static Guid SurfaceIdOf(PartId part)
        => string.Equals(part.Kind, SceneSchema.Geometry.Surface.Kind, StringComparison.Ordinal)
            ? part.Id
            : throw new InvalidOperationException($"Geometry part '{part}' is not a surface.");

    // Each value is encoded once per value type of the schema; materials are the only asset overrides.
    private static RuntimeOverride ToRuntimeOverride(SourceOverride entry)
    {
        if (!SceneWire.TryGetOverrideId(entry.Property, out var id))
        {
            throw new InvalidOperationException($"Property '{entry.Property}' is not an override the engine takes.");
        }

        RuntimeOverrideValue value = entry.Value switch
        {
            AssetPath material when entry.Property == SceneSchema.Geometry.Surface.Material.Id
                => new RuntimeOverrideValue.Asset(MaterialOverridePathMapper.ToEnginePath(material)!),
            bool flag => new RuntimeOverrideValue.Flag(flag),
            LodPolicy policy => new RuntimeOverrideValue.LodPolicyValue(policy),
            _ => throw new InvalidOperationException($"Override '{entry.Property}' holds a {entry.Value.GetType().Name}, which has no engine encoding."),
        };
        return new(id, value);
    }

    // The parts a geometry overrides: the component itself when it has values, and each surface with values.
    private static OverridePart[] OverridePartsOf(GeometryComponent geometry)
        => [.. geometry.Overrides.All.Select(static entry => new OverridePart(entry.Part, entry.LayoutRevision)).Distinct()];

    private static void ApplyLight(WorldDispatch world, SceneNode node, LightComponent light)
    {
        switch (light)
        {
            case DirectionalLightComponent directional:
                ApplyDirectionalLight(world, node, directional);
                break;

            case PointLightComponent point:
                world.Execute(new RuntimeAttachPointLight(
                    node.Id,
                    point.LuminousFluxLumens,
                    point.Range,
                    point.SourceRadius,
                    point.Color,
                    point.AffectsWorld,
                    point.CastsShadows,
                    point.ShadowBias,
                    point.ShadowNormalBias,
                    point.ContactShadows,
                    (int)point.ShadowResolutionHint,
                    point.ExposureCompensation));
                break;

            case SpotLightComponent spot:
                world.Execute(new RuntimeAttachSpotLight(
                    node.Id,
                    spot.LuminousFluxLumens,
                    spot.Range,
                    spot.SourceRadius,
                    spot.InnerConeAngleRadians,
                    spot.OuterConeAngleRadians,
                    spot.Color,
                    spot.AffectsWorld,
                    spot.CastsShadows,
                    spot.ShadowBias,
                    spot.ShadowNormalBias,
                    spot.ContactShadows,
                    (int)spot.ShadowResolutionHint,
                    spot.ExposureCompensation));
                break;
        }
    }

    private static void ApplyLocalFogVolume(WorldDispatch world, SceneNode node, LocalFogVolumeComponent volume)
        => world.Execute(new RuntimeAttachLocalFogVolume(
            node.Id,
            volume.Enabled,
            volume.RadialFogExtinction,
            volume.HeightFogExtinction,
            volume.HeightFogFalloff,
            volume.HeightFogOffset,
            volume.FogPhaseG,
            volume.FogAlbedo,
            volume.FogEmissive,
            volume.SortPriority));

    private static void ApplyDirectionalLight(WorldDispatch world, SceneNode node, DirectionalLightComponent directional)
        => world.Execute(new RuntimeAttachDirectionalLight(
            node.Id,
            directional.IntensityLux,
            directional.AngularSizeRadians,
            directional.Color,
            directional.AffectsWorld,
            0,
            directional.CastsShadows,
            directional.ShadowBias,
            directional.ShadowNormalBias,
            directional.ContactShadows,
            (int)directional.ShadowResolutionHint,
            directional.ExposureCompensation,
            (int)directional.AtmosphereSlot,
            directional.UsePerPixelAtmosphereTransmittance,
            directional.AtmosphereDiskLuminanceScaleRgb,
            directional.CascadeCount,
            (int)directional.SplitMode,
            directional.MaxShadowDistance,
            directional.CascadeDistances,
            directional.DistributionExponent,
            directional.TransitionFraction,
            directional.DistanceFadeoutFraction));

    private static float ToEngineFieldOfViewRadians(float fieldOfViewDegrees)
    {
        var degrees = float.IsFinite(fieldOfViewDegrees) && fieldOfViewDegrees > 0.0f
            ? fieldOfViewDegrees
            : PerspectiveCamera.DefaultFieldOfViewDegrees;

        return degrees * (MathF.PI / 180.0f);
    }

    private bool TryClassifyReadiness(
        IEngineService engineService,
        Scene scene,
        string operationKind,
        AffectedScope scope,
        CancellationToken cancellationToken,
        out SyncOutcome outcome)
    {
        var classified = this.TryGetReadyWorld(
            engineService,
            scene,
            operationKind,
            scope,
            cancellationToken,
            out _,
            out outcome);
        return classified;
    }

    private bool TryGetReadyWorld(
        IEngineService engineService,
        Scene scene,
        string operationKind,
        AffectedScope scope,
        CancellationToken cancellationToken,
        out WorldDispatch? world,
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
            var commands = engineService.WorldCommands;
            world = commands is null ? null : this.activeWorld is { } active && active.Target.SceneId == scope.SceneId && ReferenceEquals(this.activeScene, scene) && ReferenceEquals(active.Commands, commands)
                ? active with { CancellationToken = cancellationToken } : null;
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

    private async Task<bool> CreateNodeWithCallbackAsync(
        WorldDispatch world,
        SceneNode node,
        NodeId? parentGuid,
        bool initializeWorldAsRoot,
        CancellationToken cancellationToken)
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(NodeCreationTimeout);
        var result = await world.Commands.CreateNodeAsync(
            new RuntimeWorldRequest(Guid.NewGuid(), world.Target, new RuntimeCreateNode(node.Metadata.Name, node.Id, parentGuid, initializeWorldAsRoot)),
            timeout.Token).ConfigureAwait(false);
        if (!result.Succeeded || timeout.IsCancellationRequested || this.activeWorld?.Target != world.Target)
        {
            return false;
        }

        // The node exists in the scene activation this preview still projects.
        return !cancellationToken.IsCancellationRequested && world.Commands.RunId == world.Target.RunId;
    }

    /// <summary>One part of a geometry's overrides, as the engine takes them.</summary>
    /// <param name="Part">The surface, or <see langword="null"/> for the geometry component's own properties.</param>
    /// <param name="LayoutRevision">The layout revision a surface's overrides were resolved against; <see langword="null"/> without a part.</param>
    internal sealed record OverridePart(PartId? Part, string? LayoutRevision);
}
