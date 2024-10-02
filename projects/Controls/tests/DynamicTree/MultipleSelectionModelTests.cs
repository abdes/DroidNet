// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests;

using System.Collections.Specialized;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
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
    public void ClearSelection_WithIndex_ShouldUpdateSelectedIndex_WhenIndexIsSelectedIndexAnd()
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

    private class TestSelectionModel(params string[] items) : MultipleSelectionModel<string>
    {
        protected override string GetItemAt(int index) => items[index];

        protected override int IndexOf(string item) => Array.IndexOf(items, item);

        protected override int GetItemCount() => items.Length;
    }
}
