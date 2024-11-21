// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.Demo.Model;
using DryIoc.ImTools;

namespace DroidNet.Controls.Demo.Services;

/// <summary>
/// Provides methods to load project and scene data.
/// </summary>
internal static class ProjectLoaderService
{
    private static readonly Scene[] Data =
    [
        new("Scene 1")
        {
            Entities =
            [
                new Entity("Scene 1 - Entity 1"),
                new Entity("Scene 1 - Entity 2"),
                new Entity("Scene 1 - Entity 3"),
            ],
        },
        new("Scene 2"),
        new("Scene 3")
        {
            Entities =
            [
                new Entity("Scene 3 - Entity 1"),
                new Entity("Scene 3 - Entity 2"),
                new Entity("Scene 3 - Entity 3"),
                new Entity("Scene 3 - Entity 4"),
            ],
        },
    ];

    /// <summary>
    /// Loads the project data asynchronously.
    /// </summary>
    /// <param name="project">The project to load data into.</param>
    /// <returns>A <see cref="Task"/> representing the result of the asynchronous operation.</returns>
    public static async Task LoadProjectAsync(Project project)
    {
        foreach (var scene in Data)
        {
            project.Scenes.Add(new Scene(scene.Name));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    /// <summary>
    /// Loads the scene data asynchronously.
    /// </summary>
    /// <param name="scene">The scene to load data into.</param>
    /// <returns>A <see cref="Task"/> representing the result of the asynchronous operation.</returns>
    public static async Task LoadSceneAsync(Scene scene)
    {
        var sceneData = Data.FindFirst((sceneData) => sceneData.Name.Equals(scene.Name, StringComparison.Ordinal));

        if (sceneData is null)
        {
            return;
        }

        foreach (var entity in sceneData.Entities)
        {
            scene.Entities.Add(new Entity(entity.Name));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }
}
