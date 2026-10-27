// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Viewports;

/// <summary>The kept state of one viewport pane.</summary>
/// <param name="CameraType">The view preset.</param>
/// <param name="ControlMode">The editor camera control mode.</param>
/// <param name="EditorCamera">The editor camera, or <see langword="null"/> when the pane never had a view.</param>
/// <param name="SceneCameraId">The stable node ID of the scene camera the pane looks through, if any.</param>
/// <param name="ViewMode">What the pane renders: the lit scene or a diagnostic view.</param>
/// <param name="ShowGrid">Whether the pane draws the ground grid.</param>
/// <param name="ShowCameraPreview">Whether the pane previews the selected camera in an inset.</param>
/// <param name="ShowStatistics">Whether the pane shows the frame statistics readout.</param>
/// <param name="ShowSelectionOutline">Whether the pane outlines the selected nodes.</param>
/// <param name="ShowIcons">Whether the pane shows light and camera icons.</param>
/// <param name="MovementSpeed">The editor camera fly speed, in meters per second.</param>
/// <param name="FieldOfViewDegrees">The editor camera field of view, in degrees.</param>
/// <param name="NearViewPlane">The editor camera near view plane, in meters.</param>
/// <param name="FarViewPlane">The editor camera far view plane, in meters.</param>
public sealed record ViewportPaneState(
    CameraType CameraType,
    CameraControlMode ControlMode,
    ViewportCameraState? EditorCamera,
    NodeId? SceneCameraId,
    ViewportViewMode ViewMode = ViewportViewMode.Lit,
    bool ShowGrid = true,
    bool ShowCameraPreview = true,
    bool ShowStatistics = false,
    bool ShowSelectionOutline = true,
    bool ShowIcons = true,
    float MovementSpeed = ViewportViewModel.DefaultMovementSpeed,
    float FieldOfViewDegrees = ViewportViewModel.DefaultFieldOfViewDegrees,
    float NearViewPlane = ViewportViewModel.DefaultNearViewPlane,
    float FarViewPlane = ViewportViewModel.DefaultFarViewPlane);
