// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;

namespace Oxygen.Editor.World.Serialization;

/// <summary>Immutable scene persistence data captured on the authoring thread before storage awaits.</summary>
/// <param name="SceneId">The scene identity.</param>
/// <param name="SceneName">The file name stem captured with the scene.</param>
/// <param name="ProjectLocation">The owning project directory.</param>
/// <param name="Json">The complete serialized authoring snapshot.</param>
public sealed record SceneSaveSnapshot(Guid SceneId, string SceneName, string ProjectLocation, string Json)
{
    /// <summary>Captures owned serialized data without retaining mutable model or layout collections.</summary>
    /// <param name="scene">The authoring scene.</param>
    /// <returns>The immutable persistence snapshot.</returns>
    public static SceneSaveSnapshot Capture(Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        var location = scene.Project.ProjectInfo.Location ?? throw new InvalidOperationException("The project location is unavailable.");
        return new SceneSaveSnapshot(scene.Id, scene.Name, location, JsonSerializer.Serialize(scene.Dehydrate(), SceneJsonContext.Default.SceneData));
    }
}
