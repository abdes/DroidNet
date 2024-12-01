// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Theming;
using FluentAssertions;
using Serilog.Events;

namespace DroidNet.Controls.Tests.Formatting;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ThemedDisplayValueFormatter))]
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
        _ = text.Should().Be(expected);
        _ = style.Should().Be(ThemeStyle.String);
    }
}
