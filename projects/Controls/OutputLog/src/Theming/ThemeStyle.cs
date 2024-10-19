// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputLog.Theming;

/// <summary>
/// Elements styled by a theme.
/// </summary>
public enum ThemeStyle
{
    /// <summary>
    /// Prominent text, generally content within an event's message.
    /// </summary>
    Text = 0,

    /// <summary>
    /// Boilerplate text, for example items specified in an output template.
    /// </summary>
    SecondaryText = 1,

    /// <summary>
    /// De-emphasized text, for example literal text in output templates and
    /// punctuation used when writing structured data.
    /// </summary>
    TertiaryText = 2,

    /// <summary>
    /// Output demonstrating some kind of configuration issue, e.g. an invalid
    /// message template token.
    /// </summary>
    Invalid = 3,

    /// <summary>
    /// The built-in <see langword="null" /> value.
    /// </summary>
    Null = 4,

    /// <summary>
    /// Property and type names.
    /// </summary>
    Name = 5,

    /// <summary>
    /// Strings.
    /// </summary>
#pragma warning disable CA1720
    String = 6,
#pragma warning restore CA1720

    /// <summary>
    /// Numbers.
    /// </summary>
    Number = 7,

    /// <summary>
    /// <see cref="bool" /> values.
    /// </summary>
    Boolean = 8,

    /// <summary>
    /// All other scalar values, e.g. <see cref="Guid" /> instances.
    /// </summary>
    Scalar = 9,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelVerbose = 10,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelDebug = 11,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelInformation = 12,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelWarning = 13,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelError = 14,

    /// <summary>
    /// Level indicator.
    /// </summary>
    LevelFatal = 15,
}
