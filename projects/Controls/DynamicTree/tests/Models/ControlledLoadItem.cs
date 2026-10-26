// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;

namespace DroidNet.Controls.Tests;

/// <summary>
/// An adapter whose children load can be held back with <see cref="Gate"/> and made to fail once with
/// <see cref="FailFirstLoad"/>; the failing load leaves a partial child behind, which the adapter must release.
/// </summary>
/// <param name="label">The label of the item.</param>
/// <param name="childCount">The number of children a successful load adds.</param>
[ExcludeFromCodeCoverage]
internal sealed partial class ControlledLoadItem(string label, int childCount) : TreeItemAdapter(isRoot: false)
{
    private string label = label;

    public TaskCompletionSource? Gate { get; init; }

    public bool FailFirstLoad { get; init; }

    public int LoadCount { get; private set; }

    public override string Label
    {
        get => this.label;
        set => this.SetProperty(ref this.label, value);
    }

    public override bool ValidateItemName(string name) => !string.IsNullOrWhiteSpace(name);

    protected override int DoGetChildrenCount() => childCount;

    protected override async Task LoadChildren()
    {
        this.LoadCount++;
        if (this.Gate is { } gate)
        {
            await gate.Task.ConfigureAwait(true);
        }

        if (this.FailFirstLoad && this.LoadCount == 1)
        {
            this.AddChildInternal(new TestTreeItemAdapter { Label = "Partial" });
            throw new InvalidOperationException("load failed");
        }

        for (var index = 0; index < childCount; index++)
        {
            this.AddChildInternal(new TestTreeItemAdapter { Label = string.Create(CultureInfo.InvariantCulture, $"{this.Label}/{index}") });
        }
    }
}
