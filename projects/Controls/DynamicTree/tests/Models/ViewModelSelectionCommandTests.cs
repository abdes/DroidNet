// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace DroidNet.Controls.Tests;

/// <summary>
/// Selection behaviour shared by the single and multiple selection modes, driven through the
/// view model's production entry points.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / Selection")]
public sealed class ViewModelSelectionCommandTests : ViewModelTestBase
{
    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectItemCommand_WhenItemNotShown_DoesNothing(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode, expanded: false).ConfigureAwait(false);

        tree.ViewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.Items[0]));

        _ = tree.ViewModel.SelectedItem.Should().BeNull();
        _ = tree.ViewModel.ActiveItem.Should().BeNull();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectItemCommand_SelectsItemAndMakesItActive(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode).ConfigureAwait(false);

        tree.ViewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.Items[0]));

        _ = tree.ViewModel.SelectedItem.Should().Be(tree.Items[0]);
        _ = tree.ViewModel.GetSelectionModel()!.SelectedIndex.Should().Be(1);
        _ = tree.ViewModel.ActiveItem.Should().Be(tree.Items[0]);
        _ = tree.Items[0].IsSelected.Should().BeTrue();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectItemCommand_WhenTheActiveItemChanges_RaisesPropertyChanged(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode).ConfigureAwait(false);
        var raised = new List<string?>();
        ((INotifyPropertyChanged)tree.ViewModel).PropertyChanged += (_, args) => raised.Add(args.PropertyName);

        tree.ViewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.Items[0]));

        _ = raised.Should().Contain(nameof(DynamicTreeViewModel.ActiveItem));
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectDisplayedItem_WhenItemNotShown_DoesNothing(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode, expanded: false).ConfigureAwait(false);

        tree.ViewModel.SelectDisplayedItem(tree.Items[0], [tree.Root, tree.Items[0]], isControlDown: false, isShiftDown: false);

        _ = tree.ViewModel.SelectedItem.Should().BeNull();
        _ = tree.ViewModel.GetSelectionModel()!.SelectedIndex.Should().Be(-1);
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task ClearSelectionCommand_DeselectsItemAndForgetsActive(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode).ConfigureAwait(false);
        tree.ViewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.Items[0]));

        tree.ViewModel.ClearSelectionCommand.Execute(tree.Items[0]);

        _ = tree.ViewModel.SelectedItem.Should().BeNull();
        _ = tree.ViewModel.GetSelectionModel()!.SelectedIndex.Should().Be(-1);
        _ = tree.ViewModel.ActiveItem.Should().BeNull();
        _ = tree.Items[0].IsSelected.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectNoneCommand_ClearsSelectionAndActive(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode).ConfigureAwait(false);
        tree.ViewModel.SelectItemCommand.Execute(new ItemSelectionArgs(tree.Items[0]));

        tree.ViewModel.SelectNoneCommand.Execute(parameter: null);

        _ = tree.ViewModel.GetSelectionModel()!.IsEmpty.Should().BeTrue();
        _ = tree.ViewModel.ActiveItem.Should().BeNull();
        _ = tree.Items[0].IsSelected.Should().BeFalse();
    }

    [TestMethod]
    [DataRow(SelectionMode.Single)]
    [DataRow(SelectionMode.Multiple)]
    public async Task SelectNoneCommand_WhenSelectionIsEmpty_DoesNotRaiseSelectionSettled(SelectionMode mode)
    {
        using var tree = await TreeFixture.CreateAsync(mode).ConfigureAwait(false);
        var settles = 0;
        tree.ViewModel.SelectionSettled += (_, _) => settles++;

        tree.ViewModel.SelectNoneCommand.Execute(parameter: null);

        _ = settles.Should().Be(0);
    }

    /// <summary>A root with two expanded children, under a view model in the given selection mode.</summary>
    private sealed class TreeFixture : IDisposable
    {
        private TreeFixture(TestViewModel viewModel, TestTreeItemAdapter root, TestTreeItemAdapter[] items)
        {
            this.ViewModel = viewModel;
            this.Root = root;
            this.Items = items;
        }

        public TestViewModel ViewModel { get; }

        public TestTreeItemAdapter Root { get; }

        public TestTreeItemAdapter[] Items { get; }

        public static async Task<TreeFixture> CreateAsync(SelectionMode mode, bool expanded = true)
        {
            TestTreeItemAdapter[] items =
            [
                new() { Label = "Item1" },
                new() { Label = "Item2" },
            ];
            var root = new TestTreeItemAdapter(items, isRoot: true) { Label = "Root", IsExpanded = expanded };
            var viewModel = new TestViewModel(skipRoot: false) { SelectionMode = mode };
            await viewModel.InitializeRootAsyncPublic(root).ConfigureAwait(false);
            return new TreeFixture(viewModel, root, items);
        }

        public void Dispose() => this.ViewModel.Dispose();
    }
}
