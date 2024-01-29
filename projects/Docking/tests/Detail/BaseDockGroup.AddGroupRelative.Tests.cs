// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups, relative to a child of the group.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    public void AddGroupRelative_ToNotChild_Throws()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var sibling = new EmptyDockGroup(); // not a child of sut
        var newGroup = new DummyDockGroup();

        // Act
        var before = () => sut.AddGroupBefore(newGroup, sibling, Orientation.Undetermined);
        var after = () => sut.AddGroupAfter(newGroup, sibling, Orientation.Undetermined);

        // Assert
        _ = before.Should().Throw<InvalidOperationException>();
        _ = after.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void AddGroupBefore_UsesFirstSlotWhenFree()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var sibling = new EmptyDockGroup();
        sut.SetSecond(sibling);
        var newGroup = new MockDockGroup();

        // Act
        sut.AddGroupBefore(newGroup, sibling, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    public void AddGroupAfter_UsesSecondSlotWhenFree()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var sibling = new EmptyDockGroup();
        sut.SetFirst(sibling);
        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupAfter(newGroup, sibling, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(sibling);
        _ = sut.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    public void AddGroupBefore_WhenOnlySecondIsFree_SwapsFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var sibling = new EmptyDockGroup();
        sut.SetFirst(sibling);
        var newGroup = new MockDockGroup();

        // Act
        sut.AddGroupBefore(newGroup, sibling, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    public void AddGroupAfter_WhenOnlyFirstIsFree_SwapsFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var sibling = new EmptyDockGroup();
        sut.SetFirst(sibling);
        var newGroup = new MockDockGroup();

        // Act
        sut.AddGroupBefore(newGroup, sibling, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    [DataRow(0)]
    [DataRow(1)]
    public void AddGroupBefore_WhenBothPartsAreOccupied_ExpandsSiblingToAddAtFirst(int siblingIndex)
    {
        const int first = 0;
        const int second = 1;

        // Arrange
        var sut = new EmptyDockGroup();
        EmptyDockGroup[] siblings =
        [
            new EmptyDockGroup(),
            new EmptyDockGroup(),
        ];
        sut.SetFirst(siblings[first]);
        sut.SetSecond(siblings[second]);
        var sibling = siblings[siblingIndex];
        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupBefore(newGroup, sibling, Orientation.Undetermined);

        // Assert
        var split = siblingIndex == first ? sut.First : sut.Second;
        _ = split.Should().NotBeNull();
        _ = split!.First.Should().BeSameAs(newGroup);
        _ = split.Second.Should().BeSameAs(sibling);

        var other = siblingIndex == first ? sut.Second : sut.First;
        _ = other.Should().BeSameAs(siblings[siblingIndex == 0 ? 1 : 0]);
    }
}
