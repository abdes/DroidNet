// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using Oxygen.Editor.Scripting;

namespace Oxygen.Editor.ContentBrowser.Scripts;

/// <summary>
/// The latest analysis of one script file.
/// </summary>
/// <param name="SourcePath">The script file.</param>
/// <param name="ScriptUri">The script's asset identity, its cooked virtual path.</param>
/// <param name="Problems">The problems, in source order.</param>
/// <param name="Parameters">The declared parameters; when the script has errors, the last valid declarations.</param>
/// <param name="ParametersOutOfDate">Whether <paramref name="Parameters"/> come from an earlier, valid version of the
/// script because the current one has errors.</param>
public sealed record ScriptAnalysisSnapshot(
    string SourcePath,
    Uri ScriptUri,
    IReadOnlyList<ScriptProblem> Problems,
    IReadOnlyList<ScriptParameter> Parameters,
    bool ParametersOutOfDate)
{
    /// <summary>Gets the number of errors.</summary>
    public int ErrorCount => this.Problems.Count(static problem => problem.Severity == ScriptProblemSeverity.Error);

    /// <summary>Gets the number of warnings.</summary>
    public int WarningCount => this.Problems.Count(static problem => problem.Severity == ScriptProblemSeverity.Warning);

    /// <summary>Gets the error and warning counts as shown to the user, for example <c>2 errors, 1 warning</c>.</summary>
    public string ProblemSummary => DescribeCounts(this.ErrorCount, this.WarningCount);

    /// <summary>Gets a value indicating whether the file no longer exists; its problems are cleared.</summary>
    public bool IsRemoved { get; init; }

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
}
