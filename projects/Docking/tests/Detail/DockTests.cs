// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;
using FluentAssertions.Execution;
using Microsoft.VisualStudio.TestTools.UnitTesting;

/// <summary>
/// Unit test cases for the <see cref="Dock" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Dock))]
public sealed partial class DockTests : IDisposable
{
    private readonly SimpleDock dock = new();

    private readonly Dockable dockable1
        = Dockable.New("dockable1") ?? throw new AssertionFailedException("could not allocate a new object");

    private readonly Dockable dockable2
        = Dockable.New("dockable2") ?? throw new AssertionFailedException("could not allocate a new object");

    private bool dockable1Disposed;
    private bool dockable2Disposed;

    public DockTests()
    {
        this.dockable1.PropertyChanged += this.OnDockableDisposed;
        this.dockable2.PropertyChanged += this.OnDockableDisposed;
    }

    [TestCleanup]
    public void Dispose()
    {
        this.dockable1.PropertyChanged -= this.OnDockableDisposed;
        this.dockable1.Dispose();
        this.dockable2.PropertyChanged -= this.OnDockableDisposed;
        this.dockable2.Dispose();
        this.dock.Dispose();

        GC.SuppressFinalize(this);
    }

    [TestMethod]
    [DataRow(DockablePlacement.First)]
    [DataRow(DockablePlacement.Last)]
    [DataRow(DockablePlacement.AfterActiveItem)]
    [DataRow(DockablePlacement.BeforeActiveItem)]
    public void AddDockable_WhenDockIsEmpty(DockablePlacement placement)
    {
        // Act
        this.dock.AdoptDockable(this.dockable1, placement);

        // Assert
        _ = this.dock.Dockables.Should()
            .ContainSingle()
            .Which.Should()
            .Be(this.dockable1, "we added it to the dock");
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
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
        this.dock.AdoptDockable(this.dockable1);

        // Act
        this.dock.AdoptDockable(this.dockable2, placement);

        // Assert
        _ = this.dock.Dockables.Should().Contain(this.dockable2, "we added it to the dock");
        _ = this.dock.Dockables.IndexOf(this.dockable2).Should().Be(expectedIndex);
        _ = this.dock.ActiveDockable.Should().Be(this.dockable2);
        _ = this.dockable2.IsActive.Should().BeTrue("it has just been added to the dock");
    }

    [TestMethod]
    public void Dispose_ShouldDisposeAllDockables()
    {
        // Arrange
        this.dock.AdoptDockable(this.dockable1);
        this.dock.AdoptDockable(this.dockable2);

        // Act
        this.dock.Dispose();

        // Assert
        _ = this.dockable1Disposed.Should().BeTrue();
        _ = this.dockable2Disposed.Should().BeTrue();
    }

    [TestMethod]
    public void ToString_ShouldReturnDockId()
    {
        // Act
        var dockIdString = this.dock.ToString();

        // Assert
        _ = dockIdString.Should().Be(this.dock.Id.ToString(), "ToString should return the DockId");
    }

    [TestMethod]
    public void Width_WhenSet_SetsActiveDockableWidth()
    {
        // Arrange
        this.dockable1.PreferredWidth = new Width();
        this.dock.AdoptDockable(this.dockable1);
        _ = this.dockable1.IsActive.Should().BeTrue();
        _ = this.dockable1.PreferredWidth.IsNullOrEmpty.Should().BeTrue();
        var width = new Width(200);

        // Act
        this.dock.Width = width;

        // Assert
        _ = this.dock.Width.Should().Be(width);
        _ = this.dockable1.PreferredWidth.Should().Be(width);
    }

    [TestMethod]
    public void Height_WhenSet_SetsActiveDockableHeight()
    {
        // Arrange
        this.dockable1.PreferredHeight = new Height();
        this.dock.AdoptDockable(this.dockable1);
        _ = this.dockable1.IsActive.Should().BeTrue();
        _ = this.dockable1.PreferredHeight.IsNullOrEmpty.Should().BeTrue();
        var height = new Height(200);

        // Act
        this.dock.Height = height;

        // Assert
        _ = this.dock.Height.Should().Be(height);
        _ = this.dockable1.PreferredHeight.Should().Be(height);
    }

    [TestMethod]
    public void AddDockable_WhenNoWidth_And_DockableHasPreferredWidth_TakesThePreferredWidth()
    {
        // Arrange
        var width = new Width(200);
        this.dockable1.PreferredWidth = width;

        // Act
        this.dock.AdoptDockable(this.dockable1);

        // Assert
        _ = this.dock.Width.Should().Be(width);
    }

    [TestMethod]
    public void AddDockable_WhenNoHeight_And_DockableHasPreferredHeight_TakesThePreferredHeight()
    {
        // Arrange
        var height = new Height(200);
        this.dockable1.PreferredHeight = height;

        // Act
        this.dock.AdoptDockable(this.dockable1);

        // Assert
        _ = this.dock.Height.Should().Be(height);
    }

    [TestMethod]
    public void ActiveDockable_WhenDockIsEmpty_ReturnsNull() =>

        // Assert
        _ = this.dock.ActiveDockable.Should().BeNull();

    [TestMethod]
    public void ActiveDockable_WhenDockIsNotEmpty_ReturnsActiveDockable()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dock.AdoptDockable(this.dockable2);
        this.dockable1.IsActive = true;

        // Assert
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
    }

    [TestMethod]
    public void ActiveDockable_WhenDockIsNotEmpty_AlwaysHasAnActiveDockable()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dock.AdoptDockable(this.dockable2);
        this.dockable1.IsActive = false;
        this.dockable2.IsActive = false;

        // Assert
        _ = this.dock.ActiveDockable.Should().NotBeNull();
    }

    [TestMethod]
    public void ActiveDockable_WhenDockHasONlyOneDockable_ItIsAlwaysActive()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dockable1.IsActive = false;

        // Assert
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
    }

    [TestMethod]
    public void Dock_OnlySubscribesToIsActiveChanges()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dockable1.Title = "Hello";

        // Assert
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
    }

    private void OnDockableDisposed(object? sender, EventArgs args)
    {
        if (sender == this.dockable1)
        {
            this.dockable1Disposed = true;
        }
        else if (sender == this.dockable2)
        {
            this.dockable2Disposed = true;
        }
    }
}
