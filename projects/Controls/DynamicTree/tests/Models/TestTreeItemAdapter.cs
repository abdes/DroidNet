// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Controls.Tests;

/// <summary>
/// A simple implementation of TreeItemAdapter for testing purposes.
/// </summary>
[ExcludeFromCodeCoverage]
public partial class TestTreeItemAdapter : TreeItemAdapter, ICanBeCloned
{
    public const string InvalidName = "__INVALID__";

    private readonly List<TestTreeItemAdapter> internalChildren = [];

    public TestTreeItemAdapter()
        : base(isRoot: false)
    {
    }

    public TestTreeItemAdapter(bool isRoot)
        : base(isRoot)
    {
    }

    public TestTreeItemAdapter(IEnumerable<TestTreeItemAdapter> children, bool isRoot = false)
        : base(isRoot)
    {
        this.internalChildren.AddRange(children);
    }

    public override required string Label { get; set; }

    public void AddChild(TestTreeItemAdapter child)
    {
        this.internalChildren.Add(child);
        this.AddChildAsync(child).GetAwaiter().GetResult();
    }

    public override bool ValidateItemName(string name) => !string.IsNullOrEmpty(name) && !string.Equals(name, InvalidName, StringComparison.Ordinal);

    public ITreeItem CloneSelf()
    {
        // Follow the contract used by production adapters: clones must be orphaned and must not contain children.
        // The clipboard/paste code is responsible for reparenting clones to reflect the original hierarchy.
        var clone = new TestTreeItemAdapter { Label = this.Label };
        this.CopyBasePropertiesTo(clone);
        return clone;
    }

    protected override int DoGetChildrenCount() => this.internalChildren.Count;

    protected override Task LoadChildren()
    {
        foreach (var child in this.internalChildren)
        {
            this.AddChildInternal(child);
        }

        return Task.CompletedTask;
    }
}
