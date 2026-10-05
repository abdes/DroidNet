// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Tree.Model;

namespace DroidNet.Controls.Demo.Tree.Services;

/// <summary>
/// Provides the one opened scene used by the DynamicTree control demo.
/// </summary>
internal static class SceneLoaderService
{
    private static readonly Scene OpenedSceneTemplate = new("Scene 1")
    {
        Entities =
        [
            new("Key Light") { Light = new LightComponent() },
            new("Camera") { Camera = new CameraComponent(), IsLoaded = false },
            new("Environment")
            {
                Entities =
                [
                    new Entity("Ground") { Geometry = new GeometryComponent() },
                    new Entity("Props")
                    {
                        Entities =
                        [
                            new Entity("Crate") { Geometry = new GeometryComponent() },

                            // Seeded suppressed state: the pinned warning icon must survive hover.
                            new Entity("Marker") { IsVisible = false },
                        ],
                    },
                ],
            },
        ],
    };

    /// <summary>
    /// Loads the scene data asynchronously.
    /// </summary>
    /// <param name="scene">The scene to load data into.</param>
    /// <returns>A <see cref="Task"/> representing the result of the asynchronous operation.</returns>
    public static Task LoadSceneAsync(Scene scene)
    {
        foreach (var entity in OpenedSceneTemplate.Entities)
        {
            scene.Entities.Add(CloneWithChildren(entity));
        }

        return Task.CompletedTask;
    }

    private static Entity CloneWithChildren(Entity source)
    {
        var clone = source.CloneWithoutChildren();
        foreach (var child in source.Entities)
        {
            clone.Entities.Add(CloneWithChildren(child));
        }

        return clone;
    }
}
