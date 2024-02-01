// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Contains unit test cases for the <see cref="DockGroup" /> class, for
/// adding and removing groups.
/// </summary>
public partial class DockGroupTests
{
    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void RemoveDock_NotInDocks_Throws()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        var newDock = DummyDock.New();

        // Act
        var action = () => sut.AddDock(newDock);

        // Assert
        _ = action.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void RemoveDock_AmongMany_JustRemovesIt()
    {
        // Arrange
        var sut = new NonEmptyDockGroup();
        var first = sut.Docks.First();
        var second = DummyDock.New();
        sut.AddDock(second, new AnchorRight(first.Id));
        var third = DummyDock.New();
        sut.AddDock(third, new AnchorRight(second.Id));

        // Act
        sut.RemoveDock(first);
        sut.RemoveDock(third);

        // Assert
        _ = sut.Docks.Should().ContainInOrder(second);
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void RemoveDock_LastDockInGroup_RemovesGroupFromParent()
    {
        // Arrange
        var parent = new EmptyDockGroup();
        var sut = new NonEmptyDockGroup();
        parent.AddGroupFirst(sut, Orientation.Horizontal);
        _ = parent.First.Should().Be(sut);
        var first = sut.Docks.First();

        // Act
        sut.RemoveDock(first);

        // Assert
        _ = sut.Docks.Should().BeEmpty();
        _ = parent.First.Should().BeNull();
    }
}
