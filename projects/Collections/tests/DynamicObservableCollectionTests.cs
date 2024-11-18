// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.TestHelpers;
using FluentAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Dynamic Observable Collection")]
public class DynamicObservableCollectionTests
{
    private readonly Func<int, string> transform = i => $"Item {i.ToString(CultureInfo.InvariantCulture)}";

    [TestMethod]
    public void Initialize_WhenSourceHasItems_TransformsItems()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
            3,
        };

        // Act
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 1", "Item 2", "Item 3");
    }

    [TestMethod]
    public void Add_WhenSourceAddsItem_AddsTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>();
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Add(4);

        // Assert
        _ = collection.Count.Should().Be(1);
        _ = collection.Should().Equal("Item 4");
    }

    [TestMethod]
    public void Insert_WhenSourceInsertsItem_InsertsTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
        };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Insert(1, 10);

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 1", "Item 10", "Item 2");
    }

    [TestMethod]
    public void Remove_WhenSourceRemovesItem_RemovesTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
        };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        _ = source.Remove(1);

        // Assert
        _ = collection.Count.Should().Be(1);
        _ = collection.Should().Equal("Item 2");
    }

    [TestMethod]
    public void Replace_WhenSourceReplacesItem_UpdatesTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
        };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source[0] = 3;

        // Assert
        _ = collection.Count.Should().Be(2);
        _ = collection.Should().Equal("Item 3", "Item 2");
    }

    [TestMethod]
    public void Move_WhenSourceMovesItem_MovesTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
            3,
        };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Move(1, 0);

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 2", "Item 1", "Item 3");
    }

    [TestMethod]
    public void Reset_WhenSourceResetsCollection_ResetsTransformedCollection()
    {
        // Arrange
        var source = new ObservableCollection<int>()
        {
            1,
            2,
        };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Clear();
        source.Add(3);
        source.Add(4);

        // Assert
        _ = collection.Count.Should().Be(2);
        _ = collection.Should().Equal("Item 3", "Item 4");
    }

    [TestMethod]
    public void Dispose_WhenCalled_DisconnectsFromSource()
    {
        // Arrange
        var source = new ObservableCollection<int>();
        var collection = new DynamicObservableCollection<int, string>(source, this.transform);
        _ = collection; // Suppress warning about unused variable

        var registeredDelegates = EventHandlerTestHelper.FindAllRegisteredDelegates(
            source,
            "CollectionChanged");
        _ = registeredDelegates.Should().Contain(x => x.Method.Name == "OnSourceOnCollectionChanged");

        // Act
        collection.Dispose();

        // Assert
        registeredDelegates = EventHandlerTestHelper.FindAllRegisteredDelegates(
            source,
            "CollectionChanged");
        _ = registeredDelegates.Should().NotContain(x => x.Method.Name == "OnSourceOnCollectionChanged");
    }

    [TestMethod]
    public void Dispose_WhenCalledMultipleTimes_DoesNotThrow()
    {
        // Arrange
        var source = new ObservableCollection<int>();
        var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        collection.Dispose();
        var act = collection.Dispose;

        // Assert
        _ = act.Should().NotThrow();
    }

    [TestMethod]
    public void AddMultiple_WhenSourceAddsMultipleItems_AddsTransformedItems()
    {
        // Arrange
        var source = new ObservableCollection<int>();
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Add(1);
        source.Add(2);
        source.Add(3);

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 1", "Item 2", "Item 3");
    }

    [TestMethod]
    public void RemoveMultiple_WhenSourceRemovesMultipleItems_RemovesTransformedItems()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3 };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        _ = source.Remove(1);
        _ = source.Remove(2);

        // Assert
        _ = collection.Count.Should().Be(1);
        _ = collection.Should().Equal("Item 3");
    }

    [TestMethod]
    public void ReplaceMultiple_WhenSourceReplacesMultipleItems_UpdatesTransformedItems()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3 };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source[0] = 4;
        source[1] = 5;

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 4", "Item 5", "Item 3");
    }

    [TestMethod]
    public void MoveMultiple_WhenSourceMovesMultipleItems_MovesTransformedItems()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3, 4 };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Move(0, 3); // Moves 1 to the end, resulting in { 2, 3, 4, 1 }
        source.Move(1, 2); // Moves 3 one step to the right, resulting in { 2, 4, 3, 1 }

        // Assert
        _ = collection.Count.Should().Be(4);
        _ = collection.Should().Equal("Item 2", "Item 4", "Item 3", "Item 1");
    }

    [TestMethod]
    public void CombinationOperations_WhenSourcePerformsCombinationOperations_UpdatesTransformedCollection()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3 };
        using var collection = new DynamicObservableCollection<int, string>(source, this.transform);

        // Act
        source.Add(4);
        _ = source.Remove(2);
        source[0] = 5;

        // Assert
        _ = collection.Count.Should().Be(3);
        _ = collection.Should().Equal("Item 5", "Item 3", "Item 4");
    }
}
