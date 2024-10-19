// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Tests.Formatting;

using DroidNet.Controls.OutputLog.Formatting;
using DroidNet.Controls.OutputLog.Theming;
using FluentAssertions;
using Microsoft.UI.Xaml.Documents;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using Microsoft.VisualStudio.TestTools.UnitTesting.AppContainer;
using Serilog.Events;

[TestClass]
public class ThemedDisplayValueFormatterTests
{
    private ThemedDisplayValueFormatter sut = new(Theme.None, formatProvider: null);
    private Paragraph paragraph = new();

    [UITestMethod]
    public void StringFormattingDefault()
    {
        var value = "Hello";

        this.sut.FormatLiteralValue(new ScalarValue(value), this.paragraph, format: null);

        this.paragraph.Inlines.Count.Should().Be(1);
    }
}
