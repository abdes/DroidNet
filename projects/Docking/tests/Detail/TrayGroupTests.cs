// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Mocks;
using FluentAssertions;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TrayGroup))]
public class TrayGroupTests : IDisposable
{
    private readonly TrayGroup tray = new(AnchorPosition.Left);

    [TestCleanup]
    public void Dispose()
    {
        this.tray.Dispose();
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void Constructor_ShouldThrowException_WhenPositionIsWith()
    {
        // Act
        var act = () => _ = new TrayGroup(AnchorPosition.With);

        // Assert
        _ = act.Should().Throw<ArgumentException>().WithMessage("cannot use With for a TrayGroup*");
    }

    [TestMethod]
    [DataRow(AnchorPosition.Left, DockGroupOrientation.Vertical)]
    [DataRow(AnchorPosition.Right, DockGroupOrientation.Vertical)]
    [DataRow(AnchorPosition.Top, DockGroupOrientation.Horizontal)]
    [DataRow(AnchorPosition.Bottom, DockGroupOrientation.Horizontal)]
    public void Orientation_ShouldBeDeterminedFromPosition(AnchorPosition position, DockGroupOrientation orientation)
    {
        // Act
        using var sut = new TrayGroup(position);

        // Assert
        _ = sut.Orientation.Should().Be(orientation);
    }

    [TestMethod]
    public void IsEmpty_ShouldReturnTrue_WhenNoDocks() => this.tray.IsEmpty.Should().BeTrue();

    [TestMethod]
    public void First_ShouldAlwaysReturnNull() => this.tray.First.Should().BeNull();

    [TestMethod]
    public void Second_ShouldAlwaysReturnNull() => this.tray.Second.Should().BeNull();

    [TestMethod]
    public void AddDock_ShouldAddDockToDocks()
    {
        // Arrange
        var dock = new SimpleDock();

        // Act
        this.tray.AddDock(dock);

        // Assert
        _ = this.tray.Docks.Should().Contain(dock);
    }

    [TestMethod]
    public void RemoveDock_ShouldRemoveDockFromDocks()
    {
        // Arrange
        var dock = new SimpleDock();
        this.tray.AddDock(dock);

        // Act
        var result = this.tray.RemoveDock(dock);

        // Assert
        _ = result.Should().BeTrue();
        _ = this.tray.Docks.Should().NotContain(dock);
    }

    [TestMethod]
    public void ToString_ContainsTray()
    {
        // Act
        var result = this.tray.ToString();

        // Assert
        _ = result.Should().Contain("Tray");
    }
}
