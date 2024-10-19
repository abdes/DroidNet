// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests.Formatting;

using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Theming;
using FluentAssertions;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Serilog.Events;

[TestClass]
public class ThemedDisplayValueFormatterTests
{
    private readonly ThemedDisplayValueFormatter sut = new(Theme.None, formatProvider: null);

    [TestMethod]
    [DataRow("Hello", "l", "Hello")]
    [DataRow("Hello", "", "\"Hello\"")]
    [DataRow("Hello", null, "\"Hello\"")]
    public void StringFormatting(string value, string? format, string expected)
    {
        // Arrange

        // Act
        var (text, style) = this.sut.FormatLiteralValue(new ScalarValue(value), format);

        // Assert
        text.Should().Be(expected);
        style.Should().Be(ThemeStyle.String);
    }
}
