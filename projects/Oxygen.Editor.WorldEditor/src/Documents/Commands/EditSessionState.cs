// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable IDE0130 // Authoring commands use the established WorldEditor namespace across this assembly.

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// State of an inspector edit session.
/// </summary>
public enum EditSessionState
{
    /// <summary>
    /// The session is open.
    /// </summary>
    Open,

    /// <summary>
    /// The session was committed.
    /// </summary>
    Committed,

    /// <summary>
    /// The session was cancelled.
    /// </summary>
    Cancelled,
}
