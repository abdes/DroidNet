// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(IDockable)}.Height")]
public class DockableHeightTests
{
    [TestMethod]
    public void Height_WithNumericValue()
    {
        // Arrange
        string? height = new Height(12.25);

        // Assert
        _ = height.Should().Be(double.Round(12.25).ToString(CultureInfo.InvariantCulture));
    }

    [TestMethod]
    [DataRow("1")]
    [DataRow("23.5")]
    [DataRow("*")]
    [DataRow("1*")]
    [DataRow("24*")]
    public void Height_WithValidStringValue(string value)
    {
        // Arrange
        string? height = new Height(value);

        // Assert
        _ = height.Should().Be(value);
    }

    [TestMethod]
    public void Height_WithNullValue()
    {
        // Arrange
        string? height = new Height();

        // Assert
        _ = height.Should().BeNull();
    }
}
