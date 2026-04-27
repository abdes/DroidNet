// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Completion kind for an interactive numeric edit.
/// </summary>
public enum NumberBoxEditCompletionKind
{
    /// <summary>
    ///     The edit was committed.
    /// </summary>
    Commit,

    /// <summary>
    ///     The edit was cancelled.
    /// </summary>
    Cancel,
}
