// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;

namespace DroidNet.Controls.Tests;

/// <summary>
/// <see cref="DynamicTreeViewModel.CommitDropAsync"/> moves or duplicates the dragged roots, leaves the
/// clipboard alone, and routes every mutation through the overridable move and insert operations.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Drop")]
public sealed class ViewModelDropTests : ViewModelTestBase
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CommitDropAsync_Copy_DuplicatesSubtreeWithoutReplacingClipboard(bool cutClipboard)
    {
        var (root, source, destination, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.ExpandItemAsync(source).ConfigureAwait(false);
        if (cutClipboard)
        {
            await viewModel.CutItemsAsync([source]).ConfigureAwait(false);
        }
        else
        {
            await viewModel.CopyItemAsync(source).ConfigureAwait(false);
        }

        var clipboard = viewModel.ClipboardItems.ToArray();

        var result = await viewModel.CommitDropAsync(
            new TreeDropRequest([source], destination, destination.ChildrenCount, TreeDropOperation.Copy)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        var clone = result.Items.Should().ContainSingle().Which;
        _ = clone.Should().NotBeSameAs(source);
        _ = clone.Parent.Should().BeSameAs(destination);
        _ = (await clone.Children.ConfigureAwait(false)).Select(child => child.Label).Should().Equal("R-C1-GC1", "R-C1-GC2");
        _ = viewModel.CurrentClipboardState.Should().Be(cutClipboard ? ClipboardState.Cut : ClipboardState.Copied);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = source.IsCut.Should().Be(cutClipboard);
        _ = viewModel.ClipboardItems.Should().Equal(clipboard);
    }

    [TestMethod]
    public async Task CommitDropAsync_Copy_WhenSecondRootIsVetoed_LeavesTreeSelectionAndClipboardUnchanged()
    {
        var (root, first, second, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance) { SelectionMode = SelectionMode.Multiple };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        viewModel.ClearAndSelectItem(first);
        await viewModel.CutItemsAsync([first]).ConfigureAwait(false);
        var approvals = 0;
        viewModel.ItemBeingAdded += (_, args) => args.Proceed = ++approvals < 2;

        var result = await viewModel.CommitDropAsync(
            new TreeDropRequest([first, second], root, 3, TreeDropOperation.Copy)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = root.ChildrenCount.Should().Be(3);
        _ = viewModel.SelectedItem.Should().BeSameAs(first);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = first.IsCut.Should().BeTrue();
    }

    [TestMethod]
    public async Task CommitDropAsync_Move_IntoAnotherParent_ReparentsTheSameItem()
    {
        var (root, source, target, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);

        var result = await viewModel.CommitDropAsync(
            new TreeDropRequest([source], target, target.ChildrenCount, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Items.Should().Equal(source);
        _ = source.Parent.Should().BeSameAs(target);
        _ = (await target.Children.ConfigureAwait(false)).Select(child => child.Label).Should().Equal("R-C2-GC1", "R-C1");
        _ = (await root.Children.ConfigureAwait(false)).Should().NotContain(source);
    }

    [TestMethod]
    public async Task CommitDropAsync_Move_WhenSameParentMoveIsVetoed_ReportsRejectionWithoutInvalidatingClipboard()
    {
        var (root, first, _, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.CopyItemAsync(first).ConfigureAwait(false);
        viewModel.ItemBeingMoved += (_, args) => args.Proceed = false;

        var result = await viewModel.CommitDropAsync(
            new TreeDropRequest([first], root, 3, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = (await root.Children.ConfigureAwait(false))[0].Should().BeSameAs(first);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
    }

    [TestMethod]
    public async Task CommitDropAsync_Move_WithSourceAndItsDescendantAndRedirect_ReturnsActualRoots()
    {
        var (root, source, actualParent, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        await viewModel.ExpandItemAsync(source).ConfigureAwait(false);
        var descendant = (await source.Children.ConfigureAwait(false))[0];
        viewModel.ItemBeingMoved += (_, args) => args.NewParent = actualParent;

        var result = await viewModel.CommitDropAsync(
            new TreeDropRequest([source, descendant], root, 0, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Items.Should().Equal(source);
        _ = source.Parent.Should().BeSameAs(actualParent);
        _ = descendant.Parent.Should().BeSameAs(source);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CommitDropAsync_Move_GoesThroughOverriddenMoveItemsAsync(bool allowed)
    {
        var (root, source, _, _) = CreateTree();
        using var viewModel = new GuardedViewModel(this.LoggerFactoryInstance) { AllowMutation = allowed };
        await viewModel.LoadAsync(root).ConfigureAwait(false);

        var move = await viewModel.CommitDropAsync(
            new TreeDropRequest([source], root, 3, TreeDropOperation.Move)).ConfigureAwait(false);

        _ = move.Succeeded.Should().Be(allowed);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CommitDropAsync_Copy_GoesThroughOverriddenInsertItemAsync(bool allowed)
    {
        var (root, source, _, _) = CreateTree();
        using var viewModel = new GuardedViewModel(this.LoggerFactoryInstance) { AllowMutation = allowed };
        await viewModel.LoadAsync(root).ConfigureAwait(false);

        var copy = await viewModel.CommitDropAsync(
            new TreeDropRequest([source], root, 3, TreeDropOperation.Copy)).ConfigureAwait(false);

        _ = copy.Succeeded.Should().Be(allowed);
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task CommitDropAsync_Copy_WhenLateInsertionIsRejected_CompensatesDespiteDeleteVeto(bool throws)
    {
        var (root, first, second, _) = CreateTree();
        using var viewModel = new GuardedViewModel(this.LoggerFactoryInstance)
        {
            AllowMutation = true,
            RejectSecondInsertion = true,
            ThrowOnRejection = throws,
            SelectionMode = SelectionMode.Multiple,
        };
        await viewModel.LoadAsync(root).ConfigureAwait(false);
        var originals = (await root.Children.ConfigureAwait(false)).ToArray();
        viewModel.SelectOnly(first);
        await viewModel.CutItemsAsync([first]).ConfigureAwait(false);
        var consumerItems = new HashSet<ITreeItem>(originals);
        viewModel.ItemAdded += (_, args) => consumerItems.Add(args.TreeItem);
        viewModel.ItemRemoved += (_, args) => consumerItems.Remove(args.TreeItem);
        viewModel.ItemBeingRemoved += (_, args) => args.Proceed = false;
        var request = new TreeDropRequest([first, second], root, 3, TreeDropOperation.Copy);

        if (throws)
        {
            var act = () => viewModel.CommitDropAsync(request);
            _ = await act.Should().ThrowExactlyAsync<InvalidOperationException>().ConfigureAwait(false);
        }
        else
        {
            _ = (await viewModel.CommitDropAsync(request).ConfigureAwait(false)).Succeeded.Should().BeFalse();
        }

        _ = (await root.Children.ConfigureAwait(false)).Should().Equal(originals);
        _ = consumerItems.Should().BeEquivalentTo(originals);
        _ = viewModel.SelectedItem.Should().BeSameAs(first);
        _ = viewModel.IsClipboardValid.Should().BeTrue();
        _ = first.IsCut.Should().BeTrue();
    }

    [TestMethod]
    public async Task RemoveItemAsync_OfPastedCopy_DetachesItAndResetsItsDepth()
    {
        var (root, first, _, _) = CreateTree();
        using var viewModel = new TestViewModel(skipRoot: false, this.LoggerFactoryInstance);
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var originalChildren = (await root.Children.ConfigureAwait(false)).ToArray();
        await viewModel.CopyItemAsync(first).ConfigureAwait(false);
        await viewModel.PasteItemsAsync(root).ConfigureAwait(false);
        var copy = (await root.Children.ConfigureAwait(false)).Single(item => !originalChildren.Contains(item));

        await viewModel.RemoveItemAsync(copy).ConfigureAwait(false);

        _ = copy.Depth.Should().BeNegative();
        _ = copy.Parent.Should().BeNull();
        _ = (await root.Children.ConfigureAwait(false)).Should().Equal(originalChildren);
        _ = viewModel.ShownItems.Should().NotContain(copy);
    }

    /// <summary>
    /// Builds R with children R-C1 (R-C1-GC1, R-C1-GC2), R-C2 (R-C2-GC1) and R-C3.
    /// </summary>
    /// <returns>The root and its three children.</returns>
    private static (TestTreeItemAdapter Root, TestTreeItemAdapter First, TestTreeItemAdapter Second, TestTreeItemAdapter Third) CreateTree()
    {
        var first = new TestTreeItemAdapter(
            [
                new TestTreeItemAdapter { Label = "R-C1-GC1" },
                new TestTreeItemAdapter { Label = "R-C1-GC2" },
            ])
        {
            Label = "R-C1",
        };
        var second = new TestTreeItemAdapter([new TestTreeItemAdapter { Label = "R-C2-GC1" }]) { Label = "R-C2" };
        var third = new TestTreeItemAdapter { Label = "R-C3" };
        var root = new TestTreeItemAdapter([first, second, third], isRoot: true) { Label = "R", IsExpanded = true };
        return (root, first, second, third);
    }

    /// <summary>
    /// A view model that overrides the move and insert operations the way a domain-backed owner does.
    /// </summary>
    private sealed partial class GuardedViewModel(ILoggerFactory? loggerFactory) : DynamicTreeViewModel(loggerFactory)
    {
        private int insertCalls;

        public bool AllowMutation { get; init; }

        public bool RejectSecondInsertion { get; init; }

        public bool ThrowOnRejection { get; init; }

        public Task LoadAsync(ITreeItem root) => this.InitializeRootAsync(root, skipRoot: false);

        public void SelectOnly(ITreeItem item) => this.SelectionModel?.ClearAndSelectItem(item);

        public override Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
            => this.AllowMutation ? base.MoveItemsAsync(items, newParent, startIndex) : Task.CompletedTask;

        public override Task InsertItemAsync(ITreeItem item, ITreeItem parent, int relativeIndex)
        {
            this.insertCalls++;
            if (this.RejectSecondInsertion && this.insertCalls == 2)
            {
                return this.ThrowOnRejection ? throw new InvalidOperationException("Rejected by the owner") : Task.CompletedTask;
            }

            return this.AllowMutation ? base.InsertItemAsync(item, parent, relativeIndex) : Task.CompletedTask;
        }
    }
}
