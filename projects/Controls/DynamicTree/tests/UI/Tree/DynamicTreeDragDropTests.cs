// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Tests;
using Windows.ApplicationModel.DataTransfer;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests.Tree;

/// <summary>
/// The tree's internal drag and drop, driven through the staged-drag entry points (<c>BeginDrag</c> and
/// <c>CompleteDragAsync</c>): a drop zone maps to the right destination, Control asks for a copy, an invalid or
/// unstaged drop is refused without touching the tree, and every drop ends the staged drag.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DynamicTree")]
[TestCategory("UITest")]
public sealed partial class DynamicTreeDragDropTests : VisualUserInterfaceTests, IDisposable
{
    private TestableDynamicTree? tree;
    private TestViewModel? viewModel;

    public void Dispose() => this.viewModel?.Dispose();

    [TestMethod]
    public Task CompleteDragAsync_Inside_MovesTheItemAsTheLastChildOfTheTarget_Async() => EnqueueAsync(async () =>
    {
        var (_, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Move);
        _ = children[2].Parent.Should().BeSameAs(children[0]);
        _ = (await ChildLabelsAsync(children[0]).ConfigureAwait(true)).Should().Equal("R-C1-GC1", "R-C1-GC2", "R-C3");
    });

    [TestMethod]
    public Task CompleteDragAsync_Before_MovesTheItemAheadOfTheTarget_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Before).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Move);
        _ = (await ChildLabelsAsync(root).ConfigureAwait(true)).Should().Equal("R-C3", "R-C1", "R-C2");
    });

    [TestMethod]
    public Task CompleteDragAsync_After_MovesTheItemBehindTheTarget_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[0], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[1], DynamicTree.DropZone.After).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Move);
        _ = (await ChildLabelsAsync(root).ConfigureAwait(true)).Should().Equal("R-C2", "R-C1", "R-C3");
    });

    [TestMethod]
    public Task CompleteDragAsync_WithTheSelectionStaged_MovesEverySelectedItem_Async() => EnqueueAsync(async () =>
    {
        var (_, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        this.viewModel!.ClearAndSelectItem(children[1]);
        this.viewModel.SelectItem(children[2], RequestOrigin.PointerInput);
        _ = this.tree!.BeginDrag(children[1], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Move);
        _ = children[1].Parent.Should().BeSameAs(children[0]);
        _ = children[2].Parent.Should().BeSameAs(children[0]);
    });

    [TestMethod]
    public Task CompleteDragAsync_WhenControlIsDown_CopiesInsteadOfMoving_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside, isControlDown: true).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Copy);
        _ = children[2].Parent.Should().BeSameAs(root, "a copy leaves the source where it was");
        _ = (await root.Children.ConfigureAwait(true)).Should().HaveCount(3);
        _ = (await children[0].Children.ConfigureAwait(true)).Should().HaveCount(3);
    });

    [TestMethod]
    public Task CompleteDragAsync_WhenTheDragStartedAsACopy_CopiesWithoutControl_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: true).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.Copy);
        _ = (await root.Children.ConfigureAwait(true)).Should().HaveCount(3);
    });

    [TestMethod]
    public Task CompleteDragAsync_WhenTheDropSucceeds_FocusesTheDroppedItemFromThePointer_Async() => EnqueueAsync(async () =>
    {
        var (_, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();

        _ = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = this.viewModel!.FocusedItem.Should().NotBeNull();
        _ = this.viewModel.FocusedItem!.Item.Should().BeSameAs(children[2]);
        _ = this.viewModel.FocusedItem.Origin.Should().Be(RequestOrigin.PointerInput);
    });

    [TestMethod]
    public Task CompleteDragAsync_WithoutAStagedDrag_IsRejected_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);

        var move = await this.tree!.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);
        var copy = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside, isControlDown: true).ConfigureAwait(true);

        _ = move.Should().Be(DataPackageOperation.None);
        _ = copy.Should().Be(DataPackageOperation.None);
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(children);
    });

    [TestMethod]
    public Task CompleteDragAsync_OntoTheDraggedItem_IsRejectedAndLeavesTheTreeUnchanged_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[0], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.None);
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(children);
        _ = children[0].Parent.Should().BeSameAs(root);
    });

    [TestMethod]
    public Task CompleteDragAsync_OntoADescendantOfTheDraggedItem_IsRejected_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        await this.viewModel!.ExpandItemAsync(children[0]).ConfigureAwait(true);
        var descendant = (await children[0].Children.ConfigureAwait(true))[0];
        _ = this.tree!.BeginDrag(children[0], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(descendant, DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.None);
        _ = children[0].Parent.Should().BeSameAs(root);
    });

    [TestMethod]
    public Task CompleteDragAsync_BeforeTheRoot_IsRejected_Async() => EnqueueAsync(() => this.AssertDropBesideRootRejectedAsync(DynamicTree.DropZone.Before));

    [TestMethod]
    public Task CompleteDragAsync_AfterTheRoot_IsRejected_Async() => EnqueueAsync(() => this.AssertDropBesideRootRejectedAsync(DynamicTree.DropZone.After));

    [TestMethod]
    public Task BeginDrag_WhenTheItemIsLocked_StagesNothing_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        children[2].IsLocked = true;

        var staged = this.tree!.BeginDrag(children[2], isCopy: false);
        var operation = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = staged.Should().BeFalse();
        _ = operation.Should().Be(DataPackageOperation.None);
        _ = children[2].Parent.Should().BeSameAs(root);
    });

    [TestMethod]
    public Task BeginDrag_WhenTheItemIsNotShown_StagesNothing_Async() => EnqueueAsync(async () =>
    {
        _ = await this.ExpandRootAsync().ConfigureAwait(true);
        var detached = new TestItemAdapter(new TestTreeItem { Label = "Detached" });

        var staged = this.tree!.BeginDrag(detached, isCopy: false);

        _ = staged.Should().BeFalse();
    });

    [TestMethod]
    public Task CompleteDragAsync_AfterADrop_HasNoStagedDragLeft_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();
        _ = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Before).ConfigureAwait(true);
        var afterFirstDrop = await ChildLabelsAsync(root).ConfigureAwait(true);

        var second = await this.tree.CompleteDragAsync(children[1], DynamicTree.DropZone.Before).ConfigureAwait(true);

        _ = second.Should().Be(DataPackageOperation.None);
        _ = (await ChildLabelsAsync(root).ConfigureAwait(true)).Should().Equal(afterFirstDrop);
    });

    [TestMethod]
    public Task CompleteDragAsync_AfterARejectedDrop_HasNoStagedDragLeft_Async() => EnqueueAsync(async () =>
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[2], isCopy: false).Should().BeTrue();
        _ = await this.tree.CompleteDragAsync(children[2], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        var retry = await this.tree.CompleteDragAsync(children[0], DynamicTree.DropZone.Inside).ConfigureAwait(true);

        _ = retry.Should().Be(DataPackageOperation.None);
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(children);
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(false);

        var taskCompletionSource = new TaskCompletionSource();
        _ = EnqueueAsync(
            async () =>
            {
                this.viewModel = new TestViewModel(this.LoggerFactory);
                await this.viewModel.LoadTreeStructureAsync().ConfigureAwait(true);
                this.viewModel.SelectionMode = SelectionMode.Multiple;
                this.tree = new TestableDynamicTree { ViewModel = this.viewModel };
                await LoadTestContentAsync(this.tree).ConfigureAwait(true);

                taskCompletionSource.SetResult();
            });
        await taskCompletionSource.Task.ConfigureAwait(true);
    }

    private static async Task<string[]> ChildLabelsAsync(ITreeItem parent)
        => [.. (await parent.Children.ConfigureAwait(true)).Select(static child => child.Label)];

    private async Task AssertDropBesideRootRejectedAsync(DynamicTree.DropZone zone)
    {
        var (root, children) = await this.ExpandRootAsync().ConfigureAwait(true);
        _ = this.tree!.BeginDrag(children[0], isCopy: false).Should().BeTrue();

        var operation = await this.tree.CompleteDragAsync(root, zone).ConfigureAwait(true);

        _ = operation.Should().Be(DataPackageOperation.None);
        _ = (await root.Children.ConfigureAwait(true)).Should().Equal(children);
    }

    private async Task<(ITreeItem Root, ITreeItem[] Children)> ExpandRootAsync()
    {
        var root = this.viewModel!.ShownItems.First();
        await this.viewModel.ExpandItemAsync(root).ConfigureAwait(true);
        return (root, [.. await root.Children.ConfigureAwait(true)]);
    }
}
