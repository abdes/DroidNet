// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Controls;
using Oxygen.Editor.Core;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.WorldEditor.ProjectExplorer;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for the <see cref="GameEntity" /> model class.
/// </summary>
public partial class GameEntityAdapter : TreeItemAdapter, ITreeItem<GameEntity>
{
    /// <summary>
    /// Initializes a new instance of the <see cref="GameEntityAdapter"/> class.
    /// A <see cref="DynamicTree" /> item adapter for the <see cref="GameEntity" /> model class.
    /// </summary>
    /// <param name="gameEntity">The <see cref="GameEntity" /> object to wrap as a <see cref="ITreeItem" />.</param>
    public GameEntityAdapter(GameEntity gameEntity)
    {
        this.AttachedObject = gameEntity;
        this.AttachedObject.PropertyChanged += (_, args) =>
        {
            if (args.PropertyName?.Equals(nameof(GameEntity.Name), StringComparison.Ordinal) == true)
            {
                this.OnPropertyChanged(new PropertyChangedEventArgs(nameof(this.Label)));
            }
        };
    }

    /// <inheritdoc/>
    public override string Label
    {
        get => this.AttachedObject.Name;
        set
        {
            if (string.Equals(value, this.AttachedObject.Name, StringComparison.Ordinal))
            {
                return;
            }

            this.AttachedObject.Name = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc/>
    public GameEntity AttachedObject { get; }

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => InputValidation.IsValidFileName(name);

    /// <inheritdoc/>
    protected override int DoGetChildrenCount() => 0;

    /// <inheritdoc/>
    protected override async Task LoadChildren() => await Task.CompletedTask.ConfigureAwait(false);
}
