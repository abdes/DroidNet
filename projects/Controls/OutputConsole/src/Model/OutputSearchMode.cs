// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.OutputConsole.Model;

/// <summary>
///     What the OutputConsole search text does.
/// </summary>
public enum OutputSearchMode
{
    /// <summary>
    ///     Hide the lines that do not match.
    /// </summary>
    Filter = 0,

    /// <summary>
    ///     Keep every line, highlight the matches and move between them.
    /// </summary>
    Find = 1,
}
