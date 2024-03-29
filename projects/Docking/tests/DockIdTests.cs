// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(DockId))]
public class DockIdTests
{
    [TestMethod]
    public void Equals_GivenSameValue_ShouldReturnTrue()
    {
        // Arrange
        var dockId1 = new DockId(1);
        var dockId2 = new DockId(1);

        // Act
        var isEqual = dockId1.Equals(dockId2);

        // Assert
        _ = isEqual.Should().BeTrue("DockIds with the same value should be equal");
    }

    [TestMethod]
    public void Equals_GivenDifferentValue_ShouldReturnFalse()
    {
        // Arrange
        var dockId1 = new DockId(1);
        var dockId2 = new DockId(2);

        // Act
        var isEqual = dockId1.Equals(dockId2);

        // Assert
        _ = isEqual.Should().BeFalse("DockIds with different values should not be equal");
    }

    [TestMethod]
    public void GetHashCode_GivenDockId_ShouldReturnItsValue()
    {
        // Arrange
        var dockId = new DockId { Value = 1 };

        // Act
        var hashCode = dockId.GetHashCode();

        // Assert
        _ = hashCode.Should().Be(1, "The hash code of a DockId should be its value");
    }

    [TestMethod]
    public void ToString_GivenDockId_ShouldReturnStringValueOfItsValue()
    {
        // Arrange
        var dockId = new DockId(1);

        // Act
        var dockIdString = dockId.ToString();

        // Assert
        _ = dockIdString.Should()
            .Be("1", "The string representation of a DockId should be the string value of its value");
    }
}
