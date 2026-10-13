// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// One problem the engine analyzer found in a script.
/// </summary>
/// <param name="Severity">The severity.</param>
/// <param name="Code">The engine code, such as <c>S0503</c>.</param>
/// <param name="Message">One line, lower case.</param>
/// <param name="SourcePath">The script file.</param>
/// <param name="Line">The 1-based line of the primary span, if any.</param>
/// <param name="Column">The 1-based column of the primary span, if any.</param>
/// <param name="Rendered">The full diagnostic with source excerpts, in the engine's layout.</param>
/// <param name="Explanation">What the code means and how to resolve it.</param>
public sealed record ScriptProblem(
    ScriptProblemSeverity Severity,
    string Code,
    string Message,
    string SourcePath,
    int? Line,
    int? Column,
    string Rendered,
    string Explanation);
