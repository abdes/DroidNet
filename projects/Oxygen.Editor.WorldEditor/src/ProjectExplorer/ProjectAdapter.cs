// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

using DroidNet.Controls;
using Oxygen.Editor.Projects;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Project" /> model class.
/// </summary>
/// <param name="project">The <see cref="GameEntity" /> object to wrap as a <see cref="ITreeItem" />.</param>
/// <param name="projectManager">The configured project manager service.</param>
public partial class ProjectAdapter(Project project, IProjectManagerService projectManager)
    : TreeItemAdapter, ITreeItem<Project>
{
    public override string Label => project.ProjectInfo.Name;

    public Project AttachedObject => project;

    protected override int GetChildrenCount() => project.Scenes.Count;

    protected override async Task LoadChildren()
    {
        this.ClearChildren();

        if (!await projectManager.LoadProjectScenesAsync(project).ConfigureAwait(false))
        {
            return;
        }

        foreach (var scene in project.Scenes)
        {
            this.AddChildInternal(
                new SceneAdapter(scene, projectManager)
                {
                    IsExpanded = true,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
