// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>How a viewport presents the scene. Viewport state; never authored data.</summary>
/// <param name="ViewMode">What the view renders.</param>
/// <param name="ShowGrid">Whether the view shows the ground grid.</param>
/// <param name="ShowSelectionOutline">Whether the view outlines the selected nodes.</param>
/// <param name="ShowIcons">Whether the view shows light and camera icons.</param>
public readonly record struct ViewportRenderOptions(
    ViewportViewMode ViewMode,
    bool ShowGrid,
    bool ShowSelectionOutline,
    bool ShowIcons = true);
