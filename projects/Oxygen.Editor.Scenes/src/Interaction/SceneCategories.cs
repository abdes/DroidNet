// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Interaction;

/// <summary>The Scene Explorer node categories to display.</summary>
/// <param name="ShowMeshes">Whether mesh-category rows are displayed.</param>
/// <param name="ShowLights">Whether light-category rows are displayed.</param>
/// <param name="ShowCameras">Whether camera-category rows are displayed.</param>
public sealed record SceneCategories(bool ShowMeshes, bool ShowLights, bool ShowCameras)
{
    /// <summary>Gets the filter that includes every category.</summary>
    public static SceneCategories All { get; } = new(ShowMeshes: true, ShowLights: true, ShowCameras: true);
}
