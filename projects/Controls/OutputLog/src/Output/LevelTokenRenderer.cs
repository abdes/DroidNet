// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls.OutputLog.Rendering;
using DroidNet.Controls.OutputLog.Theming;
using Microsoft.UI.Xaml.Documents;
using Serilog.Events;
using Serilog.Parsing;

namespace DroidNet.Controls.OutputLog.Output;

/// <summary>
/// Renders log event levels with themed styling and optional alignment.
/// </summary>
/// <param name="theme">The theme to apply to the rendered output.</param>
/// <param name="token">The property token representing the log level.</param>
/// <remarks>
/// <para>
/// This renderer transforms Serilog log levels into visually distinct themed text, making log severity
/// immediately apparent through consistent styling:
/// </para>
/// <para>
/// <strong>Severity Styling:</strong>
/// Each log level maps to a corresponding <see cref="ThemeStyle"/>:
/// Verbose → LevelVerbose (subtle styling)
/// Debug → LevelDebug (diagnostic information)
/// Information → LevelInformation (standard messages)
/// Warning → LevelWarning (potential issues)
/// Error → LevelError (actual problems)
/// Fatal → LevelFatal (critical failures).
/// </para>
/// <para>
/// Example usage:
/// <code><![CDATA[
/// var renderer = new LevelTokenRenderer(theme, token);
/// renderer.Render(logEvent, paragraph);
/// // Outputs: "ERROR" with appropriate styling
/// ]]></code>
/// </para>
/// </remarks>
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

    /// <summary>
    /// Renders a level token into the specified container with theming and alignment.
    /// </summary>
    /// <param name="logEvent">The log event containing the level information.</param>
    /// <param name="paragraph">The target paragraph for the rendered output.</param>
    /// <remarks>
    /// <para>
    /// The rendering process applies the following steps:
    /// </para>
    /// <para>
    /// 1. Obtains the level text from the token
    /// 2. Applies padding if alignment is specified
    /// 3. Creates a themed run with appropriate style
    /// 4. Adds the run to the paragraph.
    /// </para>
    /// </remarks>
    public override void Render(LogEvent logEvent, Paragraph paragraph)
    {
        var moniker = GetLevelMoniker(logEvent.Level, token.Format);
        var levelStyle = Levels.GetValueOrDefault(logEvent.Level, ThemeStyle.Invalid);

        using var styleContext = theme.Apply(paragraph, levelStyle);
        styleContext.Run.Text = PaddingTransform.Apply(moniker, token.Alignment);
    }

    /// <summary>
    /// Maps a log event level to its corresponding theme style.
    /// </summary>
    /// <param name="value">The log event level to be converted to a moniker.</param>
    /// <param name="format">The format string to apply to the log level.</param>
    /// <returns>The theme style corresponding to the severity level.</returns>
    /// <remarks>
    /// <para>
    /// Each log level maps to a specific theme style:
    /// </para>
    /// <para>
    /// <strong>Mappings:</strong>
    /// <see cref="LogEventLevel.Verbose"/> → <see cref="ThemeStyle.LevelVerbose"/>
    /// <see cref="LogEventLevel.Debug"/> → <see cref="ThemeStyle.LevelDebug"/>
    /// <see cref="LogEventLevel.Information"/> → <see cref="ThemeStyle.LevelInformation"/>
    /// <see cref="LogEventLevel.Warning"/> → <see cref="ThemeStyle.LevelWarning"/>
    /// <see cref="LogEventLevel.Error"/> → <see cref="ThemeStyle.LevelError"/>
    /// <see cref="LogEventLevel.Fatal"/> → <see cref="ThemeStyle.LevelFatal"/>.
    /// </para>
    /// </remarks>
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
