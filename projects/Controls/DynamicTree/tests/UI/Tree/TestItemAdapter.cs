// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// A <see cref="DynamicTree" /> item adapter for testing purposes.
/// </summary>
internal sealed partial class TestItemAdapter(TestTreeItem node, bool isRoot = false, bool isHidden = false)
    : TreeItemAdapter(isRoot, isHidden)
{
    /// <inheritdoc/>
    public override string Label
    {
        get => node.Label;
        set
        {
            if (string.Equals(value, node.Label, StringComparison.Ordinal))
            {
                return;
            }

            node.Label = value;
            this.OnPropertyChanged();
        }
    }

    /// <inheritdoc/>
    public override bool ValidateItemName(string name) => !string.IsNullOrWhiteSpace(name);

    /// <inheritdoc/>
    protected override int DoGetChildrenCount() => node.Children.Count;

    /// <inheritdoc/>
    protected override async Task LoadChildren()
    {
        foreach (var childNode in node.Children)
        {
            this.AddChildInternal(new TestItemAdapter(childNode));
        }

        // Simulate async operation
        await Task.Delay(10).ConfigureAwait(false);
    }
}
