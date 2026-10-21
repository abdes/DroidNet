// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using System.Text;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.WorldEditor.SceneEditor;

namespace Oxygen.Editor.LevelEditor;

/// <summary>The pane's side of the transform gizmo: hover, drag feedback and editing shortcuts.</summary>
public partial class ViewportViewModel
{
    private double displayScale = 1.0;
    private bool gizmoHovered;
    private bool helperHovered;

    /// <summary>Gets or sets the scene's viewport tools, shared by its panes.</summary>
    [ObservableProperty]
    public partial TransformToolsViewModel? TransformTools { get; set; }

    /// <summary>Gets a value indicating whether a gizmo drag of this pane is running.</summary>
    [ObservableProperty]
    public partial bool IsGizmoDragging { get; private set; }

    /// <summary>Gets the running drag's readout, or <see langword="null"/>.</summary>
    [ObservableProperty]
    public partial GizmoFeedback? GizmoFeedback { get; private set; }

    /// <summary>
    /// Gets a value indicating whether the pointer is over a gizmo handle, a helper handle or the
    /// orientation triad in this pane.
    /// </summary>
    /// <remarks>A press there drags the handle or picks a view, so it must not start a selection.</remarks>
    public bool IsGizmoHovered => this.gizmoHovered || this.helperHovered;

    /// <summary>Gets the number of gizmo drags this pane has started; a selection gesture that saw it change yields.</summary>
    public int GizmoDragCount { get; private set; }

    /// <summary>Gets or sets physical pixels per device-independent pixel on the pane's display.</summary>
    public double DisplayScale
    {
        get => this.displayScale;
        set
        {
            if (value > 0.0 && value != this.displayScale)
            {
                this.displayScale = value;
                this.DisplayScaleChanged?.Invoke();
            }
        }
    }

    /// <summary>Gets or sets the receiver of <see cref="DisplayScale"/> changes, which resizes the gizmo and helpers.</summary>
    public Action? DisplayScaleChanged { get; set; }

    /// <summary>Gets or sets the receiver of the pane's editing shortcuts.</summary>
    public Func<ViewportEditCommand, Task>? EditCommandRequested { get; set; }

    /// <summary>Formats the value a gizmo drag applies, as the pointer readout shows it.</summary>
    /// <param name="gizmoEvent">The drag's latest event.</param>
    /// <returns>The readout, for example "X 1.250 m", "Z 45.0°" or "Y 1.200×".</returns>
    internal static string FormatGizmoReadout(RuntimeGizmoEvent gizmoEvent)
    {
        ArgumentNullException.ThrowIfNull(gizmoEvent);
        var text = new StringBuilder();
        if (gizmoEvent.Duplicate)
        {
            _ = text.Append("Copy  ");
        }

        var values = gizmoEvent.ReadoutValues;
        switch (gizmoEvent.Tool)
        {
            case RuntimeTransformTool.Rotate:
                var axis = AxisNames(gizmoEvent.ReadoutAxes) is { Length: 1 } names ? names[0] : "View";
                _ = text.Append(CultureInfo.InvariantCulture, $"{axis} {values.X:0.0}°");
                break;
            case RuntimeTransformTool.Scale when gizmoEvent.Handle == RuntimeGizmoHandle.Center:
                _ = text.Append(CultureInfo.InvariantCulture, $"{values.X:0.000}×");
                break;
            case RuntimeTransformTool.Scale:
                AppendAxes(text, gizmoEvent.ReadoutAxes, values, "0.000", "×");
                break;
            default:
                AppendAxes(text, gizmoEvent.ReadoutAxes, values, "0.000", " m");
                break;
        }

        if (!gizmoEvent.Representable)
        {
            _ = text.Append("  ·  would shear under its parent");
        }

        return text.ToString();

        static void AppendAxes(StringBuilder text, int mask, Vector3 values, string format, string unit)
        {
            var first = true;
            for (var i = 0; i < 3; i++)
            {
                if ((mask & (1 << i)) == 0)
                {
                    continue;
                }

                if (!first)
                {
                    _ = text.Append("  ");
                }

                first = false;
                var value = i == 0 ? values.X : i == 1 ? values.Y : values.Z;
                _ = text.Append("XYZ"[i]).Append(' ').Append(value.ToString(format, CultureInfo.InvariantCulture)).Append(unit);
            }
        }

        static string[] AxisNames(int mask)
            => [.. Enumerable.Range(0, 3).Where(i => (mask & (1 << i)) != 0).Select(i => "XYZ"[i].ToString())];
    }

    /// <summary>Formats the value a helper handle drag applies, as the pointer readout shows it.</summary>
    /// <param name="handle">The dragged handle.</param>
    /// <param name="value">Metres for a range, radians for a cone angle.</param>
    /// <returns>The readout, for example "Range 5.00 m" or "Outer cone 30.0°".</returns>
    internal static string FormatHelperReadout(RuntimeHelperHandle handle, float value)
        => handle switch
        {
            RuntimeHelperHandle.InnerCone => string.Create(CultureInfo.InvariantCulture, $"Inner cone {float.RadiansToDegrees(value):0.0}°"),
            RuntimeHelperHandle.OuterCone => string.Create(CultureInfo.InvariantCulture, $"Outer cone {float.RadiansToDegrees(value):0.0}°"),
            _ => string.Create(CultureInfo.InvariantCulture, $"Range {value:0.00} m"),
        };

    /// <summary>The orthographic view a triad click selects: from the clicked axis, or from its opposite when the pane already looks from that axis.</summary>
    /// <param name="axis">The clicked axis.</param>
    /// <param name="current">The pane's current view.</param>
    /// <returns>The view to select, or <see langword="null"/> for no axis.</returns>
    internal static CameraType? AxisView(RuntimeGizmoHandle axis, CameraType current)
    {
        (CameraType Toward, CameraType Away)? views = axis switch
        {
            RuntimeGizmoHandle.X => (CameraType.Right, CameraType.Left),
            RuntimeGizmoHandle.Y => (CameraType.Front, CameraType.Back),
            RuntimeGizmoHandle.Z => (CameraType.Top, CameraType.Bottom),
            _ => null,
        };
        return views is { } pair ? (current == pair.Toward ? pair.Away : pair.Toward) : null;
    }

    /// <summary>Records whether the pointer is over a gizmo handle.</summary>
    /// <param name="hovering">Whether it is.</param>
    internal void SetGizmoHover(bool hovering) => this.gizmoHovered = hovering;

    /// <summary>Records whether the pointer is over a helper handle or the orientation triad.</summary>
    /// <param name="hovering">Whether it is.</param>
    internal void SetHelperHover(bool hovering) => this.helperHovered = hovering;

    /// <summary>Looks along an orientation triad axis the user clicked.</summary>
    /// <param name="axis">The clicked axis.</param>
    /// <returns>A task that completes when the view was requested.</returns>
    internal Task SelectAxisViewAsync(RuntimeGizmoHandle axis)
    {
        var current = this.SceneCamera is null ? this.CameraType : CameraType.Perspective;
        return AxisView(axis, current) is { } view ? this.ApplyOrthographicCameraPresetAsync(view) : Task.CompletedTask;
    }

    /// <summary>Starts showing a gizmo drag of this pane.</summary>
    internal void BeginGizmoDrag()
    {
        this.GizmoDragCount++;
        this.gizmoHovered = false;
        this.helperHovered = false;
        this.IsGizmoDragging = true;
        this.GizmoFeedback = null;
        this.MarkNavigated();
    }

    /// <summary>Shows a running helper handle drag's latest value.</summary>
    /// <param name="gizmoEvent">The drag's latest event.</param>
    internal void UpdateHelperDrag(RuntimeGizmoEvent gizmoEvent)
    {
        if (this.IsGizmoDragging)
        {
            this.GizmoFeedback = new GizmoFeedback(
                FormatHelperReadout(gizmoEvent.HelperHandle, gizmoEvent.Value),
                gizmoEvent.PointerPixel,
                PivotPixel: null,
                IsRejected: false);
        }
    }

    /// <summary>Shows a running drag's latest value.</summary>
    /// <param name="gizmoEvent">The drag's latest event.</param>
    internal void UpdateGizmoDrag(RuntimeGizmoEvent gizmoEvent)
    {
        if (this.IsGizmoDragging)
        {
            this.GizmoFeedback = new GizmoFeedback(
                FormatGizmoReadout(gizmoEvent),
                gizmoEvent.PointerPixel,
                gizmoEvent.PivotPixel,
                !gizmoEvent.Representable);
        }
    }

    /// <summary>Stops showing the drag.</summary>
    internal void EndGizmoDrag()
    {
        this.IsGizmoDragging = false;
        this.GizmoFeedback = null;
    }

    /// <summary>Cancels a running gizmo drag, as when the pane loses focus.</summary>
    /// <returns>A task that completes when the request has been submitted.</returns>
    internal async Task CancelGizmoDragAsync()
    {
        if (!this.IsGizmoDragging)
        {
            return;
        }

        try
        {
            _ = await this.EngineService.CancelTransformGizmoDragAsync().ConfigureAwait(true);
        }
        catch (Exception ex) when (ex is not OperationCanceledException)
        {
            // The drag ends with the engine; nothing to restore here.
            this.LogGizmoCancelFailed(ex);
        }
    }

    /// <summary>Runs an editing shortcut.</summary>
    /// <param name="command">The shortcut's edit.</param>
    /// <returns>A task that completes when the edit has run.</returns>
    internal Task RequestEditAsync(ViewportEditCommand command)
        => this.EditCommandRequested?.Invoke(command) ?? Task.CompletedTask;
}
