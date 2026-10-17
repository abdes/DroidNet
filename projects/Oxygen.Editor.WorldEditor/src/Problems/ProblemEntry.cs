// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.World.Scripting;

namespace Oxygen.Editor.World.Problems;

/// <summary>
/// One row of the Problems panel.
/// </summary>
/// <param name="Problem">The script problem.</param>
public sealed record ProblemEntry(ScriptProblem Problem)
{
    /// <summary>Gets the severity glyph: error, warning or info.</summary>
    public string Glyph => this.Problem.Severity switch
    {
        ScriptProblemSeverity.Error => "",
        ScriptProblemSeverity.Warning => "",
        _ => "",
    };

    /// <summary>Gets the severity as a word, for accessibility and tooltips.</summary>
    public string SeverityText => this.Problem.Severity switch
    {
        ScriptProblemSeverity.Error => "Error",
        ScriptProblemSeverity.Warning => "Warning",
        _ => "Note",
    };

    /// <summary>Gets a value indicating whether the problem is an error.</summary>
    public bool IsError => this.Problem.Severity == ScriptProblemSeverity.Error;

    /// <summary>Gets a value indicating whether the problem is a warning.</summary>
    public bool IsWarning => this.Problem.Severity == ScriptProblemSeverity.Warning;

    /// <summary>Gets the engine code, such as <c>S0503</c>.</summary>
    public string Code => this.Problem.Code;

    /// <summary>Gets the one-line message.</summary>
    public string Message => this.Problem.Message;

    /// <summary>Gets the line and column, such as <c>12:18</c>, or empty when the problem has no position.</summary>
    public string Location => this.Problem.Line is { } line
        ? string.Create(CultureInfo.InvariantCulture, $"{line}:{this.Problem.Column ?? 1}")
        : string.Empty;

    /// <summary>Gets the full diagnostic with source excerpts.</summary>
    public string Rendered => this.Problem.Rendered;

    /// <summary>Gets the code's explanation: what it means and how to resolve it.</summary>
    public string Explanation => this.Problem.Explanation;

    /// <summary>Gets the name read by screen readers.</summary>
    public string AccessibleName => $"{this.SeverityText} {this.Code}: {this.Message}" + (this.Location.Length > 0 ? ", line " + this.Location : string.Empty);
}
