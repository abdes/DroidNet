// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using System.Diagnostics;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;
using DroidNet.Hosting.Generators;
using Microsoft.Extensions.DependencyInjection;

/// <summary>
/// The ViewModel for the <see cref="ProjectLayoutView" /> view.
/// </summary>
[InjectAs(ServiceLifetime.Singleton)]
public partial class ProjectLayoutViewModel : DynamicTreeViewModel
{
    public ProjectLayoutViewModel() => this.ItemBeingRemoved += this.OnItemBeingRemoved;

    public Project? Project { get; private set; }

    private void OnItemBeingRemoved(object? sender, ItemBeingRemovedEventArgs args)
    {
        _ = sender; // unused

        Debug.Assert(args.TreeItem.Parent is not null, "any item in the tree should have a parent");
        switch (args.TreeItem)
        {
            case SceneAdapter sceneAdapter:
            {
                var scene = sceneAdapter.AttachedObject;
                var parentAdapter = sceneAdapter.Parent as ProjectAdapter;
                Debug.Assert(parentAdapter is not null, "the parent of a SceneAdpater must be a ProjectAdapter");
                var project = parentAdapter.AttachedObject;
                project.Scenes.Remove(scene);
                break;
            }

            case EntityAdapter entityAdapter:
            {
                var entity = entityAdapter.AttachedObject;
                var parentAdapter = entityAdapter.Parent as SceneAdapter;
                Debug.Assert(parentAdapter is not null, "the parent of a EntityAdapter must be a SceneAdapter");
                var scene = parentAdapter.AttachedObject;
                scene.Entities.Remove(entity);
                break;
            }

            default:
                // Do nothing
                break;
        }
    }

    [RelayCommand]
    private async Task LoadProjectAsync()
    {
        this.Project = new Project("Sample Project");
        await ProjectLoaderService.LoadProjectAsync(this.Project).ConfigureAwait(false);

        var root = new ProjectAdapter(this.Project);
        await this.InitializeRootAsync(root).ConfigureAwait(false);
    }
}
