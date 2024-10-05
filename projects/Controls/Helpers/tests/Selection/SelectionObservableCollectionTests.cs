// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests;

using System.Diagnostics.CodeAnalysis;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Moq;

[TestClass]
[ExcludeFromCodeCoverage]
public class SelectionObservableCollectionTests
{
    private readonly SelectionObservableCollection<ISelectable> collection;
    private readonly Mock<ISelectable> mockSelectable;

    public SelectionObservableCollectionTests()
    {
        this.mockSelectable = new Mock<ISelectable>();
        var mockItemGetter = new Mock<SelectionObservableCollection<ISelectable>.ItemGetter>();
        mockItemGetter.Setup(getter => getter(It.IsAny<int>())).Returns(this.mockSelectable.Object);
        this.collection = new SelectionObservableCollection<ISelectable>([])
        {
            GetItemAt = mockItemGetter.Object,
        };
    }

    [TestMethod]
    public void InsertItem_ShouldSelectItem_WhenItemIsSelectable()
    {
        const int index = 0;
        const int item = 1;
        this.collection.Insert(index, item);
        this.mockSelectable.VerifySet(selectable => selectable.IsSelected = true, Times.Once);
    }

    [TestMethod]
    public void SetItem_ShouldDeselectOldItem_AndSelectNewItem_WhenItemsAreSelectable()
    {
        const int index = 0;
        const int oldItem = 1;
        const int newItem = 2;
        this.collection.Add(oldItem);
        this.collection[index] = newItem;
        this.mockSelectable.VerifySet(selectable => selectable.IsSelected = false, Times.Exactly(2));
    }

    [TestMethod]
    public void ClearItems_ShouldDeselectAllItems_WhenItemsAreSelectable()
    {
        const int item = 1;
        this.collection.Add(item);
        this.collection.Clear();
        this.mockSelectable.VerifySet(selectable => selectable.IsSelected = false, Times.Once);
    }

    [TestMethod]
    public void RemoveItem_ShouldDeselectItem_WhenItemIsSelectable()
    {
        const int item = 1;
        this.collection.Add(item);
        this.collection.RemoveAt(0);
        this.mockSelectable.VerifySet(selectable => selectable.IsSelected = false, Times.Once);
    }

    [TestMethod]
    public void SuspendNotifications_ShouldSuppressNotifications()
    {
        using (this.collection.SuspendNotifications())
        {
            this.collection.Add(1);
        }

        this.collection.CollectionChanged
            += (sender, e) => Assert.Fail("CollectionChanged event should not be raised.");
    }
}
