// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using AwesomeAssertions.Execution;
using DroidNet.Docking.Detail;
using DroidNet.Docking.Tests.Mocks;

namespace DroidNet.Docking.Tests.Detail;

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

    /// <summary>
    /// Initializes a new instance of the <see cref="DockTests"/> class.
    /// </summary>
    public DockTests()
    {
        this.dockable1.PropertyChanged += this.OnDockableDisposed;
        this.dockable2.PropertyChanged += this.OnDockableDisposed;
    }

    /// <inheritdoc/>
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
    public void AddDockableWhenDockIsEmpty(DockablePlacement placement)
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
    public void AddDockableWhenDockIsNotEmpty(DockablePlacement placement, int expectedIndex)
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
    public void DisposeShouldDisposeAllDockables()
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
    public void ToStringShouldReturnDockId()
    {
        // Act
        var dockIdString = this.dock.ToString();

        // Assert
        _ = dockIdString.Should().Be(this.dock.Id.ToString(), "ToString should return the DockId");
    }

    [TestMethod]
    public void WidthWhenSetSetsActiveDockableWidth()
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
    public void HeightWhenSetSetsActiveDockableHeight()
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
    public void AddDockableWhenNoWidthAndDockableHasPreferredWidthTakesThePreferredWidth()
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
    public void AddDockableWhenNoHeightAndDockableHasPreferredHeightTakesThePreferredHeight()
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
    public void ActiveDockableWhenDockIsEmptyReturnsNull() =>

        // Assert
        _ = this.dock.ActiveDockable.Should().BeNull();

    [TestMethod]
    public void ActiveDockableWhenDockIsNotEmptyReturnsActiveDockable()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dock.AdoptDockable(this.dockable2);
        this.dockable1.IsActive = true;

        // Assert
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
    }

    [TestMethod]
    public void ActiveDockableWhenDockIsNotEmptyAlwaysHasAnActiveDockable()
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
    public void ActiveDockableWhenDockHasONlyOneDockableItIsAlwaysActive()
    {
        // Setup
        this.dock.AdoptDockable(this.dockable1);
        this.dockable1.IsActive = false;

        // Assert
        _ = this.dock.ActiveDockable.Should().Be(this.dockable1);
    }

    [TestMethod]
    public void DockOnlySubscribesToIsActiveChanges()
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
