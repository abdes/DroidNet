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
public class ViewModelSingleSelectionTests
{
    private readonly TestViewModel viewModel = new(skipRoot: false) { SelectionMode = SelectionMode.Single };
    private readonly Mock<PropertyChangedEventHandler> propertyChangedHandlerMock;

    public ViewModelSingleSelectionTests()
    {
        this.propertyChangedHandlerMock = new Mock<PropertyChangedEventHandler>();
        this.Selection.PropertyChanged += this.propertyChangedHandlerMock.Object;
    }

    private SelectionModel<ITreeItem> Selection => this.viewModel.GetSelectionModel()!;

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectItem")]
    public async Task SelectItem_ShouldDoNothingIfItemNotShown()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectItem")]
    public async Task SelectItem_ShouldSelectSpecifiedItem()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item);
        _ = this.Selection.SelectedIndex.Should().Be(1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectItem")]
    public async Task SelectItem_SelectsFirstOccurrenceOfItem()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item, item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item);
        _ = this.Selection.SelectedIndex.Should().Be(1);
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectItem")]
    public async Task SelectItem_ShouldTriggerPropertyChangeNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        this.viewModel.SelectItem(item);

        // Assert
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearSelection")]
    public async Task ClearSelection_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = () => this.viewModel.ClearSelection(item);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearSelection")]
    public async Task ClearSelection_ShouldClearSelection()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item);

        // Act
        this.viewModel.ClearSelection(item);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearSelection")]
    public async Task ClearSelection_ShouldTriggerPropertyChangeNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item);

        // Act
        this.propertyChangedHandlerMock.Reset();
        this.viewModel.ClearSelection(item);

        // Assert
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = () => this.viewModel.ClearAndSelectItem(item2);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_ShouldClearSelectionAndSelectSpecifiedItem()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1);

        // Act
        this.viewModel.ClearAndSelectItem(item2);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ClearAndSelect")]
    public async Task ClearAndSelectItem_ShouldTriggerOnlyOneNotificationPerProperty()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1);

        // Act
        this.propertyChangedHandlerMock.Reset();
        this.viewModel.ClearAndSelectItem(item2);

        // Assert
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_ShouldSelectSpecifiedItem()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1);

        // Act
        this.viewModel.ExtendSelectionTo(item2);

        // Assert
        _ = this.Selection.SelectedItem.Should().Be(item2);
        _ = this.Selection.SelectedIndex.Should().Be(2);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_WhenItemNotShown_ThrowsArgumentException()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = false };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);

        // Act
        var act = () => this.viewModel.ExtendSelectionTo(item2);

        // Assert
        _ = act.Should().Throw<ArgumentException>();
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ExtendSelection")]
    public async Task ExtendSelectionTo_ShouldTriggerPropertyChangeNotifications()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item1);

        // Act
        this.propertyChangedHandlerMock.Reset();
        this.viewModel.ExtendSelectionTo(item2);

        // Assert
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / ToggleSelectAll")]
    public void ToggleSelectAll_ShouldDoNothing()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        this.viewModel.InitializeRootAsyncPublic(rootItem).Wait();

        // Act
        this.viewModel.ToggleSelectAll();

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        this.propertyChangedHandlerMock.Verify(
            handler => handler(It.IsAny<object>(), It.IsAny<PropertyChangedEventArgs>()),
            Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectNone")]
    public async Task SelectNone_ShouldClearCurrentSelectionInSingleSelectionMode()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item);

        // Act
        this.viewModel.SelectNoneCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectNone")]
    public async Task SelectNone_ShouldTriggerPropertyChangeNotifications()
    {
        // Arrange
        var item = new TestTreeItemAdapter() { Label = "Item" };
        var rootItem = new TestTreeItemAdapter([item], isRoot: true) { Label = "Root", IsExpanded = true };

        await this.viewModel.InitializeRootAsyncPublic(rootItem).ConfigureAwait(false);
        this.viewModel.SelectItem(item);

        // Act
        this.propertyChangedHandlerMock.Reset();
        this.viewModel.SelectNoneCommand.Execute(parameter: null);

        // Assert
        this.VerifySelectedIndexPropertyChange(Times.Once);
        this.VerifySelectedItemPropertyChange(Times.Once);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / SelectAll")]
    public void SelectAll_ShouldDoNothing()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        this.viewModel.InitializeRootAsyncPublic(rootItem).Wait();

        // Act
        this.viewModel.SelectAllCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
    }

    [TestMethod]
    [TestCategory($"{nameof(DynamicTree)} / ViewModel / SingleSelection / InvertSelection")]
    public void InvertSelection_ShouldDoNothing()
    {
        // Arrange
        var item1 = new TestTreeItemAdapter() { Label = "Item1" };
        var item2 = new TestTreeItemAdapter() { Label = "Item2" };
        var rootItem = new TestTreeItemAdapter([item1, item2], isRoot: true) { Label = "Root", IsExpanded = true };

        this.viewModel.InitializeRootAsyncPublic(rootItem).Wait();

        // Act
        this.viewModel.InvertSelectionCommand.Execute(parameter: null);

        // Assert
        _ = this.Selection.SelectedItem.Should().BeNull();
        _ = this.Selection.SelectedIndex.Should().Be(-1);
        this.VerifySelectedIndexPropertyChange(Times.Never);
        this.VerifySelectedItemPropertyChange(Times.Never);
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
}
