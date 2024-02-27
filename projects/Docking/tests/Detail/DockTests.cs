// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using FluentAssertions.Execution;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(Dock)}")]
public class DockTests
{
    private readonly IDockable dockable1
        = new MockDockable("dockable1") ?? throw new AssertionFailedException("could not allocate a new object");

    private readonly IDockable dockable2
        = new MockDockable("dockable2") ?? throw new AssertionFailedException("could not allocate a new object");

    [TestMethod]
    public void AddDockable_WhenDockIsEmpty_ShouldAddDockableAndSetActive()
    {
        // Arrange
        var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");

        // Act
        dock.AddDockable(this.dockable1);

        // Assert
        _ = dock.Dockables.Should()
            .ContainSingle()
            .Which.Should()
            .Be(this.dockable1, "we added it to the dock");
        _ = this.dockable1.IsActive.Should().BeTrue("it is the only dockable in the dock");
    }

    [TestMethod]
    public void AddDockable_WhenDockIsNotEmpty_ShouldAddDockableAndNotSetActive()
    {
        // Arrange
        var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");
        dock.AddDockable(this.dockable1);

        // Act
        dock.AddDockable(this.dockable2);

        // Assert
        _ = dock.Dockables.Should().Contain(this.dockable2, "we added it to the dock");
        _ = this.dockable2.IsActive.Should().BeFalse("it is not the first dockable in the dock");
    }

    [TestMethod]
    public void Dispose_ShouldDisposeAllDockablesAndClearDockables()
    {
        // Arrange
        var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");
        dock.AddDockable(this.dockable1);
        dock.AddDockable(this.dockable2);

        // Act
        dock.Dispose();

        // Assert
        _ = dock.Dockables.Should().BeEmpty("all dockables should be disposed and removed from the dock");
    }

    [TestMethod]
    public void ToString_ShouldReturnDockId()
    {
        // Arrange
        var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");

        // Act
        var dockIdString = dock.ToString();

        // Assert
        _ = dockIdString.Should().Be(dock.Id.ToString(), "ToString should return the DockId");
    }
}
