// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Editor view configuration; omitted optional values retain native defaults.</summary>
public sealed record RuntimeViewConfig
{
    /// <summary>Gets the display name.</summary>
    public string Name { get; init; } = string.Empty;

    /// <summary>Gets the view purpose.</summary>
    public string Purpose { get; init; } = string.Empty;

    /// <summary>Gets the target composition surface identity, if any.</summary>
    public Guid? CompositingTarget { get; init; }

    /// <summary>Gets the initial width in pixels, when supplied.</summary>
    public uint? Width { get; init; }

    /// <summary>Gets the initial height in pixels, when supplied.</summary>
    public uint? Height { get; init; }

    /// <summary>Gets the initial clear color, when supplied.</summary>
    public RuntimeColor? ClearColor { get; init; }

    /// <summary>Gets the preset the editor camera starts with.</summary>
    public CameraViewPreset CameraPreset { get; init; } = CameraViewPreset.Perspective;

    /// <summary>
    /// Gets the editor camera state to start from, so a recreated or restored pane is correct on its
    /// first presented frame; <see langword="null"/> frames the scene.
    /// </summary>
    public RuntimeEditorCamera? EditorCamera { get; init; }

    /// <summary>Gets the authored camera node the view looks through from its first frame, if any.</summary>
    public Guid? SceneCameraNodeId { get; init; }

    /// <summary>
    /// Gets the view a camera preview inset is composed over, or <see langword="null"/> for a view
    /// that presents to its own surface. An inset names its host's surface as
    /// <see cref="CompositingTarget"/> and receives no input.
    /// </summary>
    public RuntimeViewId? InsetHost { get; init; }

    /// <summary>Gets what the view renders from its first frame.</summary>
    public ViewportViewMode ViewMode { get; init; } = ViewportViewMode.Lit;

    /// <summary>Gets a value indicating whether the view shows the ground grid from its first frame.</summary>
    public bool ShowGrid { get; init; } = true;

    /// <summary>Gets a value indicating whether the view outlines the selected nodes from its first frame.</summary>
    public bool ShowSelectionOutline { get; init; } = true;

    /// <summary>Gets a value indicating whether the view shows light and camera icons from its first frame.</summary>
    public bool ShowIcons { get; init; } = true;
}
