// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Interop;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts managed runtime contracts after the native session boundary has been entered.</summary>
internal static class NativeSessionConversions
{
    /// <summary>Maps supported options by their named engine meaning.</summary>
    /// <typeparam name="TNative">The native public enum.</typeparam>
    /// <param name="value">The managed runtime option.</param>
    /// <returns>The corresponding native option.</returns>
    public static TNative ToNative<TNative>(Enum value)
        where TNative : struct, Enum
        => Enum.IsDefined(value.GetType(), value) && Enum.TryParse<TNative>(value.ToString(), ignoreCase: false, out var native) && Enum.IsDefined(native)
            ? native : throw new ArgumentOutOfRangeException(nameof(value), value, "The runtime option is not supported by the native contract.");

    /// <summary>Converts a native editor camera state.</summary>
    /// <param name="state">The native state, or <see langword="null"/>.</param>
    /// <returns>The managed state, or <see langword="null"/>.</returns>
    public static RuntimeEditorCamera? FromNative(EditorCameraStateManaged? state)
        => state is null ? null : new(state.Position, state.Rotation, state.FocusPoint, state.OrthographicSize);

    /// <summary>Converts a native pick result.</summary>
    /// <param name="result">The native result, or <see langword="null"/>.</param>
    /// <returns>The managed result, or <see langword="null"/>.</returns>
    public static RuntimePickResult? FromNative(ViewPickResultManaged? result)
        => result is null
            ? null
            : new(
                [.. result.Hits.Select(hit => new RuntimePickHit(FromNative(hit.Node), hit.Depth, hit.GeometrySlot, hit.CenterDistance))],
                result.WorldPosition);

    /// <summary>Converts a native node reference.</summary>
    /// <param name="reference">The native reference.</param>
    /// <returns>The managed reference.</returns>
    public static NodeReference FromNative(NodeReferenceManaged reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return new(NodeId.FromValue(reference.Node), NodePath.Of(reference.Path.Select(NodeId.FromValue)));
    }

    /// <summary>Converts a node reference for the native side.</summary>
    /// <param name="reference">The managed reference.</param>
    /// <returns>The native reference.</returns>
    public static NodeReferenceManaged ToNative(NodeReference reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        return new NodeReferenceManaged { Node = reference.Node.Value, Path = [.. reference.Path.Nodes.Select(static id => id.Value)] };
    }

    /// <summary>Converts a native framing outcome by its named meaning.</summary>
    /// <param name="outcome">The native outcome.</param>
    /// <returns>The managed outcome.</returns>
    public static RuntimeFramingOutcome FromNative(ViewFramingOutcomeManaged outcome)
        => Enum.TryParse<RuntimeFramingOutcome>(outcome.ToString(), ignoreCase: false, out var managed) && Enum.IsDefined(managed)
            ? managed : RuntimeFramingOutcome.InvalidBounds;

    /// <summary>Converts a native gizmo interaction.</summary>
    /// <param name="gizmoEvent">The native interaction.</param>
    /// <returns>The managed interaction.</returns>
    public static RuntimeGizmoEvent FromNative(TransformGizmoEventManaged gizmoEvent)
        => new(
            (RuntimeGizmoEventKind)(int)gizmoEvent.Kind,
            new RuntimeViewId(gizmoEvent.ViewId.Value),
            (RuntimeTransformTool)(int)gizmoEvent.Tool,
            (RuntimeGizmoHandle)(int)gizmoEvent.Handle,
            gizmoEvent.Duplicate,
            gizmoEvent.Hovering,
            gizmoEvent.Representable,
            [.. gizmoEvent.Targets.Select(static target => new RuntimeGizmoTarget(FromNative(target.Node), target.Position, target.Rotation, target.Scale))],
            gizmoEvent.ReadoutAxes,
            gizmoEvent.ReadoutValues,
            gizmoEvent.PivotPixel,
            gizmoEvent.PointerPixel,
            gizmoEvent.Helper,
            gizmoEvent.Node is null ? null : FromNative(gizmoEvent.Node),
            (RuntimeHelperHandle)(int)gizmoEvent.HelperHandle,
            gizmoEvent.Value,
            (RuntimeHelperLight)(int)gizmoEvent.HelperLight);

    /// <summary>Creates the native ground grid config.</summary>
    /// <param name="settings">The editor's grid settings.</param>
    /// <returns>The native configuration.</returns>
    public static GroundGridConfigManaged ToNative(GroundGridSettings settings) => new()
    {
        Enabled = settings.Enabled,
        Spacing = settings.Spacing,
        MajorEvery = (uint)Math.Max(settings.MajorEvery, 1),
        LineThickness = settings.LineThickness,
        MajorThickness = settings.MajorThickness,
        AxisThickness = settings.AxisThickness,
        FadeStart = settings.FadeStart,
        FadePower = settings.FadePower,
        HorizonBoost = settings.HorizonBoost,
        OriginX = settings.OriginX,
        OriginY = settings.OriginY,
        SmoothMotion = settings.SmoothMotion,
        SmoothTime = settings.SmoothTime,
        MinorColor = ToNative(settings.MinorColor),
        MajorColor = ToNative(settings.MajorColor),
        AxisColorX = ToNative(settings.AxisColorX),
        AxisColorY = ToNative(settings.AxisColorY),
        OriginColor = ToNative(settings.OriginColor),
    };

    /// <summary>Creates the native view config while preserving omitted native defaults.</summary>
    /// <param name="config">The editor's managed view request.</param>
    /// <returns>The native configuration.</returns>
    public static ViewConfigManaged ToNative(RuntimeViewConfig config)
    {
        var native = new ViewConfigManaged
        {
            Name = config.Name,
            Purpose = config.Purpose,
            CompositingTarget = config.CompositingTarget,
            CameraPreset = ToNative<CameraViewPresetManaged>(config.CameraPreset),
            SceneCamera = config.SceneCameraNodeId?.Value,
            InsetHost = config.InsetHost is { } host ? new ViewIdManaged(host.Value) : null,
            ViewMode = ToNative<ViewModeManaged>(config.ViewMode),
            ShowGrid = config.ShowGrid,
            ShowSelectionOutline = config.ShowSelectionOutline,
            ShowIcons = config.ShowIcons,
        };
        if (config.EditorCamera is { } camera)
        {
            native.EditorCamera = new EditorCameraStateManaged
            {
                Position = camera.Position,
                Rotation = camera.Rotation,
                FocusPoint = camera.FocusPoint,
                OrthographicSize = camera.OrthographicSize,
            };
        }

        if (config.Width is { } width)
        {
            native.Width = width;
        }

        if (config.Height is { } height)
        {
            native.Height = height;
        }

        if (config.ClearColor is { } color)
        {
            native.ClearColor = new(color.R, color.G, color.B, color.A);
        }

        return native;
    }

    private static ColorManaged ToNative(RuntimeColor color) => new(color.R, color.G, color.B, color.A);
}
