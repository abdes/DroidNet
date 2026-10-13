// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// Severity of a script problem.
/// </summary>
public enum ScriptProblemSeverity
{
    /// <summary>The script cannot run or cook as written.</summary>
    Error,

    /// <summary>The script runs, but probably not as intended.</summary>
    Warning,

    /// <summary>Additional information.</summary>
    Note,
}
