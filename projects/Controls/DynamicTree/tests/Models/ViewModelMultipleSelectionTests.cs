// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using Moq;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection")]
public sealed partial class ViewModelMultipleSelectionTests : ViewModelTestBase, IDisposable
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Multiple };
    private TestTreeItemAdapter root = null!;
    private TestTreeItemAdapter[] items = [];

    private MultipleSelectionModel<ITreeItem> Selection => (MultipleSelectionModel<ITreeItem>)this.viewModel.GetSelectionModel()!;

    public void Dispose() => this.viewModel.Dispose();

    [TestMethod]
    public async Task SelectItemCommand_WithCtrl_AddsItemToSelection()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[1], IsCtrlKeyDown: true));

        _ = this.Selection.SelectedItem.Should().Be(this.items[1]);
        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([1, 2]);
        _ = this.viewModel.ActiveItem.Should().Be(this.items[1]);
    }

    [TestMethod]
    public async Task SelectItemCommand_WithoutCtrl_ReplacesSelection()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);
        this.viewModel.SelectAllCommand.Execute(parameter: null);

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));

        _ = this.Selection.SelectedItem.Should().Be(this.items[0]);
        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([1]);
    }

    [TestMethod]
    public async Task SelectItemCommand_CtrlOnSelectedItem_DeselectsItAndKeepsTheRest()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[1], IsCtrlKeyDown: true));

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0], IsCtrlKeyDown: true));

        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([2]);
        _ = this.Selection.SelectedItem.Should().Be(this.items[1]);
        _ = this.viewModel.ActiveItem.Should().Be(this.items[1]);
    }

    [TestMethod]
    public async Task SelectItemCommand_RaisesPropertyAndCollectionNotifications()
    {
        await this.InitializeAsync(1).ConfigureAwait(false);
        var propertyChanged = new Mock<PropertyChangedEventHandler>();
        var collectionChanged = new Mock<NotifyCollectionChangedEventHandler>();
        this.Selection.PropertyChanged += propertyChanged.Object;
        ((INotifyCollectionChanged)this.Selection.SelectedIndices).CollectionChanged += collectionChanged.Object;

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));

        VerifyPropertyChange(propertyChanged, nameof(this.Selection.SelectedItem));
        VerifyPropertyChange(propertyChanged, nameof(this.Selection.SelectedIndex));
        collectionChanged.Verify(
            handler => handler(
                It.IsAny<object>(),
                It.Is<NotifyCollectionChangedEventArgs>(e => e.Action == NotifyCollectionChangedAction.Add && e.NewItems!.Cast<int>().Contains(1))),
            Times.Once);
    }

    [TestMethod]
    [DataRow(4, 2, new[] { 2, 3, 4 }, DisplayName = "Shift range up")]
    [DataRow(1, 5, new[] { 1, 2, 3, 4, 5 }, DisplayName = "Shift range down")]
    public async Task SelectItemCommand_WithShift_SelectsTheRangeFromTheSelectedItem(int anchor, int target, int[] expected)
    {
        await this.InitializeAsync(5).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[anchor - 1]));

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[target - 1], IsShiftKeyDown: true));

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo(expected.Select(n => this.items[n - 1]));
        _ = this.Selection.SelectedItem.Should().Be(this.items[target - 1]);
        _ = this.viewModel.ActiveItem.Should().Be(this.items[target - 1]);
    }

    [TestMethod]
    public async Task SelectItemCommand_WithShift_AfterTheSelectionWasReplacedBehindTheAnchor_StartsFromTheSelectedItem()
    {
        await this.InitializeAsync(3).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));
        this.viewModel.ClearAndSelectItem(this.items[1]);

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[2], IsShiftKeyDown: true));

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo([this.items[1], this.items[2]]);
        _ = this.Selection.SelectedItem.Should().Be(this.items[2]);
    }

    [TestMethod]
    public async Task ToggleSelectAllCommand_WhenSomeItemsAreNotSelected_SelectsEveryItem()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[1]));

        this.viewModel.ToggleSelectAllCommand.Execute(parameter: null);

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo([this.root, this.items[0], this.items[1]]);
        _ = this.Selection.SelectedItem.Should().Be(this.items[1]);
    }

    [TestMethod]
    public async Task ToggleSelectAllCommand_WhenEveryItemIsSelected_ClearsSelection()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);
        this.viewModel.ToggleSelectAllCommand.Execute(parameter: null);

        this.viewModel.ToggleSelectAllCommand.Execute(parameter: null);

        _ = this.Selection.IsEmpty.Should().BeTrue();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        _ = this.viewModel.ActiveItem.Should().BeNull();
    }

    [TestMethod]
    public async Task SelectAllCommand_SelectsEveryShownItem()
    {
        await this.InitializeAsync(2).ConfigureAwait(false);

        this.viewModel.SelectAllCommand.Execute(parameter: null);

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo([this.root, this.items[0], this.items[1]]);
        _ = this.Selection.SelectedItem.Should().Be(this.items[1]);
    }

    [TestMethod]
    public async Task InvertSelectionCommand_SelectsTheUnselectedItemsOnly()
    {
        await this.InitializeAsync(3).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[1]));

        this.viewModel.InvertSelectionCommand.Execute(parameter: null);

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo([this.root, this.items[0], this.items[2]]);
        _ = this.Selection.SelectedItem.Should().Be(this.items[2]);
    }

    [TestMethod]
    public async Task InvertDisplayedSelection_InvertsOnlyTheDisplayedItems()
    {
        await this.InitializeAsync(3).ConfigureAwait(false);
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(this.items[0]));

        this.viewModel.InvertDisplayedSelection([this.items[0], this.items[1]]);

        _ = this.Selection.SelectedItems.Should().BeEquivalentTo([this.items[1]]);
    }

    [TestMethod]
    public async Task ToggleDisplayedSelection_SelectsDisplayedItemsThenClearsThem()
    {
        await this.InitializeAsync(3).ConfigureAwait(false);
        ITreeItem[] displayed = [this.items[0], this.items[1]];

        this.viewModel.ToggleDisplayedSelection(displayed);
        _ = this.Selection.SelectedItems.Should().BeEquivalentTo(displayed);

        this.viewModel.ToggleDisplayedSelection(displayed);
        _ = this.Selection.IsEmpty.Should().BeTrue();
    }

    private static void VerifyPropertyChange(Mock<PropertyChangedEventHandler> handler, string propertyName)
        => handler.Verify(
            h => h(It.IsAny<object>(), It.Is<PropertyChangedEventArgs>(e => string.Equals(e.PropertyName, propertyName, StringComparison.Ordinal))),
            Times.Once);

    private async Task InitializeAsync(int childCount)
    {
        this.items = [.. Enumerable.Range(1, childCount).Select(n => new TestTreeItemAdapter { Label = $"Item{n}" })];
        this.root = new TestTreeItemAdapter(this.items, isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(this.root).ConfigureAwait(false);
    }
}
