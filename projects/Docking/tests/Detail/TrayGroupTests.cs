// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Docking.Tests.Mocks;
using DroidNet.Docking.Workspace;
using AwesomeAssertions;

namespace DroidNet.Docking.Tests.Detail;
#pragma warning disable CA2000 // Dispose objects before losing scope

/// <summary>
/// Unit test cases for the <see cref="TrayGroup" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(TrayGroup))]
public sealed partial class TrayGroupTests : IDisposable
{
    private readonly DummyDocker docker = new();
    private readonly TrayGroup tray;

    public TrayGroupTests()
    {
        this.tray = new TrayGroup(this.docker, AnchorPosition.Left);
    }

    /// <inheritdoc/>
    [TestCleanup]
    public void Dispose() => this.docker.Dispose();

    [TestMethod]
    public void ConstructorShouldThrowExceptionWhenPositionIsWith()
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
    public void OrientationShouldBeDeterminedFromPosition(AnchorPosition position, DockGroupOrientation orientation)
    {
        // Act
        var sut = new TrayGroup(this.docker, position);

        // Assert
        _ = sut.Orientation.Should().Be(orientation);
    }

    [TestMethod]
    public void AddDockShouldAddDockToDocks()
    {
        // Arrange
        var dock = new SimpleDock();

        // Act
        this.tray.AddDock(dock);

        // Assert
        _ = this.tray.Docks.Should().Contain(dock);
    }

    [TestMethod]
    public void RemoveDockShouldRemoveDockFromDocks()
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
    public void ToStringContainsTray()
    {
        // Act
        var result = this.tray.ToString();

        // Assert
        _ = result.Should().Contain("Tray");
    }
}
#pragma warning restore CA2000 // Dispose objects before losing scope
