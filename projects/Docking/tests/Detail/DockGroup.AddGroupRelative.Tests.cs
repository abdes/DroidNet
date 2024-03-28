// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups, relative to a child of the group.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupRelative_ToNotChild_Throws()
    {
        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        var sibling = new EmptyDockGroup(this.docker); // not a child of sut
        var newGroup = new MockDockGroup(this.docker);

        // Act
        var before = () => sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);
        var after = () => sut.AddGroupAfter(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = before.Should().Throw<InvalidOperationException>();
        _ = after.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupBefore_UsesFirstSlotWhenFree()
    {
        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        var sibling = new EmptyDockGroup(this.docker);
        sut.SetSecond(sibling);
        var newGroup = new MockDockGroup(this.docker);

        // Act
        sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupAfter_UsesSecondSlotWhenFree()
    {
        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        var sibling = new EmptyDockGroup(this.docker);
        sut.SetFirst(sibling);
        var newGroup = new EmptyDockGroup(this.docker);

        // Act
        sut.AddGroupAfter(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(sibling);
        _ = sut.Second.Should().BeSameAs(newGroup);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupBefore_WhenOnlySecondIsFree_SwapsFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        var sibling = new EmptyDockGroup(this.docker);
        sut.SetFirst(sibling);
        var newGroup = new MockDockGroup(this.docker);

        // Act
        sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupAfter_WhenOnlyFirstIsFree_SwapsFirstAndSecond()
    {
        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        var sibling = new EmptyDockGroup(this.docker);
        sut.SetFirst(sibling);
        var newGroup = new MockDockGroup(this.docker);

        // Act
        sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = sut.First.Should().BeSameAs(newGroup);
        _ = sut.Second.Should().BeSameAs(sibling);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    [DataRow(0)]
    [DataRow(1)]
    public void AddGroupBefore_WhenBothPartsAreOccupied_ExpandsSiblingToAddAtFirst(int siblingIndex)
    {
        const int first = 0;
        const int second = 1;

        // Arrange
        var sut = new EmptyDockGroup(this.docker);
        EmptyDockGroup[] siblings =
        [
            new EmptyDockGroup(this.docker),
            new EmptyDockGroup(this.docker),
        ];
        sut.SetFirst(siblings[first]);
        sut.SetSecond(siblings[second]);
        var sibling = siblings[siblingIndex];
        var newGroup = new EmptyDockGroup(this.docker);

        // Act
        sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        var split = siblingIndex == first ? sut.First : sut.Second;
        _ = split.Should().NotBeNull();
        _ = split!.First.Should().BeSameAs(newGroup);
        _ = split.Second.Should().BeSameAs(sibling);

        var other = siblingIndex == first ? sut.Second : sut.First;
        _ = other.Should().BeSameAs(siblings[siblingIndex == 0 ? 1 : 0]);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupBefore_InvokesOptimizerIfImplemented()
    {
        // Arrange
        var optimizingDocker = new DummyOptimizingDocker();
        var sut = new EmptyDockGroup(optimizingDocker);
        var sibling = new EmptyDockGroup(optimizingDocker);
        sut.AddGroupFirst(sibling, DockGroupOrientation.Horizontal);
        var newGroup = new MockDockGroup(optimizingDocker);

        // Act
        optimizingDocker.Reset();
        sut.AddGroupBefore(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = optimizingDocker.ConsolidateUpCalled.Should().BeTrue();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.GroupManagement")]
    public void AddGroupAfter_InvokesOptimizerIfImplemented()
    {
        // Arrange
        var optimizingDocker = new DummyOptimizingDocker();
        var sut = new EmptyDockGroup(optimizingDocker);
        var sibling = new EmptyDockGroup(optimizingDocker);
        sut.AddGroupFirst(sibling, DockGroupOrientation.Horizontal);
        var newGroup = new MockDockGroup(optimizingDocker);

        // Act
        optimizingDocker.Reset();
        sut.AddGroupAfter(newGroup, sibling, DockGroupOrientation.Undetermined);

        // Assert
        _ = optimizingDocker.ConsolidateUpCalled.Should().BeTrue();
    }
}
