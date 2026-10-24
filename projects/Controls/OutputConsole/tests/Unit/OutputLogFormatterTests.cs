// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;
using Serilog.Events;

namespace Controls.OutputConsole.Tests;

[TestClass]
public class OutputLogFormatterTests
{
    private static readonly DateTimeOffset Stamp = new(2026, 10, 24, 11, 32, 5, 840, TimeSpan.FromHours(2));

    [TestMethod]
    public void AppendLine_WithSubsystem_IncludesSubsystemTag()
    {
        var entry = new OutputLogEntry { Timestamp = Stamp, Level = LogEventLevel.Error, Message = "Boom", Subsystem = "Cooker" };
        var builder = new StringBuilder();

        OutputLogFormatter.AppendLine(builder, entry);

        _ = builder.ToString().Should().Be("[2026-10-24 11:32:05.840 ERR (Cooker)] Boom" + Environment.NewLine);
    }

    [TestMethod]
    public void AppendLine_WithoutSubsystem_OmitsSubsystemTag()
    {
        var entry = new OutputLogEntry { Timestamp = Stamp, Level = LogEventLevel.Information, Message = "Hello" };
        var builder = new StringBuilder();

        OutputLogFormatter.AppendLine(builder, entry);

        _ = builder.ToString().Should().Be("[2026-10-24 11:32:05.840 INF] Hello" + Environment.NewLine);
    }

    [TestMethod]
    public void AppendLine_WithException_IndentsEachExceptionLineByTwoSpaces()
    {
        var entry = new OutputLogEntry
        {
            Timestamp = Stamp,
            Level = LogEventLevel.Error,
            Message = "Failed",
            Exception = new InvalidOperationException("bad"),
        };
        var lines = entry.ExceptionText!.Split(["\r\n", "\n"], StringSplitOptions.None);
        var builder = new StringBuilder();

        OutputLogFormatter.AppendLine(builder, entry);

        var expected = "[2026-10-24 11:32:05.840 ERR] Failed" + Environment.NewLine
            + string.Concat(lines.Select(line => "  " + line + Environment.NewLine));
        _ = builder.ToString().Should().Be(expected);
        _ = lines[0].Should().Contain("bad");
    }

    [TestMethod]
    [DataRow(LogEventLevel.Verbose, "VRB")]
    [DataRow(LogEventLevel.Debug, "DBG")]
    [DataRow(LogEventLevel.Information, "INF")]
    [DataRow(LogEventLevel.Warning, "WRN")]
    [DataRow(LogEventLevel.Error, "ERR")]
    [DataRow(LogEventLevel.Fatal, "FTL")]
    public void LevelTag_ReturnsThreeLetterTag(LogEventLevel level, string expected)
        => _ = OutputLogFormatter.LevelTag(level).Should().Be(expected);

    [TestMethod]
    public void Format_WithTwoEntries_KeepsOrder()
    {
        var first = new OutputLogEntry { Timestamp = Stamp, Level = LogEventLevel.Debug, Message = "first" };
        var second = new OutputLogEntry { Timestamp = Stamp, Level = LogEventLevel.Warning, Message = "second" };

        var text = OutputLogFormatter.Format([first, second]);

        _ = text.Should().Be(
            "[2026-10-24 11:32:05.840 DBG] first" + Environment.NewLine
            + "[2026-10-24 11:32:05.840 WRN] second" + Environment.NewLine);
    }

    [TestMethod]
    public void AppendDetails_OrdersHeaderSourceChannelPropertiesThenException()
    {
        var entry = new OutputLogEntry
        {
            Timestamp = Stamp,
            Level = LogEventLevel.Error,
            Message = "Failed",
            Source = "Oxygen.Editor.Cooker",
            Channel = "Cook",
            Subsystem = "Cooker",
            Exception = new InvalidOperationException("bad"),
            Properties = new Dictionary<string, object?>(StringComparer.Ordinal)
            {
                ["Zeta"] = 1,
                ["SourceContext"] = "Oxygen.Editor.Cooker",
                ["Channel"] = "Cook",
                ["Alpha"] = "x",
            },
        };
        var exceptionLines = entry.ExceptionText!.Split(["\r\n", "\n"], StringSplitOptions.None);
        var builder = new StringBuilder();

        OutputLogFormatter.AppendDetails(builder, entry);

        var expected = "[2026-10-24 11:32:05.840 ERR (Cooker)] Failed" + Environment.NewLine
            + "Source: Oxygen.Editor.Cooker" + Environment.NewLine
            + "Channel: Cook" + Environment.NewLine
            + "Properties:" + Environment.NewLine
            + "  Alpha = \"x\"" + Environment.NewLine
            + "  Zeta = 1" + Environment.NewLine
            + string.Concat(exceptionLines.Select(line => "  " + line + Environment.NewLine));
        _ = builder.ToString().Should().Be(expected);
    }

    [TestMethod]
    public void FormatValue_Null_ReturnsNull() => _ = OutputLogFormatter.FormatValue(value: null).Should().Be("null");

    [TestMethod]
    public void FormatValue_String_IsQuoted() => _ = OutputLogFormatter.FormatValue("a").Should().Be("\"a\"");

    [TestMethod]
    public void FormatValue_Bool_IsLowercase()
    {
        _ = OutputLogFormatter.FormatValue(value: true).Should().Be("true");
        _ = OutputLogFormatter.FormatValue(value: false).Should().Be("false");
    }

    [TestMethod]
    public void FormatValue_Double_UsesInvariantCulture()
    {
        var previous = Thread.CurrentThread.CurrentCulture;
        try
        {
            Thread.CurrentThread.CurrentCulture = new System.Globalization.CultureInfo("de-DE");

            _ = OutputLogFormatter.FormatValue(1234.5).Should().Be("1234.5");
        }
        finally
        {
            Thread.CurrentThread.CurrentCulture = previous;
        }
    }

    [TestMethod]
    public void FormatValue_Sequence_ReturnsBracketedList()
        => _ = OutputLogFormatter.FormatValue(new object?[] { 1, "a" }).Should().Be("[1, \"a\"]");

    [TestMethod]
    public void FormatValue_Dictionary_ReturnsBracedPairsInInsertionOrder()
    {
        var map = new Dictionary<string, object?>(StringComparer.Ordinal) { ["Id"] = 0, ["Name"] = "x" };

        _ = OutputLogFormatter.FormatValue(map).Should().Be("{ Id: 0, Name: \"x\" }");
    }

    [TestMethod]
    public void FormatValue_EmptyDictionary_ReturnsEmptyBraces()
        => _ = OutputLogFormatter.FormatValue(new Dictionary<string, object?>(StringComparer.Ordinal)).Should().Be("{ }");
}
