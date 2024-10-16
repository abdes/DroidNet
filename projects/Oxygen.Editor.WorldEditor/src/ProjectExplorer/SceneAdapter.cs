// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

using DroidNet.Controls;
using Oxygen.Editor.Projects;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="GameEntity" /> object to wrap as a <see cref="ITreeItem" />.</param>
/// <param name="projectManager">The configured project manager service.</param>
public partial class SceneAdapter(Scene scene, IProjectManagerService projectManager)
    : TreeItemAdapter, ITreeItem<Scene>
{
    public override bool IsRoot => false;

    public override string Label => scene.Name;

    public Scene AttachedObject => scene;

    protected override int GetChildrenCount() => scene.Entities.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        if (!await projectManager.LoadSceneEntitiesAsync(scene).ConfigureAwait(true))
        {
            return;
        }

        foreach (var entity in this.AttachedObject.Entities)
        {
            this.AddChildInternal(
                new GameEntityAdapter(entity)
                {
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
