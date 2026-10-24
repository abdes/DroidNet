// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     What the console shows: the projection's filter and folding settings.
/// </summary>
/// <param name="Levels">The levels shown.</param>
/// <param name="HiddenSubsystems">
///     The subsystems hidden; <see cref="OtherSubsystem" /> stands for entries without a subsystem.
/// </param>
/// <param name="Text">The trimmed search text; empty for none.</param>
/// <param name="Mode">Whether the search text hides non-matching lines.</param>
/// <param name="Fold">Whether consecutive identical lines fold into one row.</param>
internal sealed record OutputLogFilter(
    LevelMask Levels,
    IReadOnlySet<string> HiddenSubsystems,
    string Text,
    OutputSearchMode Mode,
    bool Fold)
{
    /// <summary>
    ///     The <see cref="HiddenSubsystems" /> key for entries without a subsystem.
    /// </summary>
    public const string OtherSubsystem = "";
}
