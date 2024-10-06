// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests;

using System.Diagnostics.CodeAnalysis;
using DroidNet.TestHelpers;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory($"{nameof(Controls)} - Selection Helpers")]
public class SelectionModelTests : TestSuiteWithAssertions
{
    [TestMethod]
    public void IsEmpty_ShouldReturnTrue_WhenNoItemIsSelected()
    {
        // Arrange
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        var isEmpty = model.IsEmpty;

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
        var isEmpty = model.IsEmpty;

        // Assert
        _ = isEmpty.Should().BeFalse();
    }

    [TestMethod]
    public void UpdateSelectedIndex_AssertsIndexArgumentIsValid()
    {
        // Arrange
        var model = new FailAssertSelectionModel();

        // Act
        try
        {
            model.SelectItem("WHATEVER");
        }
        catch
        {
            // ignored because we just care about checking that the assertion failed
        }
        finally
        {
#if DEBUG
            _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#else
            _ = this.TraceListener.RecordedMessages.Should().BeEmpty();
#endif
        }
    }

    [TestMethod]
    public void SetSelectedIndex_ShouldReturnFalse_WhenValueIsSameAsSelectedIndex()
    {
        // Arrange
        const int initialIndex = 0;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        var result = model.PublicSetSelectedIndex(initialIndex);

        // Assert
        _ = result.Should().BeFalse();
    }

    [TestMethod]
    public void SetSelectedIndex_ShouldReturnTrue_WhenValueIsDifferentFromSelectedIndex()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        var result = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = result.Should().BeTrue();
    }

    [TestMethod]
    public void SetSelectedIndex_ShouldUpdateSelectedIndex_WhenNewValueIsDifferent()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.SelectedIndex.Should().Be(newIndex);
    }

    [TestMethod]
    public void SetSelectedIndex_ShouldUpdateSelectedItem_WhenNewValueIsDifferent()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };
        _ = model.PublicSetSelectedIndex(newIndex);

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.SelectedItem.Should().Be("B");
    }

    [TestMethod]
    public void SelectItemAt_ShouldRaisePropertyChangeEventsForSelectedIndex_WhenIndexIsNotAlreadySelected()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.SelectedIndexEventRaisedInfo.PropertyChanging.Should().BeTrue();
        _ = model.SelectedIndexEventRaisedInfo.PropertyChanged.Should().BeTrue();
    }

    [TestMethod]
    public void SelectItemAt_ShouldRaisePropertyChangeEventsForSelectedItem_WhenIndexIsNotAlreadySelected()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.SelectedItemEventRaisedInfo.PropertyChanging.Should().BeTrue();
        _ = model.SelectedItemEventRaisedInfo.PropertyChanged.Should().BeTrue();
    }

    [TestMethod]
    public void SelectItemAt_ShouldRaisePropertyChangeEventsForIsEmpty_WhenNoSelection()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = -1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.IsEmptyEventRaisedInfo.PropertyChanging.Should().BeTrue();
        _ = model.IsEmptyEventRaisedInfo.PropertyChanged.Should().BeTrue();
    }

    [TestMethod]
    public void SelectItemAt_ShouldRaisePropertyChangeEventsForIsEmpty_WhenSelectionAfterNoSelection()
    {
        // Arrange
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C");

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.IsEmptyEventRaisedInfo.PropertyChanging.Should().BeTrue();
        _ = model.IsEmptyEventRaisedInfo.PropertyChanged.Should().BeTrue();
    }

    [TestMethod]
    public void SelectItemAt_ShouldNotRaisePropertyChangeEventsForIsEmpty_WhenSelectionWhileNotEmpty()
    {
        // Arrange
        const int initialIndex = 0;
        const int newIndex = 1;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(newIndex);

        // Assert
        _ = model.IsEmptyEventRaisedInfo.PropertyChanging.Should().BeFalse();
        _ = model.IsEmptyEventRaisedInfo.PropertyChanged.Should().BeFalse();
    }

    [TestMethod]
    public void SetSelectedIndex_ShouldNotRaiseEvents_WhenValueIsSameAsSelectedIndex()
    {
        // Arrange
        const int initialIndex = 0;
        var model = new TestSelectionModel("A", "B", "C") { InitialIndex = initialIndex };

        // Act
        _ = model.PublicSetSelectedIndex(initialIndex);

        // Assert
        _ = model.SelectedIndexEventRaisedInfo.PropertyChanging.Should().BeFalse();
        _ = model.SelectedIndexEventRaisedInfo.PropertyChanged.Should().BeFalse();
        _ = model.SelectedItemEventRaisedInfo.PropertyChanging.Should().BeFalse();
        _ = model.SelectedItemEventRaisedInfo.PropertyChanged.Should().BeFalse();
    }

    private sealed class TestSelectionModel : SingleSelectionModel<string>
    {
        private readonly string[] items;

        public TestSelectionModel(params string[] items)
        {
            this.items = items;

            this.PropertyChanging += (_, args) =>
            {
                switch (args.PropertyName)
                {
                    case nameof(this.SelectedIndex):
                        this.SelectedIndexEventRaisedInfo.PropertyChanging = true;
                        break;
                    case nameof(this.SelectedItem):
                        this.SelectedItemEventRaisedInfo.PropertyChanging = true;
                        break;
                    case nameof(this.IsEmpty):
                        this.IsEmptyEventRaisedInfo.PropertyChanging = true;
                        break;

                    default:
                        // Ignore
                        break;
                }
            };

            this.PropertyChanged += (_, args) =>
            {
                switch (args.PropertyName)
                {
                    case nameof(this.SelectedIndex):
                        this.SelectedIndexEventRaisedInfo.PropertyChanged = true;
                        break;
                    case nameof(this.SelectedItem):
                        this.SelectedItemEventRaisedInfo.PropertyChanged = true;
                        break;
                    case nameof(this.IsEmpty):
                        this.IsEmptyEventRaisedInfo.PropertyChanged = true;
                        break;

                    default:
                        // Ignore
                        break;
                }
            };
        }

        public EventRaisedInfo SelectedIndexEventRaisedInfo { get; } = new();

        public EventRaisedInfo SelectedItemEventRaisedInfo { get; } = new();

        public EventRaisedInfo IsEmptyEventRaisedInfo { get; } = new();

        public int InitialIndex
        {
            init
            {
                if (value != -1)
                {
                    _ = this.SetSelectedIndex(value);
                }
            }
        }

        public bool PublicSetSelectedIndex(int value)
        {
            this.SelectedIndexEventRaisedInfo.PropertyChanging = false;
            this.SelectedIndexEventRaisedInfo.PropertyChanged = false;
            this.SelectedItemEventRaisedInfo.PropertyChanging = false;
            this.SelectedItemEventRaisedInfo.PropertyChanged = false;
            this.IsEmptyEventRaisedInfo.PropertyChanging = false;
            this.IsEmptyEventRaisedInfo.PropertyChanged = false;

            return this.SetSelectedIndex(value);
        }

        protected override string GetItemAt(int index) => this.items[index];

        protected override int IndexOf(string item) => Array.IndexOf(this.items, item);

        protected override int GetItemCount() => this.items.Length;

        public sealed class EventRaisedInfo
        {
            public bool PropertyChanged { get; set; }

            public bool PropertyChanging { get; set; }
        }
    }

    private sealed class FailAssertSelectionModel : SingleSelectionModel<string>
    {
        private const int BadIndex = -100;

        protected override string GetItemAt(int index) => throw new ArgumentOutOfRangeException(nameof(index));

        protected override int IndexOf(string item) => BadIndex;

        protected override int GetItemCount() => 0;
    }
}
