// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Scripting;
using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.Problems;

/// <summary>
/// One row of the Problems panel: a problem of any asset family, shown by its type.
/// </summary>
/// <param name="Problem">The problem.</param>
public sealed record ProblemEntry(AssetProblem Problem)
{
    /// <summary>Gets the severity glyph: error, warning or info.</summary>
    public string Glyph => this.Problem.Severity switch
    {
        >= DiagnosticSeverity.Error => "",
        DiagnosticSeverity.Warning => "",
        _ => "",
    };

    /// <summary>Gets the severity as a word, for accessibility and tooltips.</summary>
    public string SeverityText => this.Problem.Severity switch
    {
        >= DiagnosticSeverity.Error => "Error",
        DiagnosticSeverity.Warning => "Warning",
        _ => "Note",
    };

    /// <summary>Gets a value indicating whether the problem is an error.</summary>
    public bool IsError => this.Problem.Severity >= DiagnosticSeverity.Error;

    /// <summary>Gets a value indicating whether the problem is a warning.</summary>
    public bool IsWarning => this.Problem.Severity == DiagnosticSeverity.Warning;

    /// <summary>Gets the code, such as <c>S0503</c> or <c>OXE.INPUT.ActionMissing</c>.</summary>
    public string Code => this.Problem.Code;

    /// <summary>Gets the one-line message.</summary>
    public string Message => this.Problem.Message;

    /// <summary>Gets the line and column, such as <c>12:18</c>, or empty when the problem has no position.</summary>
    public string Location => this.Problem.Line is { } line
        ? string.Create(CultureInfo.InvariantCulture, $"{line}:{this.Problem.Column ?? 1}")
        : string.Empty;

    /// <summary>Gets the full diagnostic: a script problem as the engine renders it, any other in the same layout.</summary>
    public string Rendered => this.Problem is ScriptProblem script
        ? script.Rendered
        : $"{this.SeverityText.ToLowerInvariant()}[{this.Code}]: {this.Message}\n"
            + (this.Problem.File is { } file ? $" --> {file}" + (this.Location.Length > 0 ? ":" + this.Location : string.Empty) + "\n" : string.Empty);

    /// <summary>Gets what the problem means and how to resolve it, or empty.</summary>
    public string Explanation => this.Problem.Explanation ?? string.Empty;

    /// <summary>Gets the name read by screen readers.</summary>
    public string AccessibleName => $"{this.SeverityText} {this.Code}: {this.Message}" + (this.Location.Length > 0 ? ", line " + this.Location : string.Empty);
}
