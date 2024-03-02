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
        var action = () =>
        {
            // Arrange
            using var sut = new NonEmptyDockGroup();
            using var newDock = DummyDock.New();

            // Act
            sut.RemoveDock(newDock);
        };

        // Assert
        _ = action.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    [TestCategory($"{nameof(DockGroup)}.DockManagement")]
    public void RemoveDock_AmongMany_JustRemovesIt()
    {
        // Arrange
        using var sut = new NonEmptyDockGroup();
        using var first = sut.Docks.First();
        first.AddDockable(Dockable.New("first"));
        using var second = DummyDock.New();
        second.AddDockable(Dockable.New("second"));
        sut.AddDock(second, new AnchorRight(first.Dockables[0]));
        using var third = DummyDock.New();
        sut.AddDock(third, new AnchorRight(second.Dockables[0]));

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
        using var parent = new EmptyDockGroup();
        using var keep = new MockDockGroup();
        parent.AddGroupLast(keep, DockGroupOrientation.Horizontal);
        using var sut = new NonEmptyDockGroup();
        parent.AddGroupFirst(sut, DockGroupOrientation.Horizontal);
        _ = parent.First.Should().Be(sut);
        using var first = sut.Docks.First();

        // Act
        sut.RemoveDock(first);

        // Assert
        _ = sut.Docks.Should().BeEmpty();
        _ = parent.First.Should().BeNull();
    }
}
