// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;

namespace DroidNet.Controls.OutputConsole;

/// <summary>
///     The layout every row of a console shares: the prefix <c>[18:13:56.503 INF (Router)]</c> in one fixed-width
///     column, sized for the longest prefix, then one space, then the message. A new instance is created whenever an
///     option or the longest subsystem changes, so rows can skip re-applying an unchanged layout by reference.
/// </summary>
/// <param name="CharWidth">The advance width of one character of the console's monospace font.</param>
/// <param name="ShowTimestamps">Whether the prefix starts with the time.</param>
/// <param name="ShowSubsystem">Whether the prefix ends with the subsystem.</param>
/// <param name="SubsystemChars">The longest subsystem name shown, capped at <see cref="MaxSubsystemChars" />.</param>
/// <param name="WordWrap">Whether messages wrap.</param>
internal sealed record ConsoleColumns(
    double CharWidth,
    bool ShowTimestamps,
    bool ShowSubsystem,
    int SubsystemChars,
    bool WordWrap)
{
    /// <summary>
    ///     The longest subsystem name shown; a longer one is cut and ends with an ellipsis.
    /// </summary>
    public const int MaxSubsystemChars = 14;

    // "[" and "]", the three-letter level, "HH:mm:ss.fff " and " (" + name + ")".
    private const int BracketChars = 2;
    private const int LevelChars = 3;
    private const int TimeChars = 13;
    private const int SubsystemDecorationChars = 3;

    /// <summary>
    ///     Gets the number of characters of the longest prefix.
    /// </summary>
    public int PrefixChars
        => BracketChars
            + (this.ShowTimestamps ? TimeChars : 0)
            + LevelChars
            + (this.ShowSubsystem && this.SubsystemChars > 0 ? SubsystemDecorationChars + this.SubsystemChars : 0);

    /// <summary>
    ///     Gets the width of the prefix column: the longest prefix and the space before the message.
    /// </summary>
    public double PrefixWidth => (this.PrefixChars + 1) * this.CharWidth;

    /// <summary>
    ///     Gets an entry's time as the console shows it: <c>18:13:56.503</c>.
    /// </summary>
    /// <param name="timestamp">The entry's time.</param>
    /// <returns>The time.</returns>
    public static string Time(DateTimeOffset timestamp)
        => timestamp.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);

    /// <summary>
    ///     Gets the time part of a prefix, with its trailing space: <c>18:13:56.503 </c>.
    /// </summary>
    /// <param name="timestamp">The entry's time.</param>
    /// <returns>The time part.</returns>
    public static string TimePart(DateTimeOffset timestamp) => Time(timestamp) + " ";

    /// <summary>
    ///     Gets the subsystem part of a prefix, with its leading space: <c> (Router)</c>; empty without a subsystem.
    /// </summary>
    /// <param name="subsystem">The subsystem name.</param>
    /// <returns>The subsystem part.</returns>
    public static string SubsystemPart(string? subsystem)
        => subsystem is null ? string.Empty : " (" + Fit(subsystem) + ")";

    /// <summary>
    ///     Gets the number of characters a subsystem name takes in a prefix.
    /// </summary>
    /// <param name="subsystem">The subsystem name.</param>
    /// <returns>The shown length; <c>0</c> for <see langword="null" />.</returns>
    public static int ShownNameChars(string? subsystem)
        => subsystem is null ? 0 : Math.Min(subsystem.Length, MaxSubsystemChars);

    private static string Fit(string subsystem)
        => subsystem.Length <= MaxSubsystemChars ? subsystem : $"{subsystem.AsSpan(0, MaxSubsystemChars - 1)}…";
}
