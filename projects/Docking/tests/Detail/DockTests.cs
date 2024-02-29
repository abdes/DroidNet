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
    private readonly Dockable dockable1
        = Dockable.New("dockable1") ?? throw new AssertionFailedException("could not allocate a new object");

    private readonly Dockable dockable2
        = Dockable.New("dockable2") ?? throw new AssertionFailedException("could not allocate a new object");

    [TestCleanup]
    public void Dispose()
    {
        this.dockable1.Dispose();
        this.dockable2.Dispose();
    }

    [TestMethod]
    [DataRow(DockablePlacement.First)]
    [DataRow(DockablePlacement.Last)]
    [DataRow(DockablePlacement.AfterActiveItem)]
    [DataRow(DockablePlacement.BeforeActiveItem)]
    public void AddDockable_WhenDockIsEmpty(DockablePlacement placement)
    {
        // Arrange
        using var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");

        // Act
        dock.AddDockable(this.dockable1, placement);

        // Assert
        _ = dock.Dockables.Should()
            .ContainSingle()
            .Which.Should()
            .Be(this.dockable1, "we added it to the dock");
        _ = dock.ActiveDockable.Should().Be(this.dockable1);
        _ = this.dockable1.IsActive.Should().BeTrue("it has just been added to the dock");
    }

    [TestMethod]
    [DataRow(DockablePlacement.First, 0)]
    [DataRow(DockablePlacement.Last, 1)]
    [DataRow(DockablePlacement.AfterActiveItem, 1)]
    [DataRow(DockablePlacement.BeforeActiveItem, 0)]
    public void AddDockable_WhenDockIsNotEmpty(DockablePlacement placement, int expectedIndex)
    {
        // Arrange
        using var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");
        dock.AddDockable(this.dockable1);

        // Act
        dock.AddDockable(this.dockable2, placement);

        // Assert
        _ = dock.Dockables.Should().Contain(this.dockable2, "we added it to the dock");
        _ = dock.Dockables.IndexOf(this.dockable2).Should().Be(expectedIndex);
        _ = dock.ActiveDockable.Should().Be(this.dockable2);
        _ = this.dockable2.IsActive.Should().BeTrue("it has just been added to the dock");
    }

    [TestMethod]
    public void Dispose_ShouldDisposeAllDockablesAndClearDockables()
    {
        // Arrange
        using var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");
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
        using var dock = new SimpleDock() ?? throw new AssertionFailedException("could not allocate a new object");

        // Act
        var dockIdString = dock.ToString();

        // Assert
        _ = dockIdString.Should().Be(dock.Id.ToString(), "ToString should return the DockId");
    }
}
