// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

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
                [.. result.Hits.Select(hit => new RuntimePickHit(hit.NodeId, hit.Depth, hit.GeometrySlot, hit.CenterDistance))],
                result.WorldPosition);

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
            [.. gizmoEvent.Targets.Select(target => new RuntimeGizmoTarget(target.NodeId, target.Position, target.Rotation, target.Scale))],
            gizmoEvent.ReadoutAxes,
            gizmoEvent.ReadoutValues,
            gizmoEvent.PivotPixel,
            gizmoEvent.PointerPixel);

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
            SceneCamera = config.SceneCameraNodeId,
            InsetHost = config.InsetHost is { } host ? new ViewIdManaged(host.Value) : null,
            ViewMode = ToNative<ViewModeManaged>(config.ViewMode),
            ShowGrid = config.ShowGrid,
            ShowSelectionOutline = config.ShowSelectionOutline,
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
}
