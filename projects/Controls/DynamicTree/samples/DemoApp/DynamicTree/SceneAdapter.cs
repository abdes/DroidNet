// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;

public class SceneAdapter(Scene item) : TreeItemAdapter, ITreeItem<Scene>
{
    public override bool IsRoot => false;

    public override string Label
        => this.AttachedObject.Name;

    public Scene AttachedObject => item;

    protected override int GetChildrenCount() => this.AttachedObject.Entities.Count;

    protected override async Task LoadChildren()
    {
        await ProjectLoaderService.LoadSceneAsync(this.AttachedObject).ConfigureAwait(false);

        foreach (var entity in this.AttachedObject.Entities)
        {
            this.AddChildInternal(
                new EntityAdapter(entity)
                {
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
