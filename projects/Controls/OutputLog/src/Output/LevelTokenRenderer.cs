// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Output;

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

internal sealed class LevelTokenRenderer(Theme theme, PropertyToken token) : TokenRenderer
{
    private static readonly Dictionary<LogEventLevel, ThemeStyle> Levels = new()
    {
        { LogEventLevel.Verbose, ThemeStyle.LevelVerbose },
        { LogEventLevel.Debug, ThemeStyle.LevelDebug },
        { LogEventLevel.Information, ThemeStyle.LevelInformation },
        { LogEventLevel.Warning, ThemeStyle.LevelWarning },
        { LogEventLevel.Error, ThemeStyle.LevelError },
        { LogEventLevel.Fatal, ThemeStyle.LevelFatal },
    };

    private static readonly string[][] TitleCaseLevelMap =
    [
        ["V", "Vb", "Vrb", "Verb"],
        ["D", "De", "Dbg", "Dbug"],
        ["I", "In", "Inf", "Info"],
        ["W", "Wn", "Wrn", "Warn"],
        ["E", "Er", "Err", "Eror"],
        ["F", "Fa", "Ftl", "Fatl"],
    ];

    private static readonly string[][] LowercaseLevelMap =
    [
        ["v", "vb", "vrb", "verb"],
        ["d", "de", "dbg", "dbug"],
        ["i", "in", "inf", "info"],
        ["w", "wn", "wrn", "warn"],
        ["e", "er", "err", "eror"],
        ["f", "fa", "ftl", "fatl"],
    ];

    private static readonly string[][] UppercaseLevelMap =
    [
        ["V", "VB", "VRB", "VERB"],
        ["D", "DE", "DBG", "DBUG"],
        ["I", "IN", "INF", "INFO"],
        ["W", "WN", "WRN", "WARN"],
        ["E", "ER", "ERR", "EROR"],
        ["F", "FA", "FTL", "FATL"],
    ];

    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        var moniker = GetLevelMoniker(logEvent.Level, token.Format);
        var levelStyle = Levels.GetValueOrDefault(logEvent.Level, ThemeStyle.Invalid);

        using var styleContext = theme.Apply(paragraph, levelStyle);
        styleContext.Run.Text = PaddingTransform.Apply(moniker, token.Alignment);
    }

    private static string GetLevelMoniker(LogEventLevel value, string? format = default)
    {
        if (format is null || (format.Length != 2 && format.Length != 3))
        {
            return CasingTransform.Apply(value.ToString(), format);
        }

        // Using int.Parse() here requires allocating a string to exclude the first character prefix.
        // Junk like "wxy" will be accepted but produce benign results.
        var width = format[1] - '0';
        if (format.Length == 3)
        {
            width *= 10;
            width += format[2] - '0';
        }

        if (width < 1)
        {
            return string.Empty;
        }

        if (width > 4)
        {
            var stringValue = value.ToString();
            if (stringValue.Length > width)
            {
                stringValue = stringValue[..width];
            }

            return CasingTransform.Apply(stringValue);
        }

        var index = (int)value;
        if (index is >= 0 and <= (int)LogEventLevel.Fatal)
        {
            switch (format[0])
            {
                case 'w':
                    return LowercaseLevelMap[index][width - 1];
                case 'u':
                    return UppercaseLevelMap[index][width - 1];
                case 't':
                    return TitleCaseLevelMap[index][width - 1];

                default:
                    // Fall through to long format case transform
                    break;
            }
        }

        return CasingTransform.Apply(value.ToString(), format);
    }
}
