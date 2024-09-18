// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

internal static class ProjectLoaderService
{
    public static async Task<Project> LoadProjectAsync(string name)
    {
        var project = new Project(name);

        project.Scenes.Add(
            new Scene("Scene 1")
            {
                Entities =
                [
                    new Entity("Entity 1"),
                    new Entity("Entity 2"),
                ],
            });
        project.Scenes.Add(new Scene("Scene 2"));
        project.Scenes.Add(new Scene("Scene 3"));
        project.Scenes.Add(new Scene("Scene 4"));

        await Task.CompletedTask.ConfigureAwait(false);
        return project;
    }
}
