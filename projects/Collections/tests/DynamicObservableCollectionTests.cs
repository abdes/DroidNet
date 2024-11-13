// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections.Tests;

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.TestHelpers;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class DynamicObservableCollectionTests
{
    private readonly Func<int, string> transform = i => $"Item {i.ToString(CultureInfo.InvariantCulture)}";

    [TestMethod]
    [TestCategory("DynamicObservableCollection - Initialize")]
    public void DynamicObservableCollection_InitializesWithTransformedItems()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_AddsTransformedItemsOnSourceAdd()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_InsertsTransformedItemsOnSourceInsert()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_RemovesTransformedItemsOnSourceRemove()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_UpdatesTransformedItemsOnSourceReplace()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_MovesTransformedItemsOnSourceMove()
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
    [TestCategory("DynamicObservableCollection - Changes")]
    public void DynamicObservableCollection_ResetsCollectionOnSourceReset()
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
    [TestCategory("DynamicObservableCollection - Disposal")]
    public void DynamicObservableCollection_Dispose_UnregistersFromSource()
    {
        // Arrange
        var source = new ObservableCollection<int>();
        var collection = new DynamicObservableCollection<int, string>(source, this.transform);
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
}
