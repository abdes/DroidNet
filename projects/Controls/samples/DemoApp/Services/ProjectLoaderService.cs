// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Services;

using System.Globalization;
using DroidNet.Controls.Demo.Model;

internal static class ProjectLoaderService
{
    public static async Task LoadProjectAsync(Project project)
    {
        var random = new Random();
        var numScenes = random.Next(2, 10);
        for (var index = 1; index <= numScenes; index++)
        {
            project.Scenes.Add(new Scene($"Scene {index.ToString(CultureInfo.InvariantCulture)}"));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }

    public static async Task LoadSceneAsync(Scene scene)
    {
        var random = new Random();
        var numEnities = random.Next(1, 11);

        for (var index = 1; index < numEnities; index++)
        {
            scene.Entities.Add(new Entity($"{scene.Name} - Entity {index.ToString(CultureInfo.InvariantCulture)}"));
        }

        await Task.CompletedTask.ConfigureAwait(false);
    }
}
