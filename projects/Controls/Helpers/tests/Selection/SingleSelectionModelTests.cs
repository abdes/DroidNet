// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls.Selection;
using FluentAssertions;

namespace DroidNet.Controls.Tests.Selection;

/// <summary>
/// Unit test cases for the <see cref="SingleSelectionModel{T}" /> class.
/// </summary>
[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory($"{nameof(Controls)} - Selection Helpers")]
public partial class SingleSelectionModelTests
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
        _ = model.IsEmpty.Should().BeTrue();
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().BeNull();
    }

    [TestMethod]
    public void ClearSelection_ShouldRaisePropertyChanged()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();

        // Act
        model.ClearSelection();

        // Assert
        _ = model.IsEmpty.Should().BeTrue();
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().BeNull();
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);
    }

    [TestMethod]
    public void ClearSelection_WithIndex_ShouldClearSelection_WhenIndexIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection(1);

        // Assert
        _ = model.SelectedIndex.Should().Be(-1);
        _ = model.SelectedItem.Should().BeNull();
    }

    [TestMethod]
    public void ClearSelection_WithIndex_ShouldNotClearSelection_WhenIndexIsNotSelected()
    {
        // arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearSelection(0);

        // Assert
        _ = model.SelectedIndex.Should().Be(1);
        _ = model.SelectedItem.Should().Be("B");
    }

    [TestMethod]
    public void ClearAndSelect_ShouldClearPreviousSelectionAndSelectNewItem()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        model.ClearAndSelectItemAt(2);

        // Assert
        _ = model.SelectedIndex.Should().Be(2);
        _ = model.SelectedItem.Should().Be("C");
    }

    [TestMethod]
    public void ClearAndSelect_ShouldNotRaiseIntermediatePropertyChangedEvents()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();

        // Act
        model.ClearAndSelectItemAt(2);

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedItem);

        var events = monitor.OccurredEvents
            .Where(
                e => string.Equals(
                    e.EventName,
                    nameof(INotifyPropertyChanged.PropertyChanged),
                    StringComparison.Ordinal))
            .Select(e => (PropertyChangedEventArgs)e.Parameters[1])
            .ToList();

        _ = events.Should()
            .HaveCount(2, "only the final property change events should be raised")
            .And.ContainSingle(
                e => string.Equals(e.PropertyName, nameof(model.SelectedIndex), StringComparison.Ordinal),
                "the SelectedIndex should be set to the new value")
            .And.ContainSingle(
                e => string.Equals(e.PropertyName, nameof(model.SelectedItem), StringComparison.Ordinal),
                "the SelectedItem should be set to the new value");
    }

    [TestMethod]
    public void IsSelected_ShouldReturnTrue_WhenIndexIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);

        // Act
        var isSelected = model.IsSelected(1);

        // Assert
        _ = isSelected.Should().BeTrue();
    }

    [TestMethod]
    public void IsSelected_ShouldReturnFalse_WhenIndexIsNotSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        var isSelected = model.IsSelected(1);

        // Assert
        _ = isSelected.Should().BeFalse();
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
    public void SelectItemAt_ShouldRaisePropertyChanged_WhenIndexIsNotAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        using var monitor = model.Monitor();

        // Act
        model.SelectItemAt(1);

        // Assert
        _ = monitor.Should().RaisePropertyChangeFor(m => m.SelectedIndex);
    }

    [TestMethod]
    public void SelectItemAt_ShouldNotRaisePropertyChanged_WhenIndexIsAlreadySelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");
        model.SelectItemAt(1);
        using var monitor = model.Monitor();

        // Act
        model.SelectItemAt(1); // Setting the same value again

        // Assert
        monitor.Should().NotRaisePropertyChangeFor(m => m.SelectedIndex);
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

    private sealed partial class TestSelectionModel(params string[] items) : SingleSelectionModel<string>
    {
        protected override string GetItemAt(int index) => items[index];

        protected override int IndexOf(string item) => Array.IndexOf(items, item);

        protected override int GetItemCount() => items.Length;
    }
}
