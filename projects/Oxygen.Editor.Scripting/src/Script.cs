// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting;

/// <summary>
/// What is known of one script: its declared parameters and its problems, from the analysis of its source or from a
/// cooked library. Each analysis replaces it.
/// </summary>
/// <remarks>
/// A scene's script slot names a script by <see cref="Identity"/>, its cooked virtual path, such as
/// <c>/Content/Scripts/Spin.oscript</c>.
/// </remarks>
public sealed record Script
{
    /// <summary>Gets the script's identity, its cooked virtual path.</summary>
    public required AssetPath Identity { get; init; }

    /// <summary>Gets where what is known of the script comes from.</summary>
    public required ScriptOrigin Origin { get; init; }

    /// <summary>Gets the script's source file; <see langword="null"/> for a library script.</summary>
    public FileSystemPath? SourcePath { get; init; }

    /// <summary>Gets the declared parameters, in declaration order; when the source has errors, those of its last
    /// valid version.</summary>
    public IReadOnlyList<ScriptParameter> Parameters { get; init; } = [];

    /// <summary>Gets the problems of the source, in source order; a library script has none.</summary>
    public IReadOnlyList<ScriptProblem> Problems { get; init; } = [];

    /// <summary>Gets a value indicating whether <see cref="Parameters"/> come from an earlier, valid version of the
    /// source because the current one has errors.</summary>
    public bool ParametersOutOfDate { get; init; }

    /// <summary>Gets the number of errors.</summary>
    public int ErrorCount => this.Problems.Count(static problem => problem.Severity >= DiagnosticSeverity.Error);

    /// <summary>Gets the number of warnings.</summary>
    public int WarningCount => this.Problems.Count(static problem => problem.Severity == DiagnosticSeverity.Warning);

    /// <summary>Gets the error and warning counts as shown to the user, for example <c>2 errors, 1 warning</c>.</summary>
    public string ProblemSummary => DescribeCounts(this.ErrorCount, this.WarningCount);

    /// <summary>Describes error and warning counts, for example <c>2 errors, 1 warning</c>.</summary>
    /// <param name="errors">The number of errors.</param>
    /// <param name="warnings">The number of warnings.</param>
    /// <returns>The description.</returns>
    public static string DescribeCounts(int errors, int warnings)
    {
        var parts = new List<string>();
        if (errors > 0)
        {
            parts.Add(string.Create(CultureInfo.CurrentCulture, $"{errors} {(errors == 1 ? "error" : "errors")}"));
        }

        if (warnings > 0)
        {
            parts.Add(string.Create(CultureInfo.CurrentCulture, $"{warnings} {(warnings == 1 ? "warning" : "warnings")}"));
        }

        return parts.Count == 0 ? "Notes only" : string.Join(", ", parts);
    }

    /// <summary>Finds a declared parameter.</summary>
    /// <param name="name">The parameter name.</param>
    /// <returns>The parameter, or <see langword="null"/> when the script does not declare it.</returns>
    public ScriptParameter? FindParameter(string name)
        => this.Parameters.FirstOrDefault(parameter => string.Equals(parameter.Name, name, StringComparison.Ordinal));
}
