// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking;
using DroidNet.Docking.Mocks;
using DroidNet.Docking.Workspace;
using FluentAssertions;

/// <summary>
/// Unit test cases for the <see cref="TrayGroup" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TrayGroup))]
public partial class TrayGroupTests : IDisposable
{
    private readonly DummyDocker docker = new();
    private readonly TrayGroup tray;

    public TrayGroupTests() => this.tray = new TrayGroup(this.docker, AnchorPosition.Left);

    [TestCleanup]
    public void Dispose()
    {
        this.docker.Dispose();
        GC.SuppressFinalize(this);
    }

    [TestMethod]
    public void Constructor_ShouldThrowException_WhenPositionIsWith()
    {
        // Act
        var act = () => _ = new TrayGroup(this.docker, AnchorPosition.With);

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
        var sut = new TrayGroup(new DummyDocker(), position);

        // Assert
        _ = sut.Orientation.Should().Be(orientation);
    }

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
