// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using Moq;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection")]
public sealed partial class ViewModelSingleSelectionTests : ViewModelTestBase, IDisposable
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Single };

    private SelectionModel<ITreeItem> Selection => this.viewModel.GetSelectionModel()!;

    public void Dispose() => this.viewModel.Dispose();

    [TestMethod]
    public async Task SelectDisplayedItem_ReplacesPreviousSelection()
    {
        var (item1, item2) = await this.InitializeAsync().ConfigureAwait(false);

        this.SelectDisplayed(item1);
        this.SelectDisplayed(item2);

        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
        _ = item1.IsSelected.Should().BeFalse();
        _ = this.viewModel.ActiveItem.Should().Be(item2);
    }

    [TestMethod]
    public async Task SelectDisplayedItem_WithModifiers_StillSelectsOnlyTheItem()
    {
        var (item1, item2) = await this.InitializeAsync().ConfigureAwait(false);
        this.SelectDisplayed(item1);

        this.SelectDisplayed(item2, isControlDown: true, isShiftDown: true);

        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.viewModel.SelectedItemsCount.Should().Be(1);
    }

    [TestMethod]
    public async Task SelectItemCommand_ReplacesPreviousSelection()
    {
        var (item1, item2) = await this.InitializeAsync().ConfigureAwait(false);

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(item1));
        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(item2));

        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = item1.IsSelected.Should().BeFalse();
    }

    [TestMethod]
    public async Task SelectItemCommand_RaisesSelectedItemAndSelectedIndexOnce()
    {
        var (item1, _) = await this.InitializeAsync().ConfigureAwait(false);
        var changed = new List<string?>();
        this.Selection.PropertyChanged += (_, args) => changed.Add(args.PropertyName);

        this.viewModel.SelectItemCommand.Execute(new ItemSelectionArgs(item1));

        _ = changed.Count(name => string.Equals(name, nameof(this.Selection.SelectedItem), StringComparison.Ordinal)).Should().Be(1);
        _ = changed.Count(name => string.Equals(name, nameof(this.Selection.SelectedIndex), StringComparison.Ordinal)).Should().Be(1);
    }

    [TestMethod]
    [DataRow(nameof(DynamicTreeViewModel.ToggleSelectAllCommand))]
    [DataRow(nameof(DynamicTreeViewModel.SelectAllCommand))]
    [DataRow(nameof(DynamicTreeViewModel.InvertSelectionCommand))]
    public async Task BulkCommands_AreNoOpsInSingleSelectionMode(string command)
    {
        _ = await this.InitializeAsync().ConfigureAwait(false);
        var handler = new Mock<PropertyChangedEventHandler>();
        this.Selection.PropertyChanged += handler.Object;

        switch (command)
        {
            case nameof(DynamicTreeViewModel.ToggleSelectAllCommand):
                this.viewModel.ToggleSelectAllCommand.Execute(parameter: null);
                break;
            case nameof(DynamicTreeViewModel.SelectAllCommand):
                this.viewModel.SelectAllCommand.Execute(parameter: null);
                break;
            default:
                this.viewModel.InvertSelectionCommand.Execute(parameter: null);
                break;
        }

        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        handler.Verify(h => h(It.IsAny<object>(), It.IsAny<PropertyChangedEventArgs>()), Times.Never);
    }

    private async Task<(TestTreeItemAdapter Item1, TestTreeItemAdapter Item2)> InitializeAsync()
    {
        var item1 = new TestTreeItemAdapter { Label = "Item1" };
        var item2 = new TestTreeItemAdapter { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        return (item1, item2);
    }

    private void SelectDisplayed(ITreeItem item, bool isControlDown = false, bool isShiftDown = false)
        => this.viewModel.SelectDisplayedItem(item, [.. this.viewModel.ShownItems], isControlDown, isShiftDown);
}
