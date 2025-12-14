// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Controls.Selection;
using Moq;
using RequestOrigin = DroidNet.Controls.DynamicTreeViewModel.RequestOrigin;

namespace DroidNet.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection")]
public class ViewModelMultipleSelectionTests : ViewModelTestBase
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Multiple };
    private readonly Mock<PropertyChangedEventHandler> propertyChangedHandlerMock;
    private readonly Mock<NotifyCollectionChangedEventHandler> collectionChangedHandlerMock;

    public ViewModelMultipleSelectionTests()
    {
        this.propertyChangedHandlerMock = new Mock<PropertyChangedEventHandler>();
        this.collectionChangedHandlerMock = new Mock<NotifyCollectionChangedEventHandler>();
        this.Selection.PropertyChanged += this.propertyChangedHandlerMock.Object;
        ((INotifyCollectionChanged)this.Selection.SelectedIndices).CollectionChanged += this.collectionChangedHandlerMock.Object;
    }

    public TestContext TestContext { get; set; }

    private MultipleSelectionModel<ITreeItem> Selection => (this.viewModel.GetSelectionModel()! as MultipleSelectionModel<ITreeItem>)!;

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectItem")]
    public async Task SelectItem_ShouldDoNothingIfItemNotShown()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        this.VerifySelectedItemPropertyChange(Times.Never);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifyNotifySelectionChangedNever();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectItem")]
    public async Task SelectItem_ShouldAddItemToSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);
        this.viewModel.SelectItem(item2, RequestOrigin.PointerInput);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
        _ = this.Selection.SelectedIndices.Should().Contain([1, 2]);
        _ = this.Selection.SelectedItems.Should().Contain([item1, item2]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectItem")]
    public async Task SelectItem_SelectsFirstOccurrenceOfItem()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item, item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item);
        _ = this.Selection.SelectedIndex.Should().Be(1);
        _ = this.Selection.SelectedItems.Should().ContainSingle().And.Contain([item]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectItem")]
    public async Task SelectItem_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifyAddedToSelection([1]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearSelection")]
    public async Task ClearSelection_ShouldRemoveItemFromSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);
        this.viewModel.SelectItem(item2, RequestOrigin.PointerInput);

        // Act
        this.viewModel.ClearSelection(item1);

        // Assert
        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([2]);
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearSelection")]
    public async Task ClearSelection_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.ClearSelection(item);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifyRemovedFromSelection([1]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearSelection")]
    public async Task ClearSelection_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(rootItem, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        var act = () => this.viewModel.ClearSelection(item);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedItemPropertyChange(Times.Never);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifyNotifySelectionChangedNever();
        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([0]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(rootItem, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        var act = () => this.viewModel.ClearAndSelectItem(item2);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedItemPropertyChange(Times.Never);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifyNotifySelectionChangedNever();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_ShouldClearSelectionAndSelectSpecifiedItem()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectAllCommand.Execute(parameter: null);

        // Act
        this.viewModel.ClearAndSelectItem(item1);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item1);
        _ = this.Selection.SelectedIndex.Should().Be(1);
        _ = this.Selection.SelectedIndices.Should().BeEquivalentTo([1]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.ClearAndSelectItem(item2);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifySelectionReset();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_Up_ShouldAddToSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var item3 = new TestTreeItemAdapter() { Label = "Item3" };
        var item4 = new TestTreeItemAdapter() { Label = "Item4" };
        var item5 = new TestTreeItemAdapter() { Label = "Item5" };
        var rootItem =
            new TestTreeItemAdapter([item1, item2, item3, item4, item5], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item4, RequestOrigin.PointerInput);

        // Act
        this.viewModel.ExtendSelectionTo(item2);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
        _ = this.Selection.SelectedItems.Should().Contain([item2, item3, item4]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_Down_ShouldAddToSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var item3 = new TestTreeItemAdapter() { Label = "Item3" };
        var item4 = new TestTreeItemAdapter() { Label = "Item4" };
        var item5 = new TestTreeItemAdapter() { Label = "Item5" };
        var rootItem =
            new TestTreeItemAdapter([item1, item2, item3, item4, item5], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);

        // Act
        this.viewModel.ExtendSelectionTo(item5);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item5);
        _ = this.Selection.SelectedIndex.Should().Be(5);
        _ = this.Selection.SelectedItems.Should().Contain([item1, item2, item3, item4, item5]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(rootItem, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        var act = () => this.viewModel.ExtendSelectionTo(item2);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedItemPropertyChange(Times.Never);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifyNotifySelectionChangedNever();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var item3 = new TestTreeItemAdapter() { Label = "Item3" };
        var rootItem = new TestTreeItemAdapter([item1, item2, item3], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.ExtendSelectionTo(item3);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifySelectionReset();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ToggleSelectAll")]
    public async Task ToggleSelectAll_ShouldSelectAllItemsWhenSomeItemsNotSelected()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item2, RequestOrigin.PointerInput);

        // Act
        this.viewModel.ToggleSelectAll();

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
        _ = this.Selection.SelectedItems.Should().Contain([item1, item2]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / ToggleSelectAll")]
    public async Task ToggleSelectAll_ShouldClearAllSelectionsWhenAllItemsSelected()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.ToggleSelectAll();

        // Act
        this.viewModel.ToggleSelectAll();

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        _ = this.Selection.SelectedItems.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectNone")]
    public async Task SelectNone_ShouldClearCurrentSelectionInMultipleSelectionMode()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1, RequestOrigin.PointerInput);

        // Act
        this.viewModel.SelectNoneCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        _ = this.Selection.SelectedItems.Should().BeEmpty();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectNone")]
    public async Task SelectNone_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.SelectNoneCommand.Execute(parameter: null);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifySelectionReset();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectAll")]
    public void SelectAll_ShouldSelectAllItems()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        this.viewModel.InitializeRootAsyncPublic(rootItem).Wait(this.TestContext.CancellationToken);

        // Act
        this.viewModel.SelectAllCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
        _ = this.Selection.SelectedItems.Should().Contain([item1, item2]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / SelectNone")]
    public async Task SelectAll_ShouldTriggerPropertyAndCollectionNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.ResetChangeHandlers();
        this.viewModel.SelectAllCommand.Execute(parameter: null);

        // Assert
        this.VerifySelectedItemPropertyChange(Times.Once);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifyNotifySelectionReset();
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / MultipleSelection / InvertSelection")]
    public void InvertSelection_ShouldInvertCurrentSelection()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var item3 = new TestTreeItemAdapter() { Label = "Item3" };
        var rootItem = new TestTreeItemAdapter([item1, item2, item3], isRoot: true) { Label = "Root", IsExpanded = true };

        this.viewModel.InitializeRootAsyncPublic(rootItem).Wait(this.TestContext.CancellationToken);
        this.viewModel.SelectItem(item2);

        // Act
        this.viewModel.InvertSelectionCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item3); // last selected item
        _ = this.Selection.SelectedIndex.Should().Be(3);
        _ = this.Selection.SelectedItems.Should().Contain([rootItem, item1, item3]);
    }

    private void ResetChangeHandlers()
    {
        this.propertyChangedHandlerMock.Reset();
        this.collectionChangedHandlerMock.Reset();
    }

    private void VerifySelectedItemPropertyChange(Func<Times> times) =>
        this.propertyChangedHandlerMock.Verify(
            handler => handler(
                It.IsAny<object>(),
                It.Is<PropertyChangedEventArgs>(e => e.PropertyName == nameof(this.Selection.SelectedItem))),
            times);

    private void VerifySelectedIndexPropertyChange(Func<Times> times) =>
        this.propertyChangedHandlerMock.Verify(
            handler => handler(
                It.IsAny<object>(),
                It.Is<PropertyChangedEventArgs>(e => e.PropertyName == nameof(this.Selection.SelectedItem))),
            times);

    private void VerifyNotifyAddedToSelection(int[] args)
    {
        var invocations = this.collectionChangedHandlerMock.Invocations
            .Where(inv => inv.Arguments.Count >= 2 && inv.Arguments[1] is NotifyCollectionChangedEventArgs)
            .Select(inv => inv.Arguments[1] as NotifyCollectionChangedEventArgs)
            .Where(a => a is not null)
            .Select(a => a!)
            .ToArray();

        var found = invocations.Any(e =>
            (e.Action == NotifyCollectionChangedAction.Add && e.NewItems != null && args.All(arg => e.NewItems.Cast<int>().Contains(arg))) ||
            e.Action == NotifyCollectionChangedAction.Reset);

        _ = found.Should().BeTrue($"Expected added indices [{string.Join(',', args)}] but actual invocations were: {string.Join(';', invocations.Select(x => x.Action + ":" + (x.NewItems is null ? "null" : string.Join(',', x.NewItems.Cast<int>()))))}");
    }

    private void VerifyNotifyRemovedFromSelection(int[] args) => this.collectionChangedHandlerMock.Verify(
        handler => handler(
            It.IsAny<object>(),
            It.Is<NotifyCollectionChangedEventArgs>(
                e => e.Action == NotifyCollectionChangedAction.Remove &&
                     args.All(arg => e.OldItems!.Contains(arg)))),
        Times.Once);

    private void VerifyNotifySelectionReset() => this.collectionChangedHandlerMock.Verify(
        handler => handler(
            It.IsAny<object>(),
            It.Is<NotifyCollectionChangedEventArgs>(
                e => e.Action == NotifyCollectionChangedAction.Reset)),
        Times.Once);

    private void VerifyNotifySelectionChangedNever() => this.collectionChangedHandlerMock.Verify(
        handler => handler(It.IsAny<object>(), It.IsAny<NotifyCollectionChangedEventArgs>()),
        Times.Never);
}
