// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using FluentAssertions;

namespace DroidNet.Controls.Tests;

[TestClass]
public class MaskParserTests
{
    [TestMethod]
    [DataRow("~.#", 1.0f, "1.0")]
    [DataRow("~.##", 11.0f, "11.00")]
    [DataRow("~.#", 11.0f, "11.0")]
    [DataRow("~.#", 111.0f, "111.0")]
    [DataRow("~.##", -1111111.0f, "-1111111.00")]
    [DataRow("~.###", 0.1f, ".100")]
    public void WhenUnboundedShowsAllDigitsBeforeDecimal(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("#.#", 123.0f, "9.9")]
    [DataRow("##.#", 123.0f, "99.9")]
    [DataRow("###.#", 123.0f, "123.0")]
    [DataRow("-#.#", -123.0f, "-9.9")]
    [DataRow("-##.#", -123.0f, "-99.9")]
    [DataRow("-###.#", -123.0f, "-123.0")]
    public void WhenMaskHasHashesLimitsMaximumValue(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("~.# m", 1.0f, "1.0 m")]
    [DataRow("~.## km", 1.0f, "1.00 km")]
    [DataRow("~.### mph", -1.0f, "-1.000 mph")]
    public void WhenMaskHasUnitAppendsUnitToValue(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("~.#m", 1.0f, "1.0m")]
    [DataRow("~.# m", 1.0f, "1.0 m")]
    [DataRow("~.## m", 1.0f, "1.00 m")]
    [DataRow("~.## m", -1.0f, "-1.00 m")]
    public void WhenMaskHasSpaceAddsSpaceBeforeUnit(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("invalid")]
    [DataRow("")]
    [DataRow("###")]
    [DataRow(".")]
    public void WhenMaskIsInvalidThrowsArgumentException(string mask)
    {
        // Arrange & Act
        Action act = () => _ = new MaskParser(mask);

        // Assert
        _ = act.Should().Throw<ArgumentException>()
            .WithMessage("Invalid mask format*");
    }

    [TestMethod]
    [DataRow("#.#", 1.0f, "1.0")]
    [DataRow("#.##", 1.234f, "1.23")]
    [DataRow("#.###", 1.2345f, "1.235")]
    public void WhenFormattingRoundsToAfterDecimalCount(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow(".#", 0.0f, "0.0")]
    [DataRow(".##", 0.0f, "0.00")]
    [DataRow("#.#", 0.0f, "0.0")]
    [DataRow("###.#", 0.0f, "0.0")]
    [DataRow("#.##", 0.0f, "0.00")]
    [DataRow("#.###", 0.0f, "0.000")]
    [DataRow("~.#", 0.0f, "0.0")]
    [DataRow("~.", 0.0f, "0")]
    public void WhenFormattingZeroHandlesProperly(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("##.#", 1.0f, "01.0", true)]
    [DataRow("##.##", 1.0f, "01.00", true)]
    [DataRow("##.###", 1.0f, "01.000", true)]
    public void WhenFormattingWithPaddingAddsZeros(string mask, float value, string expected, bool withPadding)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value, withPadding);

        // Assert
        _ = result.Should().Be(expected);
    }

    [TestMethod]
    [DataRow("##.", 1.0f, "1")]
    [DataRow("#.", 1.234f, "1")]
    [DataRow("#.", 1.8f, "2")]
    [DataRow("~.", -128.45f, "-128")]
    public void WhenMaskHasNoDigitsAfterDecimal(string mask, float value, string expected)
    {
        // Arrange
        var parser = new MaskParser(mask);

        // Act
        var result = parser.FormatValue(value);

        // Assert
        _ = result.Should().Be(expected);
    }
}
