// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.DynamicTree;

using DroidNet.Controls.Demo.Model;
using DroidNet.Controls.Demo.Services;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="Scene" /> model class.
/// </summary>
/// <param name="scene">The <see cref="Entity" /> object to wrap as a <see cref="ITreeItem" />.</param>
public partial class SceneAdapter(Scene scene) : TreeItemAdapter(isRoot: false, isHidden: false), ITreeItem<Scene>
{
    private string label = scene.Name;

    public override string Label
    {
        get => this.label;
        set
        {
            if (string.Equals(value, this.label, StringComparison.Ordinal))
            {
                return;
            }

            this.label = value;
            this.OnPropertyChanged();
        }
    }

    public Scene AttachedObject => scene;

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
