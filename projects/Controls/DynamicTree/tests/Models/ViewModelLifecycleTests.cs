// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

/// <summary>
/// View model lifecycle: selection mode transitions, <c>ClearRoot</c>, <c>Dispose</c>, installing another root,
/// the base inline-rename commit, a failing child load and focus when a focused row leaves the tree.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Lifecycle")]
public sealed class ViewModelLifecycleTests : ViewModelTestBase
{
    [TestMethod]
    public async Task SelectionMode_FromNoneToSingle_StartsWithAnEmptySelection()
    {
        var (viewModel, _) = await this.CreateAsync(SelectionMode.None).ConfigureAwait(false);
        using var disposable = viewModel;

        viewModel.SelectionMode = SelectionMode.Single;

        _ = viewModel.SelectedItemsCount.Should().Be(0);
        _ = viewModel.SelectedItem.Should().BeNull();
    }

    [TestMethod]
    public async Task SelectionMode_FromSingleToMultiple_KeepsTheSelectedItem()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.ClearAndSelectItem(tree.B);

        viewModel.SelectionMode = SelectionMode.Multiple;

        _ = viewModel.SelectedItemsCount.Should().Be(1);
        _ = viewModel.SelectedItem.Should().BeSameAs(tree.B);
        _ = tree.B.IsSelected.Should().BeTrue();
    }

    [TestMethod]
    public async Task SelectionMode_WhenItChanges_ForgetsTheActiveItem()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.SelectItem(tree.B);
        _ = viewModel.ActiveItem.Should().BeSameAs(tree.B);

        viewModel.SelectionMode = SelectionMode.Multiple;

        _ = viewModel.ActiveItem.Should().BeNull();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectionMode_ToNone_ClearsTheSelection(SelectionMode mode)
    {
        var (viewModel, tree) = await this.CreateAsync(mode).ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.ClearAndSelectItem(tree.B);

        viewModel.SelectionMode = SelectionMode.None;

        _ = viewModel.SelectedItemsCount.Should().Be(0);
        _ = viewModel.SelectedItem.Should().BeNull();
        _ = tree.B.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task SelectionCommands_WhenSelectionModeIsNone_DoNotSelect()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.None).ConfigureAwait(false);
        using var disposable = viewModel;

        viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.B));
        viewModel.SelectAllCommand.Execute(parameter: null);

        _ = viewModel.SelectedItemsCount.Should().Be(0);
        _ = viewModel.ActiveItem.Should().BeNull();
        _ = tree.A.IsSelected.Should().BeFalse();
        _ = tree.B.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task ClearRoot_ReleasesTheRowsTheSelectionAndTheFocus()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Multiple).ConfigureAwait(false);
        using var disposable = viewModel;
        _ = viewModel.FocusItem(tree.B, RequestOrigin.PointerInput);
        viewModel.SelectItem(tree.B);

        viewModel.ClearRoot();

        _ = viewModel.ShownItems.Should().BeEmpty();
        _ = viewModel.SelectedItemsCount.Should().Be(0);
        _ = viewModel.ActiveItem.Should().BeNull();
        _ = viewModel.TryGetFocusedItem(out _, out _).Should().BeFalse();
        _ = tree.B.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task ClearRoot_RetainsTheClipboard()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Multiple).ConfigureAwait(false);
        using var disposable = viewModel;
        await viewModel.CopyItemAsync(tree.B).ConfigureAwait(false);

        viewModel.ClearRoot();

        _ = viewModel.CurrentClipboardState.Should().Be(ClipboardState.Copied);
        _ = viewModel.ClipboardItems.Should().Contain(tree.B);
    }

    [TestMethod]
    public async Task Dispose_CalledTwice_DoesNotThrow()
    {
        var (viewModel, _) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);

        viewModel.Dispose();
        var secondDispose = viewModel.Dispose;

        _ = secondDispose.Should().NotThrow();
    }

    [TestMethod]
    public async Task Dispose_WithAnActiveFilter_StopsObservingTheItems()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        viewModel.FilterPredicate = static item => !string.IsNullOrEmpty(item.Label);
        _ = viewModel.FilteredItems.Should().NotBeEmpty();

        viewModel.Dispose();
        var rename = () => tree.B.Label = "Renamed";

        _ = rename.Should().NotThrow();
        _ = viewModel.ShownItems.Should().Contain(tree.B);
    }

    [TestMethod]
    public async Task InitializeRootAsync_WhenCalledAgain_DropsTheSelectionAndFocusOfThePreviousRoot()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Multiple).ConfigureAwait(false);
        using var disposable = viewModel;
        viewModel.SelectItem(tree.B);
        _ = viewModel.FocusItem(tree.B, RequestOrigin.PointerInput);
        var newFirst = new TestTreeItemAdapter { Label = "NewFirst" };
        var newRoot = new TestTreeItemAdapter([newFirst], isRoot: true) { Label = "NewRoot", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(newRoot).ConfigureAwait(false);

        _ = viewModel.ShownItems.Should().Equal(newFirst);
        _ = viewModel.SelectedItemsCount.Should().Be(0);
        _ = tree.B.IsSelected.Should().BeFalse();
        _ = viewModel.TryGetFocusedItem(out var focused, out var origin).Should().BeTrue();
        _ = focused.Should().BeSameAs(newFirst, "focus falls back to the first row of the new root");
        _ = origin.Should().Be(RequestOrigin.PointerInput, "initialization must not scroll the new focus into view");
    }

    [TestMethod]
    public async Task InitializeRootAsync_WhenARowOfTheNewRootIsSelected_SelectsAndFocusesIt()
    {
        var (viewModel, _) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;
        var first = new TestTreeItemAdapter { Label = "First" };
        var second = new TestTreeItemAdapter { Label = "Second", IsSelected = true };
        var newRoot = new TestTreeItemAdapter([first, second], isRoot: true) { Label = "NewRoot", IsExpanded = true };

        await viewModel.InitializeRootAsyncPublic(newRoot).ConfigureAwait(false);

        _ = viewModel.SelectedItem.Should().BeSameAs(second);
        _ = viewModel.TryGetFocusedItem(out var focused, out _).Should().BeTrue();
        _ = focused.Should().BeSameAs(second);
    }

    [TestMethod]
    public async Task CommitRenameAsync_WithPaddedName_StoresTheTrimmedName()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;

        var result = await viewModel.CommitRenameAsync(tree.B, "  Renamed  ").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = tree.B.Label.Should().Be("Renamed");
    }

    [TestMethod]
    public async Task CommitRenameAsync_WithTheCurrentName_Succeeds()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;

        var result = await viewModel.CommitRenameAsync(tree.B, tree.B.Label).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = tree.B.Label.Should().Be("B");
    }

    [TestMethod]
    [DataRow(TestTreeItemAdapter.InvalidName)]
    [DataRow("")]
    [DataRow("   ")]
    public async Task CommitRenameAsync_WithAnInvalidName_IsRejectedAndKeepsTheLabel(string name)
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;

        var result = await viewModel.CommitRenameAsync(tree.B, name).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = tree.B.Label.Should().Be("B");
    }

    [TestMethod]
    public async Task CommitRenameAsync_WhenTheItemIsNotShown_IsRejectedAndKeepsTheLabel()
    {
        var (viewModel, _) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;
        var detached = new TestTreeItemAdapter { Label = "Detached" };

        var result = await viewModel.CommitRenameAsync(detached, "Renamed").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = detached.Label.Should().Be("Detached");
    }

    [TestMethod]
    public async Task CommitRenameAsync_WithoutAName_ThrowsArgumentNull()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single).ConfigureAwait(false);
        using var disposable = viewModel;

        var rename = async () => await viewModel.CommitRenameAsync(tree.B, newName: null!).ConfigureAwait(false);

        _ = await rename.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task ExpandItemAsync_WhenLoadingChildrenFails_LeavesTheTreeUnchanged()
    {
        var (viewModel, thrower) = await CreateWithThrowingItemAsync(this.LoggerFactoryInstance).ConfigureAwait(false);
        using var disposable = viewModel;

        var expand = async () => await viewModel.ExpandItemAsync(thrower).ConfigureAwait(false);

        _ = await expand.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);
        _ = thrower.IsExpanded.Should().BeFalse();
        _ = viewModel.ShownItems.Should().HaveCount(2, "only the root and the item that failed to expand are shown");
    }

    [TestMethod]
    public async Task ExpandItemAsync_AfterALoadFailure_ShowsTheChildrenOnRetry()
    {
        var (viewModel, thrower) = await CreateWithThrowingItemAsync(this.LoggerFactoryInstance).ConfigureAwait(false);
        using var disposable = viewModel;
        var failing = async () => await viewModel.ExpandItemAsync(thrower).ConfigureAwait(false);
        _ = await failing.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(false);

        await viewModel.ExpandItemAsync(thrower).ConfigureAwait(false);

        _ = thrower.IsExpanded.Should().BeTrue();
        _ = viewModel.ShownItems.Should().HaveCount(4, "the root, the item and its two children, each once");
    }

    [TestMethod]
    public async Task RemoveItemAsync_WhenTheFocusedRowIsRemoved_FocusDoesNotRestOnARemovedRow()
    {
        var (viewModel, tree) = await this.CreateAsync(SelectionMode.Single, expandA: true).ConfigureAwait(false);
        using var disposable = viewModel;
        _ = viewModel.FocusItem(tree.A1, RequestOrigin.KeyboardInput);

        await viewModel.RemoveItemAsyncPublic(tree.A1).ConfigureAwait(false);

        var hasFocus = viewModel.TryGetFocusedItem(out var focused, out _);
        _ = (!hasFocus || viewModel.ShownItems.Contains(focused!)).Should().BeTrue("a removed row must not keep the logical focus");
    }

    private static async Task<(TestViewModel ViewModel, ControlledLoadItem Thrower)> CreateWithThrowingItemAsync(ILoggerFactory loggerFactory)
    {
        var viewModel = new TestViewModel(skipRoot: false, loggerFactory) { SelectionMode = SelectionMode.Single };
        var root = new TestTreeItemAdapter(isRoot: true) { Label = "Root", IsExpanded = true };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        var thrower = new ControlledLoadItem("Thrower", childCount: 2) { FailFirstLoad = true };
        await viewModel.InsertItemAsyncPublic(thrower, root, 0).ConfigureAwait(false);
        return (viewModel, thrower);
    }

    private async Task<(TestViewModel ViewModel, Fixture Tree)> CreateAsync(SelectionMode mode, bool expandA = false)
    {
        var a1 = new TestTreeItemAdapter { Label = "A1" };
        var a = new TestTreeItemAdapter([a1]) { Label = "A", IsExpanded = expandA };
        var b = new TestTreeItemAdapter { Label = "B" };
        var root = new TestTreeItemAdapter([a, b], isRoot: true) { Label = "Root", IsExpanded = true };
        var viewModel = new TestViewModel(skipRoot: true, this.LoggerFactoryInstance) { SelectionMode = mode };
        await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
        return (viewModel, new Fixture(a, a1, b));
    }

    private sealed record Fixture(TestTreeItemAdapter A, TestTreeItemAdapter A1, TestTreeItemAdapter B);
}
