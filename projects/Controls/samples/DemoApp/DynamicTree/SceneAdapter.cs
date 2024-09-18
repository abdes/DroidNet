// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;
using DroidNet.Controls.DynamicTree;

public class SceneAdapter(Scene item) : TreeItemAdapter, ITreeItem<Scene>
{
    public override bool IsRoot => false;

    public override string Label
        => this.Item.Name;

    public Scene Item => item;

    protected override async Task LoadChildren()
    {
        await ProjectLoaderService.LoadSceneAsync(this.Item).ConfigureAwait(false);

        foreach (var entity in this.Item.Entities)
        {
            this.AddChildInternal(
                new EntityAdapter(entity)
                {
                    Level = this.Level + 1,
                    IsExpanded = false,
                });
        }

        await Task.CompletedTask.ConfigureAwait(true);
    }
}
