// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupLast_ToEmptyLeaf_UsesFirstSlot()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var newGroup = new MockDockGroup();

        // Act
        sut.AddGroupLast(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().NotBeNull().And.BeEquivalentTo(newGroup);
        _ = sut.Second.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupLast_ToNonEmptyGroup()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        sut.ExpectMigrateToBeCalled();

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupLast(newGroup, Orientation.Undetermined);

        // Assert
        sut.Verify();
        _ = sut.First.Should().NotBeNull();
        _ = sut.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupLast_FirstPartOccupied_UsesSecondSlot()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldFirst = new EmptyDockGroup();
        sut.SetFirst(oldFirst);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupLast(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(oldFirst);
        _ = sut.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupLast_SecondPartOccupied_SwapsFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldSecond = new EmptyDockGroup();
        sut.SetSecond(oldSecond);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupLast(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(oldSecond);
        _ = sut.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupLast_BothPartsOccupied_ExpandsSecondToAdd()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldFirst = new EmptyDockGroup();
        var oldSecond = new EmptyDockGroup();
        sut.SetFirst(oldFirst);
        sut.SetSecond(oldSecond);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupLast(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(oldFirst);
        _ = sut.Second.Should().NotBeNull();
        _ = sut.Second!.First.Should().BeSameAs(oldSecond);
        _ = sut.Second!.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupFirst_ToEmptyLeaf_UsesFirstSlot()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var newGroup = new MockDockGroup();

        // Act
        sut.AddGroupFirst(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().NotBeNull().And.BeEquivalentTo(newGroup);
        _ = sut.Second.Should().BeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupFirst_ToNonEmptyGroup()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        sut.ExpectMigrateToBeCalled();

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupFirst(newGroup, Orientation.Undetermined);

        // Assert
        sut.Verify();
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().NotBeNull();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupFirst_FirstPartOccupied_SwapFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldFirst = new EmptyDockGroup();
        sut.SetFirst(oldFirst);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupFirst(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(oldFirst);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupFirst_SecondPartOccupied_UseFirstSlot()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldSecond = new EmptyDockGroup();
        sut.SetSecond(oldSecond);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupFirst(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(oldSecond);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupFirst_BothPartsOccupied_ExpandsFirstToAdd()
    {
        // Arrange
        var sut = new EmptyDockGroup();
        var oldFirst = new EmptyDockGroup();
        var oldSecond = new EmptyDockGroup();
        sut.SetFirst(oldFirst);
        sut.SetSecond(oldSecond);

        var newGroup = new EmptyDockGroup();

        // Act
        sut.AddGroupFirst(newGroup, Orientation.Undetermined);

        // Assert
        _ = sut.Second.Should().BeSameAs(oldSecond);
        _ = sut.First.Should().NotBeNull();
        _ = sut.First!.First.Should().BeSameAs(newGroup);
        _ = sut.First!.Second.Should().BeSameAs(oldFirst);
    }
}
