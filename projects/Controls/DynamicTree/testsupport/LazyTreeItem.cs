// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;

namespace DroidNet.Controls.Tests;

internal sealed partial class LazyTreeItem(string label, int childCount, bool isRoot = false)
    : TreeItemAdapter(isRoot), INotifyPropertyChanged
{
    private string label = label;

    event PropertyChangedEventHandler? INotifyPropertyChanged.PropertyChanged
    {
        add
        {
            this.PropertyObservers++;
            this.PropertyChanged += value;
        }

        remove
        {
            this.PropertyObservers--;
            this.PropertyChanged -= value;
        }
    }

    public override string Label
    {
        get => this.label;
        set => this.SetProperty(ref this.label, value);
    }

    public int LoadCount { get; private set; }

    public int PropertyObservers { get; private set; }

    public override bool ValidateItemName(string name) => !string.IsNullOrWhiteSpace(name);

    protected override int DoGetChildrenCount() => childCount;

    protected override Task LoadChildren()
    {
        this.LoadCount++;
        for (var index = 0; index < childCount; index++)
        {
            this.AddChildInternal(new LazyTreeItem($"{this.Label}/{index:D4}", this.IsRoot ? 1 : 0));
        }

        return Task.CompletedTask;
    }
}
