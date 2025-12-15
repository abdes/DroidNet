// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.Collections.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ObservableCollection Extensions")]
public class ObservableCollectionExtensionsTests
{
    [TestMethod]
    public void InsertInPlace_WhenCollectionIsSorted_InsertsItemInCorrectPosition()
    {
        // Arrange
        var collection = new ObservableCollection<int>
        {
            1,
            3,
            5,
            7,
        };
        const int itemToAdd = 4;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().Equal(1, 3, 4, 5, 7);
    }

    [TestMethod]
    public void InsertInPlace_WhenCollectionIsEmpty_InsertsItem()
    {
        // Arrange
        var collection = new ObservableCollection<int>();
        const int itemToAdd = 42;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().Equal(42);
    }

    [TestMethod]
    public void InsertInPlace_WhenItemHasDuplicateKey_InsertsItemNextToExistingKey()
    {
        // Arrange
        var collection = new ObservableCollection<int>
        {
            1,
            3,
            5,
            7,
        };
        const int itemToAdd = 3; // Duplicate key

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().Equal(1, 3, 3, 5, 7); // Should insert next to existing 3
    }

    [TestMethod]
    public void InsertInPlace_WhenCollectionIsEmpty_AddsItem()
    {
        // Arrange
        var collection = new ObservableCollection<int>();
        const int itemToAdd = 4;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().ContainSingle();
        _ = collection.Should().Equal(4);
    }

    [TestMethod]
    public void InsertInPlace_WhenCollectionIsNotEmpty_InsertsItemInCorrectPlace()
    {
        // Arrange
        var collection = new ObservableCollection<int> { 1, 3, 5, 7 };
        const int itemToAdd = 4;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().HaveCount(5);
        _ = collection.Should().Equal(1, 3, 4, 5, 7);
    }

    [TestMethod]
    public void InsertInPlace_WhenItemIsSmallest_InsertsItemAtStart()
    {
        // Arrange
        var collection = new ObservableCollection<int> { 2, 3, 4, 5 };
        const int itemToAdd = 1;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().HaveCount(5);
        _ = collection.Should().Equal(1, 2, 3, 4, 5);
    }

    [TestMethod]
    public void InsertInPlace_WhenItemIsLargest_InsertsItemAtEnd()
    {
        // Arrange
        var collection = new ObservableCollection<int> { 1, 2, 3, 4 };
        const int itemToAdd = 5;

        // Act
        collection.InsertInPlace(itemToAdd, x => x, Comparer<int>.Default);

        // Assert
        _ = collection.Should().HaveCount(5);
        _ = collection.Should().Equal(1, 2, 3, 4, 5);
    }

    [TestMethod]
    public void Transform_WhenSourceHasItems_TransformsItems()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2, 3 };

        // Act
        using var result = source.Transform(x => string.Create(CultureInfo.InvariantCulture, $"Item {x * 10}"));

        // Assert
        _ = result.Should().HaveCount(3);
        _ = result.Should().Equal("Item 10", "Item 20", "Item 30");
    }

    [TestMethod]
    public void Transform_WhenSourceAddsItem_AddsTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int>();

        // Act
        using var result = source.Transform(x => string.Create(CultureInfo.InvariantCulture, $"Item {x * 10}"));
        source.Add(4);

        // Assert
        _ = result.Should().ContainSingle();
        _ = result.Should().Equal("Item 40");
    }

    [TestMethod]
    public void Transform_WhenSourceRemovesItem_RemovesTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2 };

        // Act
        using var result = source.Transform(x => string.Create(CultureInfo.InvariantCulture, $"Item {x * 10}"));
        _ = source.Remove(1);

        // Assert
        _ = result.Should().ContainSingle();
        _ = result.Should().Equal("Item 20");
    }

    [TestMethod]
    public void Transform_WhenSourceReplacesItem_UpdatesTransformedItem()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2 };

        // Act
        using var result = source.Transform(x => string.Create(CultureInfo.InvariantCulture, $"Item {x * 10}"));
        source[0] = 3;

        // Assert
        _ = result.Should().HaveCount(2);
        _ = result.Should().Equal("Item 30", "Item 20");
    }

    [TestMethod]
    public void Transform_WhenSourceClearsCollection_ClearsTransformedCollection()
    {
        // Arrange
        var source = new ObservableCollection<int> { 1, 2 };

        // Act
        using var result = source.Transform(x => string.Create(CultureInfo.InvariantCulture, $"Item {x * 10}"));
        source.Clear();

        // Assert
        _ = result.Should().BeEmpty();
    }
}
