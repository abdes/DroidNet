// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using AwesomeAssertions;

namespace DroidNet.Docking.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory($"{nameof(IDockable)}.Width")]
public class DockableWidthTests
{
    [TestMethod]
    public void Width_WithNumericValue()
    {
        // Arrange
        string? width = new Width(10.56);

        // Assert
        _ = width.Should().Be(double.Round(10.56).ToString(CultureInfo.InvariantCulture));
    }

    [TestMethod]
    [DataRow("1")]
    [DataRow("23.5")]
    [DataRow("*")]
    [DataRow("1*")]
    [DataRow("24*")]
    public void Width_WithValidStringValue(string value)
    {
        // Arrange
        string? width = new Width(value);

        // Assert
        _ = width.Should().Be(value);
    }

    [TestMethod]
    public void Width_WithNullValue()
    {
        // Arrange
        string? width = new Width();

        // Assert
        _ = width.Should().BeNull();
    }
}
