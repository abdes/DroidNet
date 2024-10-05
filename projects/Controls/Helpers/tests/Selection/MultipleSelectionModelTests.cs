// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests;

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;
using Moq.Protected;

[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory($"{nameof(Controls)} - Selection Helpers")]
public class MultipleSelectionModelTests
{
    [TestMethod]
    public void ClearSelection_ShouldClearEverything()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection();

        // Assert
        _ = model.IsEmpty().Should().BeTrue();
        _ = model.SelectedIndices.Should().BeEmpty();
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().BeNull();
    }

    [TestMethod]
    public void ClearSelection_ShouldRaiseChangeEvents()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.ClearSelection();

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);
        _ = selectedIndicesMonitor.Should().Raise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void ClearSelection_WithIndex_ShouldRemoveIndex_WhenIndexIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection(1);

        // Assert
        _ = model.SelectedIndices.Should().NotContain(1);
    }

    [TestMethod]
    public void ClearSelection_WithIndex_HasNoEffect_WhenIndexIsNotSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection(2);

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
        _ = model.SelectedIndices.Should().ContainInConsecutiveOrder(1);
    }

    [TestMethod]
    public void ClearSelection_WithIndex_HasNoEffect_WhenIndexIsInvalid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection(456);

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
        _ = model.SelectedIndices.Should().ContainInConsecutiveOrder(1);
    }

    [TestMethod]
    public void ClearSelection_WithIndex_ShouldUpdateSelectedIndex_WhenIndexIsSelectedIndex()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(0);
        model.SelectItemAt(1);
        model.SelectItemAt(2);

        // Act
        model.ClearSelection(1);

        // Assert
        _ = model.SelectedIndices.Should().ContainInConsecutiveOrder(0, 2);
        _ = model.SelectedIndex.Should().Be(0);
        _ = model.SelectedItem.Should().Be("A");

        // Act
        model.ClearSelection(0);

        // Assert
        _ = model.SelectedIndices.Should().ContainInConsecutiveOrder(2);
        _ = model.SelectedIndex.Should().Be(2);
        _ = model.SelectedItem.Should().Be("C");

        // Act
        model.ClearSelection(2);

        // Assert
        _ = model.SelectedIndices.Should().BeEmpty();
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().Be(default);
    }

    [TestMethod]
    public void ClearSelection_WithIndex_ShouldRaiseChangeEvents()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.ClearSelection();

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);
        _ = selectedIndicesMonitor.Should().Raise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void ClearSelection_WithItem_ItemNotInCollection_ThrowsArgumentException()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        var act = () => model.ClearSelection("XXX");

        // Assert
        act.Should()
            .Throw<ArgumentException>()
            .WithMessage("item not found*")
            .WithParameterName("item");
    }

    [TestMethod]
    public void ClearSelection_WithItem_ItemInCollection_ShouldRemoveItemFromSelection()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection("B");

        // Assert
        _ = model.SelectedIndices.Should().BeEmpty();
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().Be(default);
    }

    [TestMethod]
    public void ClearSelection_WithItem_ShouldRaiseChangeEvents()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.ClearSelection();

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);
        _ = selectedIndicesMonitor.Should().Raise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void IsSelected_ShouldReturnTrue_WhenIndexIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        model.SelectItemAt(2);

        // Act & Assert
        _ = model.IsSelected(1).Should().BeTrue();
        _ = model.IsSelected(2).Should().BeTrue();
    }

    [TestMethod]
    public void IsSelected_ShouldReturnFalse_WhenIndexIsNotSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act & Assert
        _ = model.IsSelected(0).Should().BeFalse();
        _ = model.IsSelected(1).Should().BeFalse();
        _ = model.IsSelected(2).Should().BeFalse();
    }

    [TestMethod]
    public void IsEmpty_ShouldReturnTrue_WhenNoItemIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        var isEmpty = model.IsEmpty();

        // Assert
        _ = isEmpty.Should().BeTrue();
    }

    [TestMethod]
    public void IsEmpty_ShouldReturnFalse_WhenAnItemIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        var isEmpty = model.IsEmpty();

        // Assert
        _ = isEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void SelectItemAt_ShouldUpdateSelection_WhenIndexIsValid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        model.SelectItemAt(1);

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
    }

    [TestMethod]
    public void SelectItemAt_ShouldThrowArgumentOutOfRangeException_WhenIndexIsInvalid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        var act = () => model.SelectItemAt(3);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void SelectItemAt_ShouldNotifyChange_WhenIndexIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.SelectItemAt(1);

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = selectedIndicesMonitor.Should().Raise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void SelectItemAt_ShouldNotNotifyChange_WhenIndexIsAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.SelectItemAt(1); // Setting the same value again

        // Assert
        monitor.Should().NotRaisePropertyChangeFor(m => m.SelectedIndex);
        selectedIndicesMonitor.Should().NotRaise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void SelectItem_ShouldSelectItem_WhenItemExists()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        model.SelectItem("B");

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
    }

    [TestMethod]
    public void SelectItem_ShouldNotChangeSelection_WhenItemDoesNotExist()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        model.SelectItem("D");

        // Assert
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().BeNull();
    }

    [TestMethod]
    public void SelectItem_ShouldNotRaisePropertyChanged_WhenItemIsAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItem("B");
        using var monitor = model.Monitor();

        // Act
        model.SelectItem("B"); // Setting the same value again

        // Assert
        monitor.Should().NotRaisePropertyChangeFor(m => m.SelectedItem);
    }

    [TestMethod]
    public void SelectItem_ShouldRaisePropertyChanged_WhenItemIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        using var monitor = model.Monitor();

        // Act
        model.SelectItem("B");

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);
    }

    [TestMethod]
    public void SelectItemsAt_ValidIndices_UpdatesSelection()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");

        // Act
        model.SelectItemsAt(1, 3, 5);

        // Assert
        _ = model.SelectedIndices.Should().Equal(1, 3, 5);
        _ = model.SelectedIndex.Should().Be(1);
    }

    [TestMethod]
    public void SelectItemsAt_InvalidIndices_IgnoresInvalidIndices()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");

        // Act
        model.SelectItemsAt(-1, 11, 3);

        // Assert
        _ = model.SelectedIndices.Should().Equal(3);
        _ = model.SelectedIndex.Should().Be(3);
    }

    [TestMethod]
    public void SelectItemsAt_DuplicateIndices_IgnoresDuplicates()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        model.SelectAll(); // Initial selection

        // Act
        model.SelectItemsAt(3, 3, 5);

        // Assert
        _ = model.SelectedIndices.Should().Equal(3, 5);
        _ = model.SelectedIndex.Should().Be(3);
    }

    [TestMethod]
    public void SelectItemsAt_NoIndices_ClearsSelection()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        model.SelectAll(); // Initial selection

        // Act
        model.SelectItemsAt();

        // Assert
        _ = model.SelectedIndices.Should().BeEmpty();
    }

    [TestMethod]
    public void SelectItemsAt_EmptyItemsCollection_Works()
    {
        // Arrange
        var model = new TestSelectionModel();

        // Act
        model.SelectItemsAt(1, 3, 5);

        // Assert
        _ = model.SelectedIndices.Should().BeEmpty();
    }

    [TestMethod]
    public void SelectItemsAt_ShouldRaiseOnlyOneCollectionChanged_WhenItemIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        var changedNotifications = 0;
        ((INotifyCollectionChanged)model.SelectedIndices).CollectionChanged += (s, e) =>
        {
            _ = s; // unused

            changedNotifications++;
            var changeType = e.Action;
            changeType.Should().Be(NotifyCollectionChangedAction.Reset);
        };

        // Act
        model.SelectItemsAt(1, 3, 5);

        // Assert
        _ = changedNotifications.Should().Be(1);
    }

    [TestMethod]
    public void SelectAll_WithItems_SelectsAllIndices()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");

        // Act
        model.SelectAll();

        // Assert
        _ = model.SelectedIndices.Should().Equal(0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
        _ = model.SelectedIndex.Should().Be(9);
    }

    [TestMethod]
    public void SelectAll_NoItems_DoesNotSelectAnyIndices()
    {
        // Arrange
        var model = new TestSelectionModel();

        // Act
        model.SelectAll();

        // Assert
        _ = model.SelectedIndices.Should().BeEmpty();
    }

    [TestMethod]
    public void Selectall_ShouldRaiseOnlyOneCollectionChanged_WhenItemIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        var changedNotifications = 0;
        ((INotifyCollectionChanged)model.SelectedIndices).CollectionChanged += (s, e) =>
        {
            _ = s; // unused

            changedNotifications++;
            var changeType = e.Action;
            changeType.Should().Be(NotifyCollectionChangedAction.Reset);
        };

        // Act
        model.SelectAll();

        // Assert
        _ = changedNotifications.Should().Be(1);
    }

    [TestMethod]
    public void SelectRange_ShouldUpdateSelectedItemAndSelectedIndex()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        const int start = 1;
        const int end = 4;
        var expectedSelectedItems = new[] { "B", "C", "D", "E" };
        var expectedSelectedIndices = new[] { 1, 2, 3, 4 };

        // Act
        model.SelectRange(start, end);

        // Assert
        _ = model.SelectedItems.Should().BeEquivalentTo(expectedSelectedItems);
        _ = model.SelectedIndices.Should().BeEquivalentTo(expectedSelectedIndices);
        _ = model.SelectedIndex.Should().Be(start);
    }

    [TestMethod]
    public void SelectRange_ShouldUpdateSelectedItemAndSelectedIndex_Reversed()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        const int start = 4;
        const int end = 1;
        var expectedSelectedItems = new[] { "E", "D", "C", "B" };
        var expectedSelectedIndices = new[] { 4, 3, 2, 1 };

        // Act
        model.SelectRange(start, end);

        // Assert
        _ = model.SelectedItems.Should().BeEquivalentTo(expectedSelectedItems);
        _ = model.SelectedIndices.Should().BeEquivalentTo(expectedSelectedIndices);
        _ = model.SelectedIndex.Should().Be(start);
    }

    [TestMethod]
    public void SelectRange_ShouldThrowArgumentOutOfRangeException_WhenStartIndexIsInvalid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");

        // Act
        var act = () => model.SelectRange(-2, 3);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void SelectRange_ShouldThrowArgumentOutOfRangeException_WhenEndIndexIsInvalid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");

        // Act
        var act = () => model.SelectRange(3, 11);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>();
    }

    [TestMethod]
    public void SelectRange_ShouldRaiseOnlyOneCollectionChanged_WhenItemIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C", "D", "E", "F", "G", "H", "I", "J");
        var changedNotifications = 0;
        ((INotifyCollectionChanged)model.SelectedIndices).CollectionChanged += (s, e) =>
        {
            _ = s; // unused

            changedNotifications++;
            var changeType = e.Action;
            changeType.Should().Be(NotifyCollectionChangedAction.Reset);
        };

        // Act
        model.SelectRange(1, 3);

        // Assert
        _ = changedNotifications.Should().Be(1);
    }

    [TestMethod]
    public void SelectRange_WithItems_StartItemNotFound_ThrowsArgumentException()
    {
        // Arrange
        var items = new[] { "A", "B", "C", "D" };
        var model = new TestSelectionModel(items);

        // Act
        var act = () => model.SelectRange("XXX", "C");

        // Assert
        act.Should()
            .Throw<ArgumentException>()
            .WithMessage("item not found*")
            .WithParameterName("start");
    }

    [TestMethod]
    public void SelectRange_WithItems_EndItemNotFound_ThrowsArgumentException()
    {
        // Arrange
        var items = new[] { "A", "B", "C", "D" };
        var model = new TestSelectionModel(items);

        // Act
        var act = () => model.SelectRange("A", "XXX");

        // Assert
        act.Should()
            .Throw<ArgumentException>()
            .WithMessage("item not found*")
            .WithParameterName("end");
    }

    [TestMethod]
    public void SelectRange_ValidRange_CallsSelectRangeWithCorrectIndices()
    {
        // Arrange
        var mockModel = new Mock<MultipleSelectionModel<string>> { CallBase = true };
        mockModel.Protected()
            .Setup<int>("GetItemCount")
            .Returns(4);
        mockModel.Protected()
            .Setup<int>("IndexOf", ItExpr.IsAny<string>())
            .Returns<string>(
                item =>
                    item switch
                    {
                        "A" => 0,
                        "B" => 1,
                        "C" => 2,
                        "D" => 3,
                        _ => -1,
                    });

        // Act
        mockModel.Object.SelectRange("B", "D");

        // Assert
        mockModel.Verify(m => m.SelectRange(1, 3), Times.Once);
    }

    [TestMethod]
    public void ClearAndSelect_WithIndex_ShouldUpdateSelection_WhenIndexIsValid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectAll();

        // Act
        model.ClearAndSelectItemAt(1);

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
    }

    [TestMethod]
    public void ClearAndSelect_WithIndex_ShouldThrowArgumentOutOfRangeException_WhenIndexIsInvalid()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectAll();

        // Act
        var act = () => model.ClearAndSelectItemAt(11);

        // Assert
        _ = act.Should().Throw<ArgumentOutOfRangeException>();
        model.SelectedItems.Should().NotBeEmpty();
    }

    [TestMethod]
    public void ClearAndSelect_WithIndex_DoesNothing_WhenIndexIsSelectedAndOnlyOneItemInCollection()
    {
        // Arrange
        var model = new TestSelectionModel("A");
        model.SelectAll();
        using var monitor = model.Monitor();
        using var selectedIndicesMonitor = ((INotifyCollectionChanged)model.SelectedIndices).Monitor();

        // Act
        model.ClearAndSelectItemAt(0);

        // Assert
        _ = model.SelectedIndex.Should().Be(0);
        _ = model.SelectedItem.Should().Be("A");
        _ = model.SelectedIndices.Should().BeEquivalentTo([0]);
        monitor.Should().NotRaisePropertyChangeFor(m => m.SelectedIndex);
        monitor.Should().NotRaisePropertyChangeFor(m => m.SelectedItem);
        selectedIndicesMonitor.Should().NotRaise(nameof(INotifyCollectionChanged.CollectionChanged));
    }

    [TestMethod]
    public void ClearAndSelectItem_ShouldCallClearAndSelectItemAt_WithCorrectIndex()
    {
        // Arrange
        var model = new Mock<SelectionModel<string>>();
        const string item = "A";
        const int index = 1;
        model.Protected().Setup<int>("IndexOf", ItExpr.IsAny<string>()).Returns(index);

        // Act
        model.Object.ClearAndSelectItem(item);

        // Assert
        model.Verify(m => m.ClearAndSelectItemAt(index), Times.Once);
    }

    [TestMethod]
    public void ClearAndSelectItem_ShouldThrowArgumentException_WhenItemNotFound()
    {
        // Arrange
        var model = new Mock<SelectionModel<string>>();
        const string item = "A";
        model.Protected().Setup<int>("IndexOf", ItExpr.IsAny<string>()).Returns(-1);

        // Act
        var action = () => model.Object.ClearAndSelectItem(item);

        // Assert
        action.Should().Throw<ArgumentException>().WithMessage("item not found*");
    }

    private sealed class TestSelectionModel(params string[] items) : MultipleSelectionModel<string>
    {
        protected override string GetItemAt(int index) => items[index];

        protected override int IndexOf(string item) => Array.IndexOf(items, item);

        protected override int GetItemCount() => items.Length;
    }
}
