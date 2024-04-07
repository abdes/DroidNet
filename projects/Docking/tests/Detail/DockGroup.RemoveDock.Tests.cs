// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using DroidNet.Docking.Mocks;
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
            using var sut = new NonEmptyDockGroup(this.docker);
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
        using var sut = new NonEmptyDockGroup(this.docker);
        using var first = sut.Docks[0];
        first.AdoptDockable(Dockable.New("first"));
        using var second = DummyDock.New();
        second.AdoptDockable(Dockable.New("second"));
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
    public void RemoveDock_InvokesOptimizerIfImplemented()
    {
        // Arrange
        var optimizingDocker = new DummyOptimizingDocker();
        using var parent = new DockGroup(optimizingDocker);

        using var sut = new DockGroup(optimizingDocker);
        sut.AddDock(new SimpleDock());
        parent.AddGroupFirst(sut, DockGroupOrientation.Horizontal);

        // Act
        optimizingDocker.Reset();
        sut.RemoveDock(sut.Docks[0]);

        // Assert
        _ = optimizingDocker.ConsolidateUpCalled.Should().BeTrue();
    }
}
