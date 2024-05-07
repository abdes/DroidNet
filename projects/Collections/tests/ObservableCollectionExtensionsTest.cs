// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Collections;

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
public class ObservableCollectionExtensionsTest
{
    [TestMethod]
    public void InsertInPlace_Should_Insert_Item_In_Correct_Position()
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
    public void InsertInPlace_Should_Handle_Empty_Collection()
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
    public void InsertInPlace_Should_Insert_Item_With_Duplicate_Key()
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
}
